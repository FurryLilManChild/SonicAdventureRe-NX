#!/usr/bin/env python3
"""SonicAdventureRe-nx: strip.py for an update, from the last strip's results.

strip.py needs a second, stub build of the whole game (switch/tools/stub_build.sh) and scans the
whole image several times. An update rarely changes the game's disc data, only where it lies, so
this starts from the last strip instead:

  1. each piece of the last fill table is placed by the symbol (or content section) it lay in,
     at the same offset in that symbol in the new build, and taken only where the new image holds
     exactly the disc's bytes there (checked against the disc file, so a piece is never wrong);
  2. each data symbol that is new, or whose bytes changed (pointers aside), and each content
     section that changed, is matched against the disc as strip.py does where it has no stub
     twin: every byte not zero is a candidate, found in runs of 16 or as pieces of 4 or more in
     any disc file. Taking a byte that only happens to match the disc is harmless (the fill puts
     it back); missing one is what the audit (switch/tools/audit.py) catches afterwards.

  strip_update.py LAST.full.elf LAST.img LAST.fill NEW.img NEW.full.elf NEW.map DECOMP OUT.img OUT.fill

The outputs are strip.py's, checked the same way (the fill must give the image back).
"""
import bisect
import hashlib
import os
import struct
import sys
import time
import zlib

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, '..', '..', 'tools', 'english'))
sys.path.insert(0, here)
from common import prs_decompress  # noqa: E402
from strip import read_image, write_image, map_ranges, K  # noqa: E402

T0 = time.time()


def say(text):
    print(f'strip_update.py [{time.time() - T0:5.0f}s]: {text}', file=sys.stderr, flush=True)


def elf_places(path):
    """the symbols with a size (objects and functions) and the allocated sections:
    places: sorted [(address, size, key)], key ('S', section) for a content section (sa1_data_*)
    or a section with no symbol over it, ('Y', name, size, n) for the n-th symbol of that name and
    size by address; data: key -> bytes of each data object symbol (not in a content section);
    secdata: content section name -> bytes"""
    from elftools.elf.elffile import ELFFile
    places, data, secdata = [], {}, {}
    with open(path, 'rb') as fh:
        e = ELFFile(fh)
        secs = list(e.iter_sections())
        cache = {}
        seen = {}
        syms = []
        for s in e.get_section_by_name('.symtab').iter_symbols():
            t = s['st_info']['type']
            if t not in ('STT_OBJECT', 'STT_FUNC') or not s['st_size'] or not isinstance(s['st_shndx'], int):
                continue
            sec = secs[s['st_shndx']]
            if not sec['sh_flags'] & 2 or sec.name.startswith('sa1_data_'):
                continue
            syms.append((s['st_value'], s['st_size'], s.name, t, s['st_shndx']))
        syms.sort()
        for addr, size, name, t, shndx in syms:
            n = seen.get((name, size), 0)
            seen[(name, size)] = n + 1
            key = ('Y', name, size, n)
            places.append((addr, size, key))
            sec = secs[shndx]
            if t == 'STT_OBJECT':
                if sec['sh_type'] == 'SHT_NOBITS':
                    data[key] = bytes(size)
                else:
                    if sec.name not in cache:
                        cache[sec.name] = sec.data()
                    o = addr - sec['sh_addr']
                    data[key] = cache[sec.name][o:o + size]
        for sec in secs:
            if not sec['sh_flags'] & 2 or not sec['sh_size'] or not sec['sh_addr']:
                continue
            places.append((sec['sh_addr'], sec['sh_size'], ('S', sec.name)))
            if sec.name.startswith('sa1_data_'):
                secdata[sec.name] = sec.data() if sec['sh_type'] != 'SHT_NOBITS' else bytes(sec['sh_size'])
    # symbols before sections at the same address, so the narrowest place is found first
    places.sort(key=lambda p: (p[0], p[2][0] == 'S'))
    return places, data, secdata


class Places:
    """finds the place holding an address: a content section, else the symbol, else the section"""

    def __init__(self, places):
        self.kinds = {}
        for a, size, key in places:
            kind = 'C' if key[0] == 'S' and key[1].startswith('sa1_data_') else key[0]
            self.kinds.setdefault(kind, []).append((a, size, key))
        for v in self.kinds.values():
            v.sort()
        self.starts = {k: [p[0] for p in v] for k, v in self.kinds.items()}

    def find(self, addr):
        for kind in ('C', 'Y', 'S'):
            v = self.kinds.get(kind, [])
            k = bisect.bisect_right(self.starts.get(kind, []), addr) - 1
            # symbols can nest (a few overlap): look back a little
            for j in range(k, max(k - 8, -1), -1):
                a, size, key = v[j]
                if a <= addr < a + size:
                    return key, addr - a
        return None, 0


def main():
    last_elf, last_img, last_fill, new_img, new_elf, new_map, decomp, out_img, out_fill = map(os.path.abspath, sys.argv[1:10])
    sys.path.insert(0, os.path.join(decomp, 'tools'))
    cwd = os.getcwd()
    os.chdir(decomp)
    import images
    files = images.files()
    os.chdir(cwd)
    name_of = {p: n for n, p in files.items()}

    h, segs, body, rels, _, _ = read_image(new_img)
    start = h[2]
    rel_words = set()
    for r in rels:
        rel_words.update(range(r, r + 8))
    dom_lo, dom_hi = h[2] - h[1], h[3] - h[1]

    # the sources, in the last fill's order (then any more this needs)
    sources, index = [], {}

    def source(name):
        if name in index:
            return index[name]
        path = files.get(name)
        if not path:
            return None
        if path.upper().endswith('.PRS'):
            dec = os.path.join(decomp, 'build/gen/img', os.path.basename(path)[:-4] + '.BIN')
            data = open(dec, 'rb').read() if os.path.exists(dec) else \
                prs_decompress(open(os.path.join(decomp, 'orig', path), 'rb').read())
            prs = 1
        else:
            data = open(os.path.join(decomp, 'orig', path), 'rb').read()
            prs = 0
        index[name] = len(sources)
        sources.append((path, data, prs))
        return index[name]

    f = open(last_fill, 'rb').read()
    magic, nsrc, npc, _, _ = struct.unpack_from('<8sIIII', f, 0)
    assert magic == b'SARNXFL1', last_fill
    pos = 24
    last_src = []
    for _ in range(nsrc):
        path, size, sha, prs, _ = struct.unpack_from('<32sI20sII', f, pos)
        pos += 64
        path = path.rstrip(b'\0').decode()
        c = source(name_of[path])
        assert hashlib.sha1(sources[c][1]).digest() == sha, f'{path} is not the disc file the last fill read'
        last_src.append(c)
    last_pieces = [struct.unpack_from('<IIII', f, pos + 16 * i) for i in range(npc)]
    say(f'the last fill: {nsrc} disc files, {npc} pieces')

    old_places, old_data, old_secdata = elf_places(last_elf)
    say(f'the last build: {len(old_places)} places')
    new_places, new_data, new_secdata = elf_places(new_elf)
    say(f'this build: {len(new_places)} places')
    old_find = Places(old_places)
    new_at = {}
    for a, size, key in new_places:
        new_at.setdefault(key, (a, size))
    # the last image's start: the last fill's pieces are offsets from it
    old_start = struct.unpack_from('<8sQQ', open(last_img, 'rb').read(24), 0)[2]

    covered = bytearray(len(body))
    pieces = []

    def take(at, src, s_at, n):
        pieces.append((at, src, s_at, n))
        covered[at:at + n] = b'\x01' * n

    # 1. the last pieces, moved with their symbols, checked against the disc
    moved = dropped = moved_bytes = 0
    for at, src, s_at, n in last_pieces:
        key, off = old_find.find(old_start + at)
        if key is None or key not in new_at:
            dropped += 1
            continue
        a, size = new_at[key]
        if off + n > size:
            dropped += 1
            continue
        na = a - start + off
        c = last_src[src]
        want = sources[c][1][s_at:s_at + n]
        if na < 0 or na + n > len(body) or bytes(body[na:na + n]) != want or covered[na] or \
                any(na + q in rel_words for q in (0, n - 1)):
            dropped += 1
            continue
        take(na, c, s_at, n)
        moved += 1
        moved_bytes += n
    say(f'{moved} pieces ({moved_bytes} bytes) in place again, {dropped} not')

    ranges = map_ranges(new_map)
    rstarts = [r[0] for r in ranges]

    def program_at(off):
        a = start + off
        k = bisect.bisect_right(rstarts, a) - 1
        if k >= 0 and ranges[k][0] <= a < ranges[k][0] + ranges[k][1]:
            return ranges[k][2]
        return None

    def masked(b):
        """b with the words that may be pointers (into either build) zeroed"""
        m = bytearray(b)
        for w in range(0, len(m) - 3, 4):
            v = struct.unpack_from('<I', m, w)[0]
            if dom_lo - (1 << 24) <= v < dom_hi + (1 << 24):
                m[w:w + 4] = bytes(4)
        for w in range(0, len(m) - 7, 8):
            v = struct.unpack_from('<Q', m, w)[0]
            if h[1] <= v < h[1] + (h[3] - h[2]) + (1 << 24):
                m[w:w + 8] = bytes(8)
        return bytes(m)

    blob = {}

    def find_anywhere(piece):
        if not blob:
            for name in sorted(files):
                source(name)
            blob['data'] = b''.join(d for _, d, _ in sources)
            at, blob['starts'] = 0, []
            for _, d, _ in sources:
                blob['starts'].append(at)
                at += len(d)
        data, st = blob['data'], blob['starts']
        j = data.find(piece)
        while j >= 0:
            c = bisect.bisect_right(st, j) - 1
            if j + len(piece) <= st[c] + len(sources[c][1]):
                return c, j - st[c]
            j = data.find(piece, j + 1)
        return None, None

    def match_free(at0, size, cands):
        """strip.py's matching of a region with no stub twin: the bytes not zero, not pointers and
        not yet taken, in runs of K from the candidates' files, then pieces of 4 or more anywhere"""
        mine = bytes(body[at0:at0 + size])
        want = [k for k in range(size) if mine[k] and not covered[at0 + k] and at0 + k not in rel_words]
        if not want:
            return 0
        ptr = set()
        for k in want:
            w = (at0 + k) & ~3
            if w >= at0 and w + 4 <= at0 + size and dom_lo <= struct.unpack_from('<I', body, w)[0] < dom_hi:
                ptr.add(k)
        want = [k for k in want if k not in ptr]
        if not want:
            return 0
        got = 0
        for c in cands:
            d = sources[c][1]
            i = 0
            while i + K <= size:
                if covered[at0 + i] or mine[i:i + K].count(0) > K - 4:
                    i += 4
                    continue
                j = d.find(mine[i:i + K])
                if j < 0:
                    i += 4
                    continue
                n = K
                while i + n < size and j + n < len(d) and mine[i + n] == d[j + n]:
                    n += 1
                k = 0
                while k < n:
                    if covered[at0 + i + k] or at0 + i + k in rel_words:
                        k += 1
                        continue
                    e = k
                    while e < n and not covered[at0 + i + e] and at0 + i + e not in rel_words:
                        e += 1
                    take(at0 + i + k, c, j + k, e - k)
                    got += e - k
                    k = e
                i += n
        k = 0
        while k < len(want):
            if covered[at0 + want[k]]:
                k += 1
                continue
            m = k
            while m + 1 < len(want) and want[m + 1] == want[m] + 1 and not covered[at0 + want[m + 1]]:
                m += 1
            lo, hi = want[k], want[m] + 1
            if hi - lo >= 4:
                piece = mine[lo:hi]
                c, j = None, -1
                for cc in cands:
                    j = sources[cc][1].find(piece)
                    if j >= 0:
                        c = cc
                        break
                if c is None:
                    c, j = find_anywhere(piece)
                if c is not None:
                    take(at0 + lo, c, j, hi - lo)
                    got += hi - lo
            k = m + 1
        return got

    # 2. what changed: content sections and data symbols that are new or differ, pointers aside
    changed_secs = changed_syms = new_bytes = 0
    for name, b in sorted(new_secdata.items()):
        if name in old_secdata and masked(old_secdata[name]) == masked(b):
            continue
        changed_secs += 1
        a, size = new_at[('S', name)]
        c = source(name[len('sa1_data_'):])
        new_bytes += match_free(a - start, size, [c] if c is not None else [])
    for key, b in new_data.items():
        old = old_data.get(key)
        if old is not None and len(old) == len(b) and masked(old) == masked(b):
            continue
        a, size = new_at[key]
        at0 = a - start
        if at0 < 0 or at0 + size > len(body):
            continue
        prog = program_at(at0) or '1st_read'
        if prog == 'port':
            continue
        changed_syms += 1
        cands = [c for c in (source(prog), source('1st_read')) if c is not None]
        new_bytes += match_free(at0, size, cands)
    say(f'{changed_secs} content sections and {changed_syms} data symbols new or changed: '
        f'{new_bytes} more bytes from the disc')

    # the stripped image, checked as strip.py checks it
    stripped = bytearray(body)
    for at, src, s_at, n in pieces:
        stripped[at:at + n] = bytes(n)
    check = bytearray(stripped)
    for at, src, s_at, n in pieces:
        check[at:at + n] = sources[src][1][s_at:s_at + n]
    assert check == body, 'the fill table does not give the image back'

    srcs = bytearray()
    for path, data, prs in sources:
        srcs += struct.pack('<32sI20sII', path.encode(), len(data), hashlib.sha1(data).digest(), prs, 0)
    pcs = bytearray()
    for at, src, s_at, n in sorted(pieces):
        pcs += struct.pack('<IIII', at, src, s_at, n)
    crc = zlib.crc32(bytes(srcs) + bytes(pcs))
    fill = struct.pack('<8sIIII', b'SARNXFL1', len(sources), len(pieces), crc, 0) + srcs + pcs
    open(out_fill, 'wb').write(fill)
    h2 = list(h)
    h2[7] = 1
    h2[8] = crc
    write_image(out_img, h2, segs, stripped, rels)
    say(f'{len(sources)} disc files, {len(pieces)} pieces, {sum(p[3] for p in pieces)} bytes taken '
        f'from the disc; fill table {len(fill)} bytes, crc {crc:08x}')


if __name__ == '__main__':
    main()
