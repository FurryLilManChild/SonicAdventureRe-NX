#!/usr/bin/env python3
"""SonicAdventureRe-nx: write the English layer's maps, which the repository ships (numbers only,
no game text or sound). Run by the port's developers, not the players.

  make_maps.py GEN_EVENTS_DIR US_EVENTS_DIR VOICE_MAP.json OUT_DIR

GEN_EVENTS_DIR is the decomp's build/gen/events (the Japanese release's subtitles), US_EVENTS_DIR
the US disc's cutscene modules decompressed (EV0001.bin, ...), VOICE_MAP.json event_voices.py's
output. It writes:

  subtitle_map.txt  event, CRC-32 of the Japanese subtitle's bytes (as the game passes them),
                    the US module's language table, first slot and slot count
  voice_map.txt     a Japanese EVENT_ADX.AFS index and its EVENT_ADX_US.AFS index
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


if __name__ == '__main__':
    main()
