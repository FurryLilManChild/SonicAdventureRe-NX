#!/usr/bin/env python3
"""SonicAdventureRe-nx: what of the disc is still in a stripped image.

  audit.py IMAGE.img DECOMP [ELF]

Every 16-byte window of the image (at 4-byte steps, skipping windows of mostly zeros or one
repeated byte) is looked for in every disc file the image's content and bodies come from (the
decomp's images, decoded), at any byte position. Prints how many windows are found, in which
of the ELF's sections (when ELF is given), and a few of them.
"""
import os
import struct
import sys

import numpy as np

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..', '..', 'tools', 'english'))
from common import prs_decompress  # noqa: E402
sys.path.insert(0, here)
from strip import read_image, elf_sections  # noqa: E402

W = 16


def hashes(b, step):
    a = np.frombuffer(b, dtype=np.uint8)
    n = (len(a) - W) // step + 1
    if n <= 0:
        return np.zeros(0, np.uint64), np.zeros(0, bool)
    idx = np.arange(n) * step
    lo = np.zeros(n, np.uint64)
    hi = np.zeros(n, np.uint64)
    for k in range(8):
        lo |= a[idx + k].astype(np.uint64) << np.uint64(8 * k)
        hi |= a[idx + 8 + k].astype(np.uint64) << np.uint64(8 * k)
    h = lo * np.uint64(0x9E3779B97F4A7C15) ^ (hi + np.uint64(0x632BE59BD9B4E019)) * np.uint64(0xC2B2AE3D27D4EB4F)
    zeros = np.zeros(n, np.uint8)
    same = np.ones(n, bool)
    first = a[idx]
    for k in range(W):
        col = a[idx + k]
        zeros += col == 0
        same &= col == first
    ok = (zeros < W - 4) & ~same
    return h, ok


def main():
    img, decomp = sys.argv[1], os.path.abspath(sys.argv[2])
    elf = sys.argv[3] if len(sys.argv) > 3 else None
    h, segs, body, rels, _, _ = read_image(img)
    start = h[2]
    ih, ok = hashes(bytes(body), 4)
    order = np.argsort(ih)
    sh = ih[order]
    found = np.zeros(len(ih), bool)
    sys.path.insert(0, os.path.join(decomp, 'tools'))
    cwd = os.getcwd()
    os.chdir(decomp)
    import images
    files = images.files()
    os.chdir(cwd)
    paths = sorted(set(files.values()))
    for p in paths:
        full = os.path.join(decomp, 'orig', p)
        if not os.path.exists(full):
            continue
        d = open(full, 'rb').read()
        if p.upper().endswith('.PRS'):
            dec = os.path.join(decomp, 'build/gen/img', os.path.basename(p)[:-4] + '.BIN')
            d = open(dec, 'rb').read() if os.path.exists(dec) else prs_decompress(d)
        sh_src, _ = hashes(d, 1)
        pos = np.searchsorted(sh, sh_src)
        pos[pos >= len(sh)] = len(sh) - 1
        hit = sh[pos] == sh_src
        for q in np.unique(sh_src[hit]):
            a = np.searchsorted(sh, q)
            b = np.searchsorted(sh, q, side='right')
            found[order[a:b]] = True
    found &= ok
    print(f'audit: {int(ok.sum())} windows checked, {int(found.sum())} found on the disc')
    if elf and found.any():
        secs = sorted((a - start, a - start + s, n) for n, (a, s, f) in elf_sections(elf).items() if s and a >= start)
        per = {}
        for w in np.nonzero(found)[0]:
            off = int(w) * 4
            name = next((n for a, e, n in secs if a <= off < e), '?')
            per[name] = per.get(name, 0) + 1
        for n, c in sorted(per.items(), key=lambda kv: -kv[1])[:25]:
            print(f'  {n}: {c}')
    for w in np.nonzero(found)[0][:10]:
        off = int(w) * 4
        print(f'  image+{off:#x}: {bytes(body[off:off + W])!r}')


if __name__ == '__main__':
    main()
