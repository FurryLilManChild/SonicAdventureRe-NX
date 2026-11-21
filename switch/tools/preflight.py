#!/usr/bin/env python3
"""SonicAdventureRe-nx: the sweep a Switch build must PASS before the owner gets it.

  preflight.py BUILD_DIR    (switch/build: SonicAdventureRe-nx.full.elf, .img, .elf, loader.elf)

Checks, each named in the output with PASS or FAIL:
  1. code holds no fixed address in the link window: no absolute relocation in code (mkimage
     stops on those), and every MOV/MOVK of a 64-bit window-5 number is a packed switch table
     (shifted) or two 32-bit fields stored at once (cargo_top_init: frame 0.0 and unk_08 5), never
     a register used as an address;
  2. no module's lowered IR holds a number near the domain's marker (0x5a5a00000000,
     tools/a64/env-nx.sh): a domain address the lowering missed would be one. (The image's data
     can't tell: a table of 32-bit fields can hold 0x5a5a beside a 0. Words that read as window-5
     addresses without a relocation are counted for information: a value, then a 5.)
  3. the loader maps every range of the domain the decomp's runtime maps, at the same offsets and
     sizes (runner.c, threads.c, shim.c);
  4. the image fits the heap the loader keeps for it;
  5. the game's ELF keeps its symbol table and every static records.c finds by name;
  6. the loader and the image are the build of COMMIT (preflight.py BUILD_DIR COMMIT).
"""
import bisect
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WINDOW = 1 << 32
results = []


def check(name, ok, detail=''):
    results.append(ok)
    print(f'{"PASS" if ok else "FAIL"}  {name}' + (f': {detail}' if detail else ''))


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def image(path):
    b = path.read_bytes()
    F = '<8sQQQQIIIIQ'
    hs = struct.calcsize(F)
    magic, link, start, end, entry, nseg, nrel, _, _, sym = struct.unpack_from(F, b, 0)
    segs = [struct.unpack_from('<QQQII', b, hs + i * 32) for i in range(nseg)]
    p = hs + nseg * 32
    datas = []
    for off, fs, ms, fl, _ in segs:
        datas.append((off, fl, b[p:p + fs]))
        p += (fs + 7) & ~7
    rel = set(struct.unpack_from(f'<{nrel}I', b, p))
    return link, start, end, datas, rel


def main():
    build = Path(sys.argv[1])
    full = build / 'SonicAdventureRe-nx.full.elf'
    link, start, end, datas, rel = image(build / 'SonicAdventureRe-nx.img')
    lo, hi = link, link + WINDOW

    # 1. code
    dis = run('aarch64-linux-gnu-objdump', '-d', '--no-show-raw-insn', str(full)).splitlines()
    window_hi = link >> 32
    bad, tables, stores, wide, library = [], 0, 0, 0, 0
    # NVK's code (libvulkan.a), from the link's map: its constants were compiled before our link
    # chose window 5, so one with no relocation on it is NVK's own number, never a window address
    nvk = []
    lines = (build / 'SonicAdventureRe-nx.map').read_text(errors='replace').splitlines()
    for i, l in enumerate(lines):
        m = re.match(r'\s*(?:\.text\S*\s+)?0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\S*libvulkan\.a\(', l)
        if m and (l.lstrip().startswith('.text') or lines[i - 1].lstrip().startswith('.text')):
            a = int(m.group(1), 16)
            nvk.append((a, a + int(m.group(2), 16)))
    nvk.sort()
    starts = [a for a, _ in nvk]
    relocated = set()
    for l in run('aarch64-linux-gnu-readelf', '-rW', str(full)).splitlines():
        m = re.match(r'\s*([0-9a-f]{8,16})\s+[0-9a-f]+\s+R_AARCH64_MOVW', l)
        if m:
            relocated.add(int(m.group(1), 16))

    def in_nvk(addr):
        k = bisect.bisect_right(starts, addr) - 1
        return k >= 0 and nvk[k][0] <= addr < nvk[k][1]

    for k, l in enumerate(dis):
        m = (re.search(rf'\bmovk\s+(x\d+), #0x{window_hi:x}, lsl #32\b', l) or
             re.search(rf'\bmov\s+(x\d+), #0x{window_hi:x}[0-9a-f]{{8}}\b', l))
        if not m:
            continue
        reg = m.group(1)
        addr = int(l.split(':')[0], 16)
        if in_nvk(addr) and addr not in relocated:
            library += 1
            continue
        after = dis[k + 1:k + 12]
        # a constant whose top 16 bits are set next (movk xD, #N, lsl #48, N nonzero) is no address:
        # the address space is 36 bits (NVK's GPU method words are such constants)
        if any(re.search(rf'\bmovk\s+{reg}, #0x[1-9a-f][0-9a-f]*, lsl #48\b', a) for a in after[:4]):
            wide += 1
            continue
        # a packed table is shifted by a register soon after: lsr/lsl/asr xD, <reg>, xS
        if any(re.search(rf'\b(lsr|lsl|asr)\s+[xw]\d+, {reg}, [xw]\d+', a) for a in after[:5]):
            tables += 1
        # two 32-bit fields stored at once (a value, then a 5): the register is only stored
        elif any(re.search(rf'\b(str|stur|stp)\s+[^,]*\b{reg}\b', a) for a in after) and \
                not any(re.search(rf'\b(ldr|ldur|ldrb|ldrh|str|stur|strb|strh)\s+[xwbhsdq]\d+, \[{reg}[],]', a) for a in after):
            stores += 1
        else:
            bad.append(l.strip())
    check('1. no fixed window address in code', not bad,
          f'{len(bad)} instructions, first: {bad[0]}' if bad else
          f'no absolute relocation in code (mkimage); window-5 numbers in code: {tables} packed switch tables '
          f'(shifted), {stores} merged stores of two 32-bit fields and {wide} 64-bit constants with their top bits '
          f'set, not addresses (none used as one); {library} of NVK\'s own constants (no relocation, in '
          f'{len(nvk)} sections of libvulkan.a)')

    # 2. the marker, where it would show: the lowered IR of every module (the image's data can't
    #    tell, since a table of 32-bit fields can hold 0x5a5a beside a 0)
    marker = 0x5a5a00000000
    lls = list((ROOT / 'decomp/build/engine/native/obj').rglob('*.ll'))
    number = re.compile(rb'(?<![\w.])(-?\d{12,20})(?![\w.])')
    left = []
    for ll in lls:
        for m in number.finditer(ll.read_bytes()):
            v = int(m.group(1)) & ((1 << 64) - 1)
            if marker <= v < marker + WINDOW:
                left.append(f'{ll.relative_to(ROOT)}: {m.group(1).decode()}')
    pairs = 0
    for off, fl, d in datas:
        if fl & 1:
            continue
        for i in range(0, len(d) - 7, 8):
            v, = struct.unpack_from('<Q', d, i)
            if lo <= v < hi and (off + i) not in rel:
                pairs += 1
    check('2. no domain address the lowering missed (the marker, in every module\'s lowered IR)', bool(lls) and not left,
          f'{len(left)} left, first: {left[0]}' if left else
          f'none in {len(lls)} modules; {len(rel)} addresses on the move list; {pairs} data words read as window-5 numbers without a relocation (32-bit field pairs)')

    # 3. the domain's ranges
    loader = (ROOT / 'switch/loader/main.c').read_text()
    have = {(int(a, 16), int(b, 16)) for a, b in re.findall(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+)\}', loader)}
    runner = (ROOT / 'decomp/engine/native/runner.c').read_text()
    threads = (ROOT / 'decomp/engine/native/threads.c').read_text()
    shim = (ROOT / 'decomp/engine/native/shim.c').read_text()
    want = {(int(a, 16), int(b, 16)) for a, b in re.findall(r'\{(0x[0-9a-f]+), (0x[0-9a-f]+)\}',
                                                            runner.split('devices[][2] = {')[1].split('};')[0])}
    want |= {(int(x, 16), 0x01000000) for x in re.findall(r'(0x[0-9a-f]+)', runner.split('ram_mirrors[] = {')[1].split('}')[0])}
    stack = int(re.search(r'#define GAME_STACK (0x[0-9a-fA-F]+)', runner).group(1), 16)
    stack_size = int(re.search(r'#define GAME_STACK_SIZE (0x[0-9a-fA-F]+)', runner).group(1), 16)
    want.add((stack, stack_size))
    p2 = int(re.search(r'#define P2_CALL (0x[0-9a-fA-F]+)', runner).group(1), 16)
    want.add((p2, int(re.search(r'#define P2_CALL_SIZE (\d+)', runner).group(1))))
    stacks = int(re.search(r'#define STACKS \(NATIVE_DOMAIN \+ (0x[0-9a-fA-F]+)UL\)', threads).group(1), 16)
    ssize = int(re.search(r'#define STACK_SIZE (0x[0-9a-fA-F]+)UL', threads).group(1), 16)
    gsize = int(re.search(r'#define GUARD_SIZE (0x[0-9a-fA-F]+)UL', threads).group(1), 16)
    contexts = int(re.search(r'#define CONTEXTS (\d+)', threads).group(1))
    want.add((stacks, (contexts + 1) * (ssize + gsize)))
    heap = int(re.search(r'#define HEAP \(NATIVE_DOMAIN \+ (0x[0-9a-fA-F]+)UL\)', shim).group(1), 16)
    hsize = int(re.search(r'#define HEAP_SIZE (0x[0-9a-fA-F]+)UL', shim).group(1), 16)
    want.add((heap, hsize))
    lacking = sorted(want - have)
    check('3. the loader keeps every range the runtime maps clear', not lacking,
          f'{len(want)} ranges' if not lacking else 'missing ' + ', '.join(f'{a:#x}+{b:#x}' for a, b in lacking))

    # 4. size
    carve = int(re.search(r'#define CARVE \((\d+)ULL << 20\)', loader).group(1)) << 20
    size = end - start
    check('4. the image fits the heap the loader keeps', size <= carve, f'{size >> 20} MB of {carve >> 20} MB')

    # 5. symbols
    syms = set(l.split()[-1] for l in run('aarch64-linux-gnu-nm', str(build / 'SonicAdventureRe-nx.elf')).splitlines()
               if len(l.split()) >= 2)
    records = (ROOT / 'decomp/engine/sa1/records.c').read_text()
    names = re.findall(r'\[[A-Z_]+\] = "(\w+)"', records)
    gone = [n for n in names if n not in syms]
    check('5. the symbol table and the statics found by name', bool(syms) and not gone,
          f'{len(syms)} symbols, {len(names)} statics' if not gone else 'missing ' + ', '.join(gone))

    # 6. one build
    commit = sys.argv[2].encode()
    in_loader = commit in (build / 'loader.elf').read_bytes()
    in_image = commit in (build / 'SonicAdventureRe-nx.img').read_bytes()
    check('6. the loader and the image are the build of ' + sys.argv[2], in_loader and in_image,
          f'loader {"has" if in_loader else "LACKS"} it, image {"has" if in_image else "LACKS"} it')

    print('\npreflight:', 'PASS' if all(results) else 'FAIL')
    return 0 if all(results) else 1


if __name__ == '__main__':
    sys.exit(main())
