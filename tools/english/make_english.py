#!/usr/bin/env python3
"""SonicAdventureRe-nx: make the English layer's folder from a US Sonic Adventure disc.

  make_english.py US_DISC_FOLDER JP_DISC_FOLDER OUT_FOLDER

US_DISC_FOLDER is the US disc's files (the folder holding EVENT_ADX_US.AFS and the cutscene
modules EV0001.PRS ...; its SONICADV folder, or the disc's root above it), JP_DISC_FOLDER the
Japanese disc's the same way. OUT_FOLDER becomes the SD card's
sdmc:/switch/SonicAdventureRe-nx/english/. It writes:

  EVENT_ADX_US.AFS  the US cutscene voices in English, copied as they are
  voice_map.txt     which English clip each Japanese clip plays (this folder's voice_map.txt)
  subtitles.txt     each mapped cutscene subtitle in English: the cutscene, the CRC-32 of the
                    Japanese line, then the English parts, tab-separated, with \\n, \\t, \\a and \\\\
                    escaped
  hints.txt         each mapped stage hint page in English, as subtitles.txt: the module, the
                    CRC-32 of the Japanese page, then the English parts
  files.txt         the Japanese files below, one a line, the game reads from this folder in place
                    of the disc's: text (textures and messages) or voices (sound banks)
  SONICADV/         those files, under the disc's names (the renderer reads this folder's textures
                    ahead of the disc's, as it reads the disc's SONICADV folder)
  NAME.PRS/.PVR     a texture file under its Japanese name: each texture whose size matches the
                    US one in the same place is the US one (the Japanese texture's identity stays)
  NAME.MLT          a sound unit: each bank holding sounds from the US unit (the same rooms in
                    both), the Japanese layout kept; left Japanese where a bank has no match
  NAME.BIN          a field's messages (MR_MES, SS_MES, PAST_MES): the US file moved to the
                    Japanese file's address, its voices numbered as the Japanese game plays them

Python's standard library only.
"""
import os
import shutil
import struct
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
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    disc, jp_disc, out = sys.argv[1:4]
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
    hints(disc, out)
    files(disc, jp_disc, out)
    return 0


def hints(disc, out):
    """the stages' hint pages in English, from the US stage modules (STG01.PRS ...)"""
    modules, rows, missing = {}, [], set()
    lines = ['# SonicAdventureRe-nx English stage hints, made by tools/english/make_english.py\n']
    for row in open(os.path.join(here, 'hint_map.txt')):
        if row.startswith('#') or not row.strip():
            continue
        mod, crc, b, r, p = row.split()
        if mod not in modules:
            path = find(disc, mod.upper() + '.PRS')
            modules[mod] = common.hint_blocks(common.prs_decompress(open(path, 'rb').read())) if path else None
            if not path:
                missing.add(mod)
        blocks = modules[mod]
        if not blocks:
            continue
        try:
            parts = common.hint_english(blocks, int(b), int(r), int(p))
        except IndexError:
            parts = None
        if parts:
            lines.append('\t'.join([mod, crc] + [escape(x) for x in parts]) + '\n')
    with open(os.path.join(out, 'hints.txt'), 'w', encoding='ascii') as f:
        f.writelines(lines)
    print(f'hints: {len(lines) - 1} written', f'({len(missing)} modules not found)' if missing else '')


def voice_numbers():
    """a US clip number to the number the Japanese game plays it by: a Japanese clip mapped to it,
    else 0x2000 plus the US number (nx_english.c plays that US clip, and nothing in Japanese)"""
    back = {}
    for row in open(os.path.join(here, 'voice_map.txt')):
        if row.startswith('#') or not row.strip():
            continue
        jp, us = map(int, row.split())
        back.setdefault(us, jp)
    return lambda us: back.get(us, 0x2000 + us)


# files the US release laid out anew, whose textures match the Japanese ones in size but not in
# use: kept Japanese
KEEP_JAPANESE = {
    # the opening title screen: the US backgrounds have the logo cut out, which the US game draws
    # from textures of its own (8 to 14) that the Japanese game never draws
    'AVA_GTITLE0.PRS',
}


def files(disc, jp_disc, out):
    """the US disc's English files (NAME_E.EXT) under their Japanese names"""
    voice_of = voice_numbers()
    os.makedirs(os.path.join(out, 'SONICADV'), exist_ok=True)
    listed = []
    report = {'text': [0, 0, 0], 'voices': [0, 0, 0]}
    us_files = {}
    for root, _, names in os.walk(disc):
        for f in names:
            us_files[f.upper()] = os.path.join(root, f)
    jp_files = {}
    for root, _, names in os.walk(jp_disc):
        for f in names:
            jp_files[f.upper()] = os.path.join(root, f)
    for name in sorted(us_files):
        if '_E.' not in name:
            continue
        stem, ext = name.rsplit('_E.', 1)
        base = stem + '.' + ext
        if ext not in ('PRS', 'PVR', 'MLT', 'BIN') or base not in jp_files:
            continue
        if base in KEEP_JAPANESE:
            print(f'  {base}: kept Japanese (the US laid it out anew)')
            continue
        us = open(us_files[name], 'rb').read()
        jp = open(jp_files[base], 'rb').read()
        kind = 'voices' if ext == 'MLT' else 'text'
        try:
            if ext == 'PRS':
                ju, uu = common.prs_decompress(jp), common.prs_decompress(us)
                if ju[:4] != b'PVMH' or uu[:4] != b'PVMH':
                    print(f'  {base}: not a texture archive, left Japanese')
                    continue
                hj = 8 + struct.unpack_from('<I', ju, 4)[0]
                hu = 8 + struct.unpack_from('<I', uu, 4)[0]
                made, n, total = common.swap_textures(ju, uu, hj, hu)
                data = common.prs_store(made)
            elif ext == 'PVR':
                made, n, total = common.swap_textures(jp, us, 0, 0)
                data = made
            elif ext == 'MLT':
                if jp[:4] != b'SMLT' or us[:4] != b'SMLT':
                    continue
                data, n, total = common.swap_banks(jp, us)
                if n < total:
                    print(f'  {base}: {total - n} of its {total} sound banks have no US match, left Japanese')
                    continue
            else:
                if 'MES' not in base:
                    continue
                jw = [struct.unpack_from('<I', jp, k)[0] for k in range(0, len(jp) - 3, 4)]
                jp_base = min(w for w in jw if 0x0c000000 <= w < 0x0d000000) & ~0xfff
                data, voices = common.messages_rebased(us, jp_base, voice_of)
                n, total = 1, 1
        except (ValueError, IndexError, struct.error) as e:
            print(f'  {base}: left Japanese ({e})')
            continue
        r = report[kind]
        r[1] += n
        r[2] += total
        if n == 0:
            print(f'  {base}: nothing the same size, left Japanese')
            continue
        if n < total:
            print(f'  {base}: {n} of {total} in English (the rest differ in size)')
        with open(os.path.join(out, 'SONICADV', base), 'wb') as f:
            f.write(data)
        listed.append(f'{kind} {base}\n')
        r[0] += 1
    with open(os.path.join(out, 'files.txt'), 'w') as f:
        f.write('# SonicAdventureRe-nx: the files read from this folder in place of the disc\'s\n')
        f.writelines(listed)
    t, v = report['text'], report['voices']
    print(f'text files: {t[0]} ({t[1]} of {t[2]} textures or message files in English); '
          f'voice banks: {v[0]} files ({v[1]} of {v[2]} banks in English)')


if __name__ == '__main__':
    sys.exit(main())
