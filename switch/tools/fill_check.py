#!/usr/bin/env python3
"""SonicAdventureRe-nx: rebuild a stripped image from a disc folder the way the loader does, and
compare it with the full image.

  fill_check.py STRIPPED.img SonicAdventureRe-nx.fill DISC_FOLDER FULL.img
"""
import hashlib
import os
import struct
import sys
import zlib

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..', '..', 'tools', 'english'))
sys.path.insert(0, here)
from common import prs_decompress  # noqa: E402
from strip import read_image  # noqa: E402


def main():
    img, fill, disc, full = sys.argv[1:5]
    h, segs, body, rels, _, _ = read_image(img)
    hf, _, body_full, _, _, _ = read_image(full)
    assert h[7] == 1, 'not a stripped image'
    t = open(fill, 'rb').read()
    magic, nsrc, npc, crc, _ = struct.unpack_from('<8sIIII', t, 0)
    assert magic == b'SARNXFL1'
    pos = 24
    srcs = []
    for i in range(nsrc):
        path, size, sha, prs, _ = struct.unpack_from('<32sI20sII', t, pos)
        srcs.append((path.rstrip(b'\0').decode(), size, sha, prs))
        pos += 64
    pcs_at = pos
    assert zlib.crc32(t[24:pcs_at + 16 * npc]) == crc == h[8], 'crc'
    data = []
    for path, size, sha, prs in srcs:
        d = open(os.path.join(disc, path), 'rb').read()
        if prs:
            d = prs_decompress(d)
        assert len(d) == size and hashlib.sha1(d).digest() == sha, path
        data.append(d)
    for i in range(npc):
        at, s, so, n = struct.unpack_from('<IIII', t, pcs_at + 16 * i)
        body[at:at + n] = data[s][so:so + n]
    print('fill_check:', 'the same as the full image' if body == body_full else 'DIFFERENT from the full image',
          f'({nsrc} files, {npc} pieces)')
    return 0 if body == body_full else 1


if __name__ == '__main__':
    sys.exit(main())
