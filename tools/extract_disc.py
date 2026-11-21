#!/usr/bin/env python3
"""SonicAdventureRe-nx: the Japanese disc's files from its GD-ROM data track (Redump track03.bin,
raw 2352-byte sectors, starting at LBA 45000), into a folder: the folder the Switch build reads
(sdmc:/switch/SonicAdventureRe-nx/disc/). Resumable: a file already there at its full size is
skipped, so it can run in short passes (--seconds).

  extract_disc.py TRACK03.BIN OUT_DIR [--seconds N]
"""
import argparse
import os
import struct
import sys
import time

RAW = 2352
DATA = 16
START = 45000


class Track:
    def __init__(self, path):
        self.f = open(path, 'rb')

    def sectors(self, lba, count):
        out = bytearray()
        for i in range(count):
            self.f.seek((lba + i - START) * RAW + DATA)
            out += self.f.read(2048)
        return bytes(out)


def entries(track, lba, size):
    data = track.sectors(lba, (size + 2047) // 2048)
    i = 0
    while i < len(data):
        n = data[i]
        if n == 0:
            i = (i // 2048 + 1) * 2048
            continue
        ext, = struct.unpack_from('<I', data, i + 2)
        length, = struct.unpack_from('<I', data, i + 10)
        flags = data[i + 25]
        name_len = data[i + 32]
        name = data[i + 33:i + 33 + name_len].decode('ascii', 'replace')
        i += n
        if name in ('\x00', '\x01'):
            continue
        yield name.split(';')[0], ext, length, bool(flags & 2)


def walk(track, lba, size, prefix=''):
    for name, ext, length, is_dir in entries(track, lba, size):
        path = prefix + name
        if is_dir:
            yield from walk(track, ext, length, path + '/')
        else:
            yield path, ext, length


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('track')
    ap.add_argument('out')
    ap.add_argument('--seconds', type=float, default=0)
    args = ap.parse_args()
    t0 = time.time()
    track = Track(args.track)
    pvd = track.sectors(START + 16, 1)
    if pvd[1:6] != b'CD001':
        sys.exit('extract_disc: no ISO 9660 volume at LBA 45016: not the data track')
    root_lba, = struct.unpack_from('<I', pvd, 156 + 2)
    root_size, = struct.unpack_from('<I', pvd, 156 + 10)
    files = list(walk(track, root_lba, root_size))
    done = 0
    for path, lba, length in files:
        dest = os.path.join(args.out, path)
        if os.path.exists(dest) and os.path.getsize(dest) == length:
            done += 1
            continue
        if args.seconds and time.time() - t0 > args.seconds:
            break
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        tmp = dest + '.part'
        with open(tmp, 'wb') as out:
            left, at = length, lba
            while left:
                n = min(left, 2048 * 512)
                chunk = track.sectors(at, (n + 2047) // 2048)[:n]
                out.write(chunk)
                left -= n
                at += (n + 2047) // 2048
        os.replace(tmp, dest)
        done += 1
    print(f'extract_disc: {done} of {len(files)} files in {args.out}'
          + ('' if done == len(files) else ' (run again to continue)'))
    return 0 if done == len(files) else 3


if __name__ == '__main__':
    sys.exit(main())
