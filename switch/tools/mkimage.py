#!/usr/bin/env python3
"""SonicAdventureRe-nx: a fixed-address program image the Switch loader can move by whole 4 GB
windows.

The game's 32-bit pointers hold an address within its 4 GB window, so its program must sit inside
that window, and Horizon puts its heap and alias regions in different windows on each launch. The
loader adds the distance from the link's window to the chosen one to every 64-bit address in the
image; 32-bit addresses keep their low bits in any window and need nothing.

  mkimage.py LINK.elf OUT.bin [--symbol NAME]
      LINK.elf is linked with --emit-relocs. Each R_AARCH64_ABS64 whose value lies in the window is
      an address to move, and so is each word of .got in the window (the link fills those with no
      relocation). An absolute relocation in code against the window (MOVZ/MOVK of an address)
      can't be moved, and stops the build, naming it.
  mkimage.py LINK_A.elf LINK_B.elf OUT.bin [--symbol NAME]
      the same program linked for two windows: every 64-bit word that differs by exactly the
      windows' distance is an address; anything else that differs stops the build. (Works only
      where nothing holds a 32-bit address computed against one window's base.)

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


R_ABS64 = 257
R_MOVW_ABS = set(range(263, 271))  # MOVW_UABS_G0 .. G3 and the signed ones


def sections(data):
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x3a)
    out = []
    for i in range(shnum):
        out.append(struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize))
    names = out[shstrndx]
    def name(sh):
        o = names[4] + sh[0]
        return data[o:data.index(b'\0', o)].decode()
    return [(name(sh),) + sh for sh in out]


def relocs_mode(path, segs):
    """the image's addresses to move, from a link with --emit-relocs"""
    data = open(path, 'rb').read()
    secs = sections(data)
    start = segs[0][0]
    base = start & ~(WINDOW - 1)
    inside = lambda v: base <= v < base + WINDOW
    image = {}
    for v, fl, d, m in segs:
        image[v] = (fl, d)
    def word(addr):
        for v, (fl, d) in image.items():
            if v <= addr < v + len(d) - 7:
                return struct.unpack_from('<Q', d, addr - v)[0], fl
        return None, 0
    moved, problems = set(), []
    for name, sh_name, sh_type, flags, addr, off, size, link, info, align, entsize in secs:
        if sh_type != 4:  # SHT_RELA
            continue
        target = secs[info]
        if not target[3] & 2:  # not SHF_ALLOC
            continue
        exec_ = target[3] & 4
        for k in range(size // 24):
            r_off, r_info, r_add = struct.unpack_from('<QQq', data, off + k * 24)
            rtype = r_info & 0xffffffff
            if rtype == R_ABS64:
                val, _ = word(r_off)
                if val is not None and inside(val):
                    moved.add(r_off)
            elif rtype in R_MOVW_ABS and exec_:
                problems.append(f'{target[0]}+{r_off - target[4]:#x}: an absolute address in code (type {rtype})')
    for name, sh_name, sh_type, flags, addr, off, size, *_ in secs:
        if name in ('.got', '.got.plt') and flags & 2:
            for a in range(addr, addr + size, 8):
                val, _ = word(a)
                if val is not None and inside(val):
                    moved.add(a)
    if problems:
        sys.exit('mkimage: addresses this image can\'t move:\n  ' + '\n  '.join(problems[:20]) +
                 (f'\n  ... {len(problems)} in all' if len(problems) > 20 else ''))
    return sorted(a - start for a in moved)


def symbol(path, name):
    out = subprocess.run(['aarch64-linux-gnu-nm', path], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    sys.exit(f'mkimage: {path} has no symbol {name}')


def write(out, base, start, end, entry, segs, relocs, sym):
    with open(out, 'wb') as f:
        f.write(struct.pack(FORMAT, b'SAREIMG1', base, start, end, entry, len(segs), len(relocs), 0, 0, sym))
        for v, fl, d, m in segs:
            f.write(struct.pack('<QQQII', v - start, len(d), m, fl, 0))
        for v, fl, d, m in segs:
            f.write(d)
            f.write(b'\0' * ((-len(d)) % 8))
        f.write(struct.pack(f'<{len(relocs)}I', *relocs))
    print(f'mkimage: {out}: {len(segs)} segments, {end - start:#x} bytes in memory, '
          f'{len(relocs)} addresses to move, linked for {base:#x}')


def pair_mode(a, b):
    entry_a, segs_a = loads(a)
    entry_b, segs_b = loads(b)
    base_a = segs_a[0][0] & ~(WINDOW - 1)
    base_b = segs_b[0][0] & ~(WINDOW - 1)
    delta = base_b - base_a
    if not delta or len(segs_a) != len(segs_b) or entry_b - entry_a != delta:
        sys.exit('mkimage: the two links must be the same program in two different windows')
    start = segs_a[0][0]
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
                x, = struct.unpack_from('<Q', da, word)
                y, = struct.unpack_from('<Q', db, word)
                if y - x == delta:
                    relocs.append(va + word - start)
                    i = word + 8
                    continue
            problems.append(f'{"code" if fa & 1 else "data"} at {va + i:#x}: bytes differ, not a 64-bit address')
            i = word + 8
    if problems:
        sys.exit('mkimage: addresses this image can\'t move:\n  ' + '\n  '.join(problems[:20]) +
                 (f'\n  ... {len(problems)} in all' if len(problems) > 20 else ''))
    return entry_a, segs_a, relocs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--symbol')
    args = ap.parse_args()
    if len(args.files) == 3:
        a, b, out = args.files
        entry, segs, relocs = pair_mode(a, b)
    elif len(args.files) == 2:
        a, out = args.files
        entry, segs = loads(a)
        relocs = relocs_mode(a, segs)
    else:
        sys.exit('mkimage: LINK.elf OUT.bin, or LINK_A.elf LINK_B.elf OUT.bin')
    start = segs[0][0]
    base = start & ~(WINDOW - 1)
    end = (max(v + m for v, _, _, m in segs) + 0xfff) & ~0xfff
    sym = symbol(a, args.symbol) - start if args.symbol else 0
    write(out, base, start, end, entry, segs, relocs, sym)


if __name__ == '__main__':
    main()
