#!/usr/bin/env python3
"""SonicAdventureRe-nx: write the English layer's maps, which the repository ships (numbers only,
no game text or sound). Run by the port's developers, not the players.

  make_maps.py GEN_EVENTS_DIR US_EVENTS_DIR VOICE_MAP.json OUT_DIR [GEN_MODULES_DIR US_MODULES_DIR]

GEN_EVENTS_DIR is the decomp's build/gen/events (the Japanese release's subtitles), US_EVENTS_DIR
the US disc's cutscene modules decompressed (EV0001.bin, ...), VOICE_MAP.json event_voices.py's
output. It writes:

  subtitle_map.txt  event, CRC-32 of the Japanese subtitle's bytes (as the game passes them),
                    the US module's language table, first slot and slot count
  voice_map.txt     a Japanese EVENT_ADX.AFS index and its EVENT_ADX_US.AFS index
  hint_map.txt      a stage's hint page: module, CRC-32 of the Japanese page, and where its US
                    Japanese page sits (block, run, page; common.hint_blocks); with the modules
                    (the decomp's build/gen/modules, and the US disc's stage modules decompressed)
"""
import json
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: E402
from event_text import jp_subtitles  # noqa: E402


def main():
    gen, us_dir, voice_json, out = sys.argv[1:5]
    lines, total, aligned = [], 0, 0
    for ev in sorted(os.listdir(gen)):
        inc = os.path.join(gen, ev, 'script', 'text.inc')
        mod = os.path.join(us_dir, ev.upper() + '.bin')
        if not os.path.exists(inc) or not os.path.exists(mod):
            continue
        jp = jp_subtitles(inc)
        if not jp:
            continue
        total += len(jp)
        found = common.slots(open(mod, 'rb').read())
        if len(found) != len(jp):
            continue
        aligned += len(jp)
        for mine, (table, first, count, _) in zip(jp.values(), found):
            if count:
                lines.append(f'{ev} {zlib.crc32(mine):08x} {table} {first} {count}\n')
    with open(os.path.join(out, 'subtitle_map.txt'), 'w') as f:
        f.write('# SonicAdventureRe-nx: event, CRC-32 of the Japanese subtitle, US table, first slot, slots\n')
        f.writelines(lines)
    voices = json.load(open(voice_json))
    with open(os.path.join(out, 'voice_map.txt'), 'w') as f:
        f.write('# SonicAdventureRe-nx: Japanese EVENT_ADX.AFS index, EVENT_ADX_US.AFS index\n')
        for jp_index, us_index in sorted((int(k), v) for k, v in voices.items()):
            f.write(f'{jp_index} {us_index}\n')
    print(f'{len(lines)} subtitles mapped ({aligned} of {total} aligned), {len(voices)} voices')
    if len(sys.argv) > 6:
        hints(sys.argv[5], sys.argv[6], out)


def hints(gen_modules, us_modules, out):
    """each Japanese hint page to the US module's same page, matched by its words (the two
    releases' Japanese agree, or nearly)"""
    import difflib
    import re
    from event_text import c_string

    def texts(inc):
        src = open(inc, encoding='latin-1').read()
        found = {}
        for m in re.finditer(r'#define TEXT_(\d+) ((?:"(?:[^"\\]|\\.)*"\s*\\?\s*)+)', src):
            found[int(m.group(1))] = b''.join(c_string(x) for x in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)))
        return found

    bare = lambda t: bytes(b for b in t if b not in (7, 9, 10))
    rows, total = [], 0
    for mod in sorted(os.listdir(gen_modules)):
        # the stages' scripts, and Station Square's people (its hint boxes' rows)
        incs = [os.path.join(gen_modules, mod, d, 'text.inc') for d in ('scripts', 'people')]
        incs = [i for i in incs if os.path.exists(i)]
        us = os.path.join(us_modules, mod.upper() + '.bin')
        if not incs or not os.path.exists(us):
            continue
        blocks = common.hint_blocks(open(us, 'rb').read())
        # only pages that have English (a better match without any would hide one with it)
        pages = [(b, r, p, t) for b, (J, _) in enumerate(blocks) for r, run in enumerate(J) for p, t in enumerate(run[1])
                 if common.hint_english(blocks, b, r, p)]
        for t in [t for inc in incs for t in texts(inc).values()]:
            # Japanese: mostly Shift JIS (a page may start without the tab or bell is_japanese wants)
            if len(t) <= 4 or sum(1 for b in t if b >= 0x81) <= len(t) // 3:
                continue
            total += 1
            best = max(pages, key=lambda c: difflib.SequenceMatcher(None, bare(t), bare(c[3])).ratio(), default=None)
            if best and difflib.SequenceMatcher(None, bare(t), bare(best[3])).ratio() >= 0.6 \
                    and common.hint_english(blocks, *best[:3]):
                rows.append(f'{mod} {zlib.crc32(t):08x} {best[0]} {best[1]} {best[2]}\n')
    with open(os.path.join(out, 'hint_map.txt'), 'w') as f:
        f.write('# SonicAdventureRe-nx: module, CRC-32 of the Japanese hint page, US block, run, page\n')
        f.writelines(rows)
    print(f'{len(rows)} of {total} hint pages mapped')


if __name__ == '__main__':
    main()
