#!/usr/bin/env python3
"""Map the Japanese game's cutscene subtitles to the US disc's English lines (SonicAdventureRe-nx).

Each US cutscene module (EVxxxx, loaded at 0x0cb80000) holds a table of five pointers, one per
language (Japanese, English, French, Spanish, German). Each points to an array of N line pointers,
and the five arrays are parallel: slot i is the same moment of the cutscene in every language.
Where the English splits one Japanese line in two, the Japanese array has an empty (null) slot.

So a Japanese line in slot s owns the English lines in slots s up to the next Japanese line. The
decomp's Japanese subtitles (build/gen/events/<ev>/script/text.inc, the TEXT_n that begin with
\\a) are matched to the US Japanese slots in order, which is checked by the count and, where the
US kept the line unchanged, by the bytes.

  event_text.py GEN_EVENTS_DIR US_EVENTS_DIR OUT.json

US_EVENTS_DIR holds the US event modules decompressed (EV0001.bin, ...). OUT.json:
{ "ev0006": { "1": ["Hey Sonic.  \\nLong time no see, huh?"], ... }, ... } in Latin-1 text.
Measured: 1,018 of 1,085 Japanese lines align by count; 647 match the US bytes exactly.
"""
import json
import os
import re
import struct
import sys

BASE = 0x0cb80000
ESCAPES = {'a': 7, 'n': 10, 't': 9, '\\': 92, '"': 34, '0': 0}


def c_string(s):
    out = bytearray()
    i = 0
    while i < len(s):
        if s[i] != '\\':
            out.append(ord(s[i]))
            i += 1
            continue
        if s[i + 1] == 'x':
            digits = re.match(r'[0-9a-fA-F]+', s[i + 2:]).group()
            out.append(int(digits, 16) & 255)
            i += 2 + len(digits)
        else:
            out.append(ESCAPES[s[i + 1]])
            i += 2
    return bytes(out)


def jp_subtitles(text_inc):
    """TEXT_n -> bytes, for the strings that are subtitles (they begin with \\a)"""
    src = open(text_inc, encoding='latin-1').read()
    # a define may continue over lines as adjacent literals
    out = {}
    for m in re.finditer(r'#define TEXT_(\d+) ((?:"(?:[^"\\]|\\.)*"\s*\\?\s*)+)', src):
        body = b''.join(c_string(p) for p in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)))
        if body.startswith(b'\x07'):
            out[int(m.group(1))] = body
    return dict(sorted(out.items()))


def string_at(d, ptr):
    if not ptr:
        return None
    o = ptr - BASE
    return d[o:d.index(b'\0', o)]


def language_tables(d):
    """each table: five parallel arrays of strings (None for an empty slot)"""
    words = [struct.unpack_from('<I', d, k)[0] for k in range(0, len(d) - 3, 4)]
    inside = lambda w: BASE <= w < BASE + len(d)
    found = []
    for k in range(len(words) - 4):
        w = words[k:k + 5]
        if not all(inside(x) for x in w):
            continue
        stride = w[1] - w[0]
        if stride <= 0 or stride % 4 or any(w[i + 1] - w[i] != stride for i in range(4)):
            continue
        n = stride // 4
        arrays = [[struct.unpack_from('<I', d, x - BASE + 4 * i)[0] for i in range(n)] for x in w]
        if all(p == 0 or inside(p) for a in arrays for p in a) and any(arrays[1]):
            found.append([[string_at(d, p) for p in a] for a in arrays])
    return found


def main():
    gen, us_dir, out_path = sys.argv[1:4]
    result, aligned, total, exact = {}, 0, 0, 0
    for ev in sorted(os.listdir(gen)):
        inc = os.path.join(gen, ev, 'script', 'text.inc')
        mod = os.path.join(us_dir, ev.upper() + '.bin')
        if not os.path.exists(inc) or not os.path.exists(mod):
            continue
        jp = jp_subtitles(inc)
        if not jp:
            continue
        total += len(jp)
        d = open(mod, 'rb').read()
        slots = []  # (japanese bytes, [english lines]) in order
        for table in language_tables(d):
            japanese, english = table[0], table[1]
            for i, line in enumerate(japanese):
                if line is not None:
                    slots.append((line, []))
                if slots and english[i] is not None:
                    slots[-1][1].append(english[i])
        if len(slots) != len(jp):
            continue
        aligned += len(jp)
        exact += sum(1 for (us_jp, _), mine in zip(slots, jp.values()) if us_jp == mine)
        result[ev] = {str(n): [e.decode('latin-1') for e in en] for n, (_, en) in zip(jp, slots)}
    json.dump(result, open(out_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print(f'{aligned} of {total} Japanese subtitles aligned to English lines ({exact} unchanged in the US)')


if __name__ == '__main__':
    main()
