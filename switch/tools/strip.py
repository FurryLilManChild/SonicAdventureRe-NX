#!/usr/bin/env python3
"""SonicAdventureRe-nx: the game's image without the disc's data, and how to put it back.

The image switch/build.sh makes holds data from the Japanese disc in two ways:

  - the content rows (sections sa1_data_NAME, from build/gen/NAME/content.c): each image's data
    as C, the same bytes as the disc's file but for the pointers, which the link points at the
    port's own rows;
  - the bodies the game's C includes (build/gen/**/*.inc: texts, values, scripts' words, tables),
    which switch/tools/stub_build.sh's build replaces with the decomp's zero bodies (build/stub/),
    of the same sizes.

This writes:

  OUT.img   the image with every byte taken from the disc zeroed, its header's flags 1 (to be
            filled) and its pad the fill table's CRC-32
  OUT.fill  where each zeroed byte comes from on the player's disc:
              header  "SARNXFL1", u32 sources, u32 pieces, u32 build crc, u32 0
              source  char path[32] (as the disc names it, under disc/), u32 size (decoded, for a
                      .PRS), u8 sha1[20] (of the decoded bytes), u32 prs (1: decode it), u32 0
              piece   u32 image offset (from the image's start), u32 source, u32 source offset,
                      u32 length

  strip.py REAL.img REAL.elf REAL.map STUB.elf DECOMP OUT.img OUT.fill

Each byte the two images differ in must come from a source (or the strip stops); a content byte
is taken from its image's file where it equals it there, found by runs of 16 bytes and then
filled out between runs that line up (pointers differ, and stay). It reports what of the content
sections stays in the image: the pointers, which are the port's, not the disc's.
"""
import hashlib
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'english'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import prs_decompress  # noqa: E402

HEADER = '<8sQQQQIIIIQ'
SEGMENT = '<QQQII'
K = 16


def read_image(path):
    d = open(path, 'rb').read()
    h = list(struct.unpack_from(HEADER, d, 0))
    magic, link_base, start, end, entry, nseg, nrel, flags, pad, symbol = h
    assert magic == b'SAREIMG1', path
    pos = struct.calcsize(HEADER)
    segs = []
    for i in range(nseg):
        segs.append(struct.unpack_from(SEGMENT, d, pos))
        pos += struct.calcsize(SEGMENT)
    body = bytearray(end - start)
    file_at = []
    for off, filesz, memsz, fl, _ in segs:
        file_at.append(pos)
        body[off:off + filesz] = d[pos:pos + filesz]
        pos += filesz + (8 - filesz % 8) % 8
    rels = struct.unpack_from(f'<{nrel}I', d, pos)
    return h, segs, body, rels, d, file_at


def write_image(path, h, segs, body, rels):
    out = bytearray(struct.pack(HEADER, *h))
    for s in segs:
        out += struct.pack(SEGMENT, *s)
    for off, filesz, memsz, fl, _ in segs:
        out += body[off:off + filesz]
        out += b'\0' * ((8 - filesz % 8) % 8)
    out += struct.pack(f'<{len(rels)}I', *rels)
    open(path, 'wb').write(out)


def elf_sections(path):
    d = open(path, 'rb').read()
    shoff, = struct.unpack_from('<Q', d, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 0x3a)
    secs = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * shentsize) for i in range(shnum)]
    names = secs[shstrndx]
    out = {}
    for s in secs:
        o = names[4] + s[0]
        out[d[o:d.index(b'\0', o)].decode()] = (s[3], s[5], s[2])  # address, size, flags
    return out


def map_ranges(path):
    """(address, size, program name) of each input section the map places from a program-NAME.o,
    and from the game's own objects (1st_read)"""
    out = []
    pending = None
    for line in open(path, errors='replace'):
        m = re.match(r'^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$', line)
        if not m:
            m2 = re.match(r'^ (\S+)$', line)
            pending = m2.group(1) if m2 else None
            m = re.match(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$', line) if pending else None
            if not m:
                continue
            addr, size, obj = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
        else:
            addr, size, obj = int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        if not size or not addr:
            continue
        p = re.search(r'program-([a-z0-9_]+)\.o$', obj)
        if p:
            out.append((addr, size, p.group(1)))
        elif '/decomp/build/engine/native/obj/' in obj or '/sare-stub/build/engine/native/obj/' in obj:
            out.append((addr, size, '1st_read'))
    out.sort()
    return out


def data_symbols(path):
    """(name, size) -> [(address, section, bytes)] of each data symbol in an allocated section"""
    from elftools.elf.elffile import ELFFile
    out = {}
    with open(path, 'rb') as fh:
        e = ELFFile(fh)
        secs = list(e.iter_sections())
        cache = {}
        for s in e.get_section_by_name('.symtab').iter_symbols():
            if s['st_info']['type'] != 'STT_OBJECT' or not s['st_size'] or not isinstance(s['st_shndx'], int):
                continue
            sec = secs[s['st_shndx']]
            if not sec['sh_flags'] & 2:
                continue
            if sec.name.startswith('sa1_data_'):
                data = b''
            elif sec['sh_type'] == 'SHT_NOBITS':
                data = bytes(s['st_size'])
            else:
                if sec.name not in cache:
                    cache[sec.name] = sec.data()
                o = s['st_value'] - sec['sh_addr']
                data = cache[sec.name][o:o + s['st_size']]
            out.setdefault((s.name, s['st_size']), []).append((s['st_value'], sec.name, data))
    return out


def main():
    real_img, real_elf, real_map, stub_elf, decomp, out_img, out_fill = sys.argv[1:8]
    real_img, real_elf, real_map, stub_elf, decomp, out_img, out_fill = map(
        os.path.abspath, (real_img, real_elf, real_map, stub_elf, decomp, out_img, out_fill))
    sys.path.insert(0, os.path.join(decomp, 'tools'))
    here = os.getcwd()
    os.chdir(decomp)  # images.py reads config/ from the decomp's root
    import images
    files = images.files()
    os.chdir(here)

    h, segs, body, rels, _, _ = read_image(real_img)
    start = h[2]
    rel_words = set()
    for r in rels:
        rel_words.update(range(r, r + 8))

    # the sources: each image's file on the disc, decoded
    sources, index = [], {}

    def source(name):
        if name in index:
            return index[name]
        path = files.get(name)
        if not path:
            return None
        if path.upper().endswith('.PRS'):
            dec = os.path.join(decomp, 'build/gen/img', os.path.basename(path)[:-4] + '.BIN')
            if os.path.exists(dec):
                data = open(dec, 'rb').read()
            else:
                data = prs_decompress(open(os.path.join(decomp, 'orig', path), 'rb').read())
            prs = 1
        else:
            data = open(os.path.join(decomp, 'orig', path), 'rb').read()
            prs = 0
        index[name] = len(sources)
        sources.append((path, data, prs))
        return index[name]

    covered = bytearray(len(body))  # 1 where a piece fills the byte
    pieces = []

    def take(at, src, s_at, n):
        pieces.append((at, src, s_at, n))
        covered[at:at + n] = b'\x01' * n

    def match_region(at0, sec, f, src, near=0):
        """take each byte of sec (at image offset at0) found in f: runs of K bytes, then the bytes
        between two runs that line up; never a 64-bit address the loader moves. Returns the bytes
        taken and the last source position (a hint for the next region)"""
        size = len(sec)
        taken = 0
        i, pos = 0, near
        runs = []  # (region offset, source offset, length)
        kk = K if size >= K else size
        while i + kk <= size:
            chunk = sec[i:i + kk]
            if kk == K and chunk.count(0) > K - 4:
                i += 4
                continue
            j = f.find(chunk, pos, pos + (1 << 16))
            if j < 0:
                j = f.find(chunk)
            if j < 0:
                i += 4
                continue
            n = kk
            while i + n < size and j + n < len(f) and sec[i + n] == f[j + n]:
                n += 1
            b = 0
            while i - b > 0 and j - b > 0 and sec[i - b - 1] == f[j - b - 1] and \
                    (not runs or i - b - 1 >= runs[-1][0] + runs[-1][2]):
                b += 1
            runs.append((i - b, j - b, n + b))
            pos = j + n
            i += n
        filled = []
        for k, (i, j, n) in enumerate(runs):
            filled.append((i, j, n))
            if k + 1 < len(runs):
                i2, j2, _ = runs[k + 1]
                if i2 - i == j2 - j:
                    g = i + n
                    while g < i2:
                        if sec[g] == f[j + (g - i)]:
                            e = g
                            while e < i2 and sec[e] == f[j + (e - i)]:
                                e += 1
                            filled.append((g, j + (g - i), e - g))
                            g = e
                        else:
                            g += 1
        for i, j, n in filled:
            k = 0
            while k < n:
                if at0 + i + k in rel_words:
                    k += 1
                    continue
                e = k
                while e < n and at0 + i + e not in rel_words:
                    e += 1
                take(at0 + i + k, src, j + k, e - k)
                taken += e - k
                k = e
        return taken, pos

    # 1. the content sections, against their images' files
    secs = elf_sections(real_elf)
    content_total = content_taken = 0
    print('strip.py: the content sections', file=sys.stderr, flush=True)
    for name, (addr, size, flags) in sorted(secs.items()):
        if not name.startswith('sa1_data_') or not size:
            continue
        src = source(name[len('sa1_data_'):])
        if src is None:
            print(f'strip.py: no disc file for {name}', file=sys.stderr)
            sys.exit(1)
        at0 = addr - start
        content_total += size
        content_taken += match_region(at0, bytes(body[at0:at0 + size]), sources[src][1], src)[0]

    # 2. the bodies: each data symbol outside the content sections, against the same symbol in the
    # stub build (the decomp's zero bodies): the bytes they differ in came from the disc, and must
    # be found in the symbol's program's file (or 1st_read, or any source)
    ranges = map_ranges(real_map)
    starts = [r[0] for r in ranges]
    import bisect

    def program_at(off):
        a = start + off
        k = bisect.bisect_right(starts, a) - 1
        if k >= 0 and ranges[k][0] <= a < ranges[k][0] + ranges[k][1]:
            return ranges[k][2]
        return None

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

    real_syms = data_symbols(real_elf)
    stub_syms = data_symbols(stub_elf)
    body_bytes = 0
    pointer_bytes = 0
    dom_lo, dom_hi = h[2] - h[1], h[3] - h[1]
    missing = []
    near = {}
    done_syms = 0
    for key, places in sorted(real_syms.items(), key=lambda kv: kv[1][0][0]):
        done_syms += 1
        if done_syms % 20000 == 0:
            print(f'strip.py: {done_syms} of {len(real_syms)} symbols, {body_bytes} body bytes', file=sys.stderr, flush=True)
        stub_places = stub_syms.get(key)
        for n_place, (addr, secname, _) in enumerate(places):
            if secname.startswith('sa1_data_'):
                continue
            at0 = addr - start
            size = key[1]
            if at0 < 0 or at0 + size > len(body):
                continue
            mine = bytes(body[at0:at0 + size])
            if stub_places and len(stub_places) == len(places):
                theirs = stub_places[n_place][2]
            else:
                theirs = None  # no stub twin: every byte is a candidate
            diff = [k for k in range(size) if at0 + k not in rel_words and
                    (theirs is None or mine[k] != theirs[k])]
            # the game's 32-bit pointers (into this image, where the stub has none): the port's
            ptr = set()
            for k in diff:
                w = (at0 + k) & ~3
                if w >= at0 and w + 4 <= at0 + size:
                    v = struct.unpack_from('<I', body, w)[0]
                    if dom_lo <= v < dom_hi:
                        t = struct.unpack_from('<I', theirs, w - at0)[0] if theirs is not None else 0
                        if t == 0 or dom_lo <= t < dom_hi:
                            ptr.add(k)
            pointer_bytes += len(ptr)
            diff = [k for k in diff if k not in ptr]
            if theirs is None:
                diff = [k for k in diff if mine[k]]
            if not diff:
                continue
            body_bytes += len(diff)
            prog = program_at(at0) or '1st_read'
            cands = [c for c in (source(prog), source('1st_read')) if c is not None]
            for c in cands:
                near[c] = match_region(at0, mine, sources[c][1], c, near.get(c, 0))[1]
                if all(covered[at0 + k] for k in diff):
                    break
            # what no run covered: the differing bytes in pieces, anywhere
            k = 0
            while k < len(diff):
                if covered[at0 + diff[k]]:
                    k += 1
                    continue
                m = k
                while m + 1 < len(diff) and diff[m + 1] == diff[m] + 1 and not covered[at0 + diff[m + 1]]:
                    m += 1
                lo, hi = diff[k], diff[m] + 1
                piece = mine[lo:hi]
                found = False
                for c in cands:
                    j = sources[c][1].find(piece)
                    if j >= 0:
                        take(at0 + lo, c, j, hi - lo)
                        found = True
                        break
                if not found and hi - lo >= 4:
                    # strings the linker merged lose their program: look in every image's file
                    c, j = find_anywhere(piece)
                    if c is not None:
                        take(at0 + lo, c, j, hi - lo)
                        found = True
                if not found:
                    missing.append((at0 + lo, at0 + hi, f'{key[0]} ({prog})'))
                k = m + 1
    # 3. anything else the disc holds: every 16-byte window of the image (at 4-byte steps) found
    # in any image's file, grown byte by byte while it agrees
    import numpy as np
    from audit import hashes, W
    for name in sorted(files):
        source(name)
    ih, ok = hashes(bytes(body), 4)
    cov = np.frombuffer(bytes(covered), dtype=np.uint8)
    nwin = len(ih)
    full = np.ones(nwin, bool)
    for k in range(W):
        full &= cov[np.arange(nwin) * 4 + k] == 1
    want = ok & ~full
    order = np.argsort(ih)
    sh = ih[order]
    owner = np.full(nwin, -1, np.int64)
    where = np.zeros(nwin, np.int64)
    for c in range(len(sources)):
        d = sources[c][1]
        if len(d) < W:
            continue
        hs, _ = hashes(d, 1)
        uh, first = np.unique(hs, return_index=True)
        pos = np.searchsorted(uh, sh)
        pos[pos >= len(uh)] = len(uh) - 1
        hit = uh[pos] == sh
        wins = order[hit]
        srcpos = first[pos[hit]]
        free = (owner[wins] < 0) & want[wins]
        owner[wins[free]] = c
        where[wins[free]] = srcpos[free]
    extra = 0
    wins = np.nonzero(owner >= 0)[0]
    last_end = -1
    for w in wins.tolist():
        at = w * 4
        if all(covered[at:at + W]):
            continue
        c = int(owner[w])
        j = int(where[w])
        f = sources[c][1]
        if bytes(body[at:at + W]) != f[j:j + W]:
            continue  # a hash collision
        b = 0
        while at - b > 0 and j - b > 0 and body[at - b - 1] == f[j - b - 1] and not covered[at - b - 1]:
            b += 1
        n = W
        while at + n < len(body) and j + n < len(f) and body[at + n] == f[j + n]:
            n += 1
        a0, j0, n0 = at - b, j - b, n + b
        k = 0
        while k < n0:
            if covered[a0 + k] or a0 + k in rel_words:
                k += 1
                continue
            e = k
            while e < n0 and not covered[a0 + e] and a0 + e not in rel_words:
                e += 1
            take(a0 + k, c, j0 + k, e - k)
            extra += e - k
            k = e
        last_end = a0 + n0
    print(f'strip.py: {extra} more bytes found on the disc elsewhere in the image', file=sys.stderr, flush=True)

    del ih, ok, order, sh, owner, where, cov, full, want, wins
    # 4. the same on the image as it now stands (taken bytes zeroed): short disc strings beside bytes
    # already taken, whose windows only match the disc once those are zero
    view = bytearray(body)
    for at, src, s_at, n in pieces:
        view[at:at + n] = bytes(n)
    ih, ok = hashes(bytes(view), 4)
    order = np.argsort(ih)
    sh = ih[order]
    more = 0
    for c in range(len(sources)):
        d = sources[c][1]
        if len(d) < W:
            continue
        hs, _ = hashes(d, 1)
        uh, first = np.unique(hs, return_index=True)
        pos = np.searchsorted(uh, sh)
        pos[pos >= len(uh)] = len(uh) - 1
        hit = (uh[pos] == sh) & ok[order]
        for w, j in zip(order[hit].tolist(), first[pos[hit]].tolist()):
            at = w * 4
            if bytes(view[at:at + W]) != d[j:j + W]:
                continue
            for k in range(W):
                if not covered[at + k] and at + k not in rel_words and view[at + k]:
                    e = k
                    while e < W and not covered[at + e] and at + e not in rel_words:
                        e += 1
                    take(at + k, c, j + k, e - k)
                    for q in range(k, e):
                        view[at + q] = 0
                    more += e - k
    print(f'strip.py: {more} more bytes in short disc strings', file=sys.stderr, flush=True)

    missing = [(k, m, what) for k, m, what in missing if not all(covered[k:m])]
    with open(out_fill + '.left.txt', 'w') as lf:
        for k, m, what in missing:
            lf.write(f'{k:#x} {m - k} {what} {bytes(body[k:m]).hex()}\n')
    print(f'strip.py: {pointer_bytes} body bytes are the game\'s 32-bit pointers into the image (kept)', file=sys.stderr)
    if missing:
        total = sum(m - k for k, m, _ in missing)
        print(f'strip.py: {len(missing)} pieces ({total} bytes) of the bodies found in no disc file:', file=sys.stderr)
        for k, m, what in missing[:30]:
            print(f'  image+{k:#x}, {m - k} bytes, {what}: {bytes(body[k:m])[:24]!r}', file=sys.stderr)
        if total > int(os.environ.get('STRIP_MAY_MISS', '0')):
            sys.exit(1)

    # the stripped image: every byte a piece fills, zeroed; verify the fill gives the image back
    stripped = bytearray(body)
    for at, src, s_at, n in pieces:
        stripped[at:at + n] = bytes(n)
    check = bytearray(stripped)
    for at, src, s_at, n in pieces:
        check[at:at + n] = sources[src][1][s_at:s_at + n]
    assert check == body, 'the fill table does not give the image back'

    fill = bytearray()
    srcs = bytearray()
    for path, data, prs in sources:
        srcs += struct.pack('<32sI20sII', path.encode(), len(data), hashlib.sha1(data).digest(), prs, 0)
    pcs = bytearray()
    for at, src, s_at, n in sorted(pieces):
        pcs += struct.pack('<IIII', at, src, s_at, n)
    crc = zlib.crc32(bytes(srcs) + bytes(pcs))
    fill += struct.pack('<8sIIII', b'SARNXFL1', len(sources), len(pieces), crc, 0) + srcs + pcs
    open(out_fill, 'wb').write(fill)
    h2 = list(h)
    h2[7] = 1    # flags: to be filled
    h2[8] = crc  # pad: the fill table it goes with
    write_image(out_img, h2, segs, stripped, rels)

    left = content_total - content_taken
    print(f'strip.py: {len(sources)} disc files, {len(pieces)} pieces, {sum(p[3] for p in pieces)} bytes taken '
          f'from the disc; content {content_taken} of {content_total} bytes ({left} left: the pointers), '
          f'bodies {body_bytes} bytes; fill table {len(fill)} bytes, crc {crc:08x}')


if __name__ == '__main__':
    main()
