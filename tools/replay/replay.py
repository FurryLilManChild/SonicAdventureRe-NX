#!/usr/bin/env python3
"""SonicAdventureRe-nx: turn a replay (logs/replay_NNN.bin, the left stick click) into PNGs.

  replay.py replay_NNN.bin OUT_DIR [--every N]

Writes OUT_DIR/pic-PPPPPP.png per picture (stretched to 854x480 where the run was 16:9) and
OUT_DIR/frames.txt: picture, vsync, game_mode, stage, act per line. The format is in
switch/runtime/nx_capture.c.
"""
import os
import struct
import sys
import zlib


def png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, data):
        c = kind + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def stretch(rgb, w, h, ow):
    out = bytearray(ow * h * 3)
    cols = [(x * w // ow) * 3 for x in range(ow)]
    for y in range(h):
        row = rgb[y * w * 3:(y + 1) * w * 3]
        o = y * ow * 3
        for x, c in enumerate(cols):
            out[o + x * 3:o + x * 3 + 3] = row[c:c + 3]
    return bytes(out)


def main():
    path, out = sys.argv[1], sys.argv[2]
    every = int(sys.argv[sys.argv.index('--every') + 1]) if '--every' in sys.argv else 1
    d = open(path, 'rb').read()
    if d[:8] != b'SARNXRP1':
        raise SystemExit('not a SonicAdventureRe-nx replay')
    count, w, h, wide = struct.unpack_from('<IIII', d, 8)
    build = d[24:40].split(b'\0')[0].decode()
    recs = [struct.unpack_from('<IIiiiI', d, 40 + 24 * i) for i in range(count)]
    base = 40 + 24 * count
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'frames.txt'), 'w') as f:
        f.write(f'# build {build}, {count} pictures, {w}x{h}{" shown at 16:9" if wide else ""}\n')
        f.write('# index picture vsync game_mode stage act\n')
        for i, r in enumerate(recs):
            f.write(f'{i} {r[0]} {r[1]} {r[2]} {r[3]} {r[4]}\n')
    for i in range(0, count, every):
        rgb = d[base + i * w * h * 3:base + (i + 1) * w * h * 3]
        ow = 854 if wide else w
        png(os.path.join(out, f'pic-{recs[i][0]:06d}.png'), ow, h, stretch(rgb, w, h, ow) if wide else rgb)
    print(f'{count} pictures (build {build}) from vsync {recs[0][1]} to {recs[-1][1]} written to {out}')


if __name__ == '__main__':
    main()
