#!/usr/bin/env python3
"""The second half of lowering the decomp's 32-bit game pointers to integers (SonicAdventureRe-nx).

After refs.py (with the domain's base above 0) and Gep32 (getelementptr on a reference as integer
arithmetic), a reference, `ptr addrspace(271)`, is only ever loaded, stored, compared, moved
through a phi or a select, or cast to and from an integer. Each of those is the same operation on
an i32, so this pass retypes the address space as i32 throughout the module's text:

  - the type `ptr addrspace(271)` becomes `i32` (struct fields, globals, loads, stores, phis);
  - a cast between a reference and a number becomes the number itself (`add i32 %v, 0`), or a
    zext or a trunc where the number is 64 bits;
  - in a constant, `inttoptr (i32 E to ptr addrspace(271))` becomes E, and null becomes 0.

The x86 data layout already gives a reference 4 bytes, so no layout moves. With the address space
gone, the module no longer depends on x86's 32-bit pointers, and --retarget TRIPLE sets the triple
and data layout of another 64-bit target (AArch64 for the Switch).

  ref32_int.py FILE.ll [--retarget aarch64-unknown-linux-gnu|aarch64-none-elf]

It stops, naming the line, at a reference in any form it has no rule for.
"""
import re
import sys

REF = 'ptr addrspace(271)'
LAYOUTS = {
    'aarch64': 'e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128',
}

CAST_LINE = re.compile(r'^(?P<head>\s*%[\w.]+ = )(?P<op>ptrtoint|inttoptr) (?P<from>i\d+) (?P<value>%[\w.]+|-?\d+) '
                       r'to (?P<to>i\d+)(?P<rest>,.*)?$')


def closing(text, start):
    """the index past the parenthesis that closes the one opened just before start"""
    depth = 1
    for i in range(start, len(text)):
        c = text[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError('unbalanced: ' + text[:200])


def strip_casts(line):
    """`inttoptr (i32 E to i32)` and `ptrtoint (i32 E to i32)`, as constants, become E"""
    for op in ('inttoptr (i32 ', 'ptrtoint (i32 '):
        while True:
            at = line.find(op)
            if at < 0:
                break
            end = closing(line, at + len(op.split('(')[0]) + 1)
            inner = line[at + len(op):end - 1]
            if not inner.endswith(' to i32'):
                # a number widened to the host's width: leave it for the backend
                if inner.endswith(' to i64') and op.startswith('ptrtoint'):
                    raise SystemExit(f'ref32_int: a constant reference widened to 64 bits: {line.strip()[:160]}')
                break
            expr = inner[:-len(' to i32')]
            line = line[:at] + expr + line[end:]
    return line


def lower_line(line):
    if REF not in line and 'addrspace(271)' not in line:
        return line
    if 'addrspace(271)' in line.replace(REF, ''):
        raise SystemExit(f'ref32_int: a call or intrinsic through a reference: {line.strip()[:160]}')
    line = line.replace(REF, 'i32')
    line = strip_casts(line)
    m = CAST_LINE.match(line)
    if m:
        src, dst = m['from'], m['to']
        rest = m['rest'] or ''
        if src == dst:
            return f"{m['head']}add {src} {m['value']}, 0{rest}"
        if src == 'i32' and dst == 'i64':
            return f"{m['head']}zext i32 {m['value']} to i64{rest}"
        if src == 'i64' and dst == 'i32':
            return f"{m['head']}trunc i64 {m['value']} to i32{rest}"
        raise SystemExit(f'ref32_int: a cast it has no rule for: {line.strip()[:160]}')
    # a null reference is the number 0
    line = re.sub(r'\bnull\b', '0', line)
    return line


def main():
    path = sys.argv[1]
    retarget = None
    if len(sys.argv) > 3 and sys.argv[2] == '--retarget':
        retarget = sys.argv[3]
    lines = open(path, encoding='utf-8').read().split('\n')
    out = []
    for line in lines:
        if retarget and line.startswith('target triple = '):
            line = f'target triple = "{retarget}"'
        elif retarget and line.startswith('target datalayout = '):
            line = f'target datalayout = "{LAYOUTS[retarget.split("-")[0]]}"'
        else:
            line = lower_line(line)
        out.append(line)
    text = '\n'.join(out)
    if 'addrspace(271)' in text:
        raise SystemExit('ref32_int: a reference was left')
    open(path, 'w', encoding='utf-8').write(text)


if __name__ == '__main__':
    main()
