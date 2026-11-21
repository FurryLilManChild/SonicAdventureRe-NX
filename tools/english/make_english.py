#!/usr/bin/env python3
"""SonicAdventureRe-nx: make the English layer's folder from a US Sonic Adventure disc.

  make_english.py US_DISC_FOLDER OUT_FOLDER

US_DISC_FOLDER is the US disc's files (the folder holding EVENT_ADX_US.AFS and the cutscene
modules EV0001.PRS ...; its SONICADV folder, or the disc's root above it). OUT_FOLDER becomes the
SD card's sdmc:/switch/SonicAdventureRe-nx/english/. It writes:

  EVENT_ADX_US.AFS  the US cutscene voices in English, copied as they are
  voice_map.txt     which English clip each Japanese clip plays (this folder's voice_map.txt)
  subtitles.txt     each mapped cutscene subtitle in English: the cutscene, the CRC-32 of the
                    Japanese line, then the English parts, tab-separated, with \\n, \\t, \\a and \\\\
                    escaped

Python's standard library only.
"""
import os
import shutil
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import common  # noqa: E402


def find(folder, name):
    for root, _, files in os.walk(folder):
        for f in files:
            if f.upper() == name.upper():
                return os.path.join(root, f)
    return None


def escape(b):
    out = []
    for c in b:
        if c == 0x5c:
            out.append('\\\\')
        elif c == 10:
            out.append('\\n')
        elif c == 9:
            out.append('\\t')
        elif c == 7:
            out.append('\\a')
        elif c < 0x20 or c >= 0x7f:
            out.append('\\x%02x' % c)
        else:
            out.append(chr(c))
    return ''.join(out)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    disc, out = sys.argv[1:3]
    voices = find(disc, 'EVENT_ADX_US.AFS')
    if not voices:
        print('no EVENT_ADX_US.AFS under', disc, '(is it the US disc?)')
        return 1
    os.makedirs(out, exist_ok=True)
    shutil.copyfile(voices, os.path.join(out, 'EVENT_ADX_US.AFS'))
    shutil.copyfile(os.path.join(here, 'voice_map.txt'), os.path.join(out, 'voice_map.txt'))
    print('voices: EVENT_ADX_US.AFS copied,', len(common.afs_entries(open(voices, 'rb').read(64 * 1024))),
          'clips')

    modules, written, missing = {}, 0, set()
    lines = ['# SonicAdventureRe-nx English subtitles, made by tools/english/make_english.py\n']
    for row in open(os.path.join(here, 'subtitle_map.txt')):
        if row.startswith('#') or not row.strip():
            continue
        ev, crc, table, first, count = row.split()
        if ev not in modules:
            path = find(disc, ev.upper() + '.PRS')
            modules[ev] = common.prs_decompress(open(path, 'rb').read()) if path else None
            if not path:
                missing.add(ev)
        if modules[ev] is None:
            continue
        parts = common.english_lines(modules[ev], int(table), int(first), int(count))
        if parts:
            lines.append('\t'.join([ev, crc] + [escape(p) for p in parts]) + '\n')
            written += 1
    with open(os.path.join(out, 'subtitles.txt'), 'w', encoding='ascii') as f:
        f.writelines(lines)
    print(f'subtitles: {written} written', f'({len(missing)} cutscenes not found)' if missing else '')
    return 0


if __name__ == '__main__':
    sys.exit(main())
