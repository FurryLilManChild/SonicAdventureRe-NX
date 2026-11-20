#!/usr/bin/env python3
"""SonicAdventureRe-nx: a fixed-address program image the Switch loader can move by whole 4 GB
windows.

The game's 32-bit pointers hold an address within its 4 GB window, so its program must sit inside
that window, and Horizon puts its heap and alias regions in different windows on each launch. The
program is linked twice, for two windows; every 64-bit word that differs between the two links by
exactly the windows' distance is an address, and the loader adds the distance from the first link
to the chosen window to each. Anything else that differs (an instruction, a 32-bit word, a word off
by another amount) is an address this scheme can't move, and stops the build, naming it.

  mkimage.py LINK_A.elf LINK_B.elf OUT.bin [--symbol NAME]

OUT.bin: header (see FORMAT), the segments of LINK_A, then the moved words' offsets (u32 each,
from the image's start). --symbol records a symbol's offset in the header (the probe's handoff).
"""
import argparse
import struct
import subprocess
import sys

FORMAT = '<8sQQQQIIIIQ'  # magic, link base, image start, image end, entry, segments, relocations,
                         # flags, pad, symbol offset
WINDOW = 1 << 32


def loads(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'\x7fELF' and data[4] == 2, path
    phoff, = struct.unpack_from('<Q', data, 0x20)
    phentsize, phnum = struct.unpack_from('<HH', data, 0x36)
    entry, = struct.unpack_from('<Q', data, 0x18)
    segs = []
    for i in range(phnum):
        p_type, p_flags, p_offset, p_vaddr, _, p_filesz, p_memsz, _ = struct.unpack_from(
            '<IIQQQQQQ', data, phoff + i * phentsize)
        if p_type == 1 and p_memsz:
            segs.append((p_vaddr, p_flags, data[p_offset:p_offset + p_filesz], p_memsz))
    return entry, segs


def symbol(path, name):
    out = subprocess.run(['aarch64-linux-gnu-nm', path], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    sys.exit(f'mkimage: {path} has no symbol {name}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('out')
    ap.add_argument('--symbol')
    args = ap.parse_args()
    entry_a, segs_a = loads(args.a)
    entry_b, segs_b = loads(args.b)
    base_a = segs_a[0][0] & ~(WINDOW - 1)
    base_b = segs_b[0][0] & ~(WINDOW - 1)
    delta = base_b - base_a
    if not delta or len(segs_a) != len(segs_b) or entry_b - entry_a != delta:
        sys.exit('mkimage: the two links must be the same program in two different windows')
    start = segs_a[0][0]
    end = max(v + m for v, _, _, m in segs_a)
    end = (end + 0xfff) & ~0xfff
    relocs, problems = [], []
    for (va, fa, da, ma), (vb, fb, db, mb) in zip(segs_a, segs_b):
        if vb - va != delta or ma != mb or len(da) != len(db) or fa != fb:
            sys.exit(f'mkimage: segment at {va:#x} differs in shape between the links')
        if da == db:
            continue
        i = 0
        n = len(da)
        while i < n:
            if da[i] == db[i]:
                i += 1
                continue
            word = i & ~7
            if word + 8 <= n:
                a, = struct.unpack_from('<Q', da, word)
                b, = struct.unpack_from('<Q', db, word)
                if b - a == delta and da[word + 8:word + 8] == db[word + 8:word + 8]:
                    relocs.append(va + word - start)
                    i = word + 8
                    continue
            problems.append(f'{"code" if fa & 1 else "data"} at {va + i:#x}: bytes differ, not a 64-bit address')
            i = word + 8
    if problems:
        sys.exit('mkimage: addresses this image can\'t move:\n  ' + '\n  '.join(problems[:20]) +
                 (f'\n  ... {len(problems)} in all' if len(problems) > 20 else ''))
    sym = symbol(args.a, args.symbol) - start if args.symbol else 0
    with open(args.out, 'wb') as f:
        f.write(struct.pack(FORMAT, b'SAREIMG1', base_a, start, end, entry_a, len(segs_a), len(relocs), 0, 0,
                            sym))
        for v, fl, d, m in segs_a:
            f.write(struct.pack('<QQQII', v - start, len(d), m, fl, 0))
        for v, fl, d, m in segs_a:
            f.write(d)
            f.write(b'\0' * ((-len(d)) % 8))
        f.write(struct.pack(f'<{len(relocs)}I', *relocs))
    print(f'mkimage: {args.out}: {len(segs_a)} segments, {end - start:#x} bytes in memory, '
          f'{len(relocs)} addresses to move, linked for {base_a:#x}')


if __name__ == '__main__':
    main()
