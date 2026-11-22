#!/usr/bin/env python3
"""SonicAdventureRe-nx: zero bodies for the float tables, which the decomp's stubs give as a bare 0
and the native build's float pass can't read. Each written from the disc's body with every number
0, so the shape (and the size) stays and no value does.

  stub_floats.py GEN_DIR DECOMP_STUB_DIR COPY_GEN_DIR

rewrites, in COPY_GEN_DIR, each body the decomp has a stub for (in DECOMP_STUB_DIR) whose disc
body (in GEN_DIR) holds floats. It writes a new file and renames it over the old name, so a file
the copy shares with decomp/ by a hard link is never written through.
"""
import os
import re
import sys

NUMBER = re.compile(r'(?<![\w.])-?(?:0[xX][0-9a-fA-F]*\.?[0-9a-fA-F]*(?:[pP][+-]?\d+)?|\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?)[fFuUlL]*(?![\w.])')
FLOAT = re.compile(r'\d[fF]\b|[pP][+-]?\d+[fF]?|\d\.\d')


def main():
    gen, stub, copy = sys.argv[1:4]
    n = 0
    for root, _, files in os.walk(stub):
        for f in files:
            if not f.endswith('.inc'):
                continue
            rel = os.path.relpath(os.path.join(root, f), stub)
            g = os.path.join(gen, rel)
            s = os.path.join(copy, rel)
            if not os.path.exists(g) or not os.path.exists(s):
                continue
            body = open(g, encoding='latin-1').read()
            if '"' in body or not FLOAT.search(body):
                continue
            zeroed = NUMBER.sub(lambda m: '0.0F' if re.search(r'[.pPeE]|\d[fF]$', m.group(0)) and not m.group(0).lower().startswith(('0x', '-0x')) or re.search(r'[pP]', m.group(0)) else '0', body)
            open(s + '.new', 'w', encoding='latin-1').write(zeroed)
            os.replace(s + '.new', s)
            n += 1
    print(f'stub_floats.py: {n} float bodies zeroed in their own shape')


if __name__ == '__main__':
    main()
