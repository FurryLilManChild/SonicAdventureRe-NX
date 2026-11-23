#!/usr/bin/env python3
"""SonicAdventureRe-nx: the characters' idle lines as C, for patch 0017.

sadx_idle_table.json is SADX's own table (Steam sonic.exe, 2004 layout, at 0x915CC8): for each
stage, the characters that have a line there and their voices, as indices of the US disc's
EVENT_ADX_US.AFS. This turns each voice into the Japanese EVENT_ADX.AFS index that plays the same
line (tools/english/voice_map.txt, read backwards), so VOICES Japanese plays the Japanese clip and
VOICES English swaps in the US one through the usual voice hook. US 316 has two Japanese clips of
the same length (321 and 322); 321 is used.

    python3 tools/idle/make_idle_lines.py > idle_lines.inc
"""
import json, os
here = os.path.dirname(os.path.abspath(__file__))
inv = {}
for line in open(os.path.join(here, '..', 'english', 'voice_map.txt')):
    if line.startswith('#') or not line.strip():
        continue
    jp, us = map(int, line.split())
    inv.setdefault(us, jp)
table = json.load(open(os.path.join(here, 'sadx_idle_table.json')))
# the line each character says anywhere it has none of its own (Sonic's own in stage 19)
general = {0: 390, 2: 391, 3: 392, 5: 393, 6: 394, 7: 395}
print('/* made by tools/idle/make_idle_lines.py from SADX\'s idle table (Japanese voice indices) */')
print('static const Sint16 idle_general[8] = {%s};' % ', '.join(
    str(inv[general[c]]) if c in general else '-1' for c in range(8)))
print('#define IDLE_GENERAL_SONIC_19 %d' % inv[396])
print('static const struct idle_line idle_lines[] = {')
for stage in sorted(table, key=int):
    for ch, voices in table[stage]:
        print('    {%s, %d, %d, {%s}},' % (stage, ch, len(voices), ', '.join(str(inv[v]) for v in voices)))
print('};')
