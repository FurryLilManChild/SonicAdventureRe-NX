#!/usr/bin/env python3
"""SonicAdventureRe-nx: tools/english's maps as C, for the loader's English build
(switch/loader/english_build.c). Numbers only, as the maps are.

  maps2c.py TOOLS_ENGLISH_DIR OUT.c
"""
import os
import sys
import zlib


def rows(path):
    return [l.split() for l in open(path) if l.strip() and not l.startswith('#')]


def main():
    src, out = sys.argv[1:3]
    sub = rows(os.path.join(src, 'subtitle_map.txt'))
    hint = rows(os.path.join(src, 'hint_map.txt'))
    voice = rows(os.path.join(src, 'voice_map.txt'))
    crc = 0
    for name in ('subtitle_map.txt', 'hint_map.txt', 'voice_map.txt'):
        crc = zlib.crc32(open(os.path.join(src, name), 'rb').read(), crc)
    with open(out, 'w') as f:
        f.write('/* SonicAdventureRe-nx: tools/english\'s maps, written by switch/tools/maps2c.py */\n')
        f.write('#include <stdint.h>\n')
        f.write('struct subtitle_row { const char *event; uint32_t crc; int table, first, count; };\n')
        f.write('struct hint_row { const char *module; uint32_t crc; int block, run, page; };\n')
        f.write('const struct subtitle_row english_subtitle_map[] = {\n')
        for ev, c, t, first, count in sub:
            f.write(f'    {{"{ev}", 0x{c}u, {t}, {first}, {count}}},\n')
        f.write('};\n')
        f.write(f'const int english_subtitle_rows = {len(sub)};\n')
        f.write('const struct hint_row english_hint_map[] = {\n')
        for mod, c, b, r, p in hint:
            f.write(f'    {{"{mod}", 0x{c}u, {b}, {r}, {p}}},\n')
        f.write('};\n')
        f.write(f'const int english_hint_rows = {len(hint)};\n')
        f.write('const short english_voice_map[][2] = {\n')
        for jp, us in voice:
            f.write(f'    {{{jp}, {us}}},\n')
        f.write('};\n')
        f.write(f'const int english_voice_rows = {len(voice)};\n')
        f.write(f'const uint32_t english_maps_crc = 0x{crc:08x}u;\n')


if __name__ == '__main__':
    main()
