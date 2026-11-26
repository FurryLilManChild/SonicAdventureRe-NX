"""SonicAdventureRe-nx: what the English tools share: PRS, AFS, and the US cutscenes' language tables.

Python's standard library only, so the converter runs anywhere Python 3 does.
"""
import struct

EVENT_BASE = 0x0cb80000  # where a US cutscene module loads


def prs_decompress(src):
    """Sega's PRS (LZ77 with a bit stream of flags, read low bit first)."""
    out = bytearray()
    pos = 0
    flags = 0
    left = 0

    def bit():
        nonlocal pos, flags, left
        if left == 0:
            flags = src[pos]
            pos += 1
            left = 8
        b = flags & 1
        flags >>= 1
        left -= 1
        return b

    while pos < len(src):
        if bit():
            out.append(src[pos])
            pos += 1
            continue
        if bit():
            if pos + 1 >= len(src):
                break
            word = src[pos] | src[pos + 1] << 8
            pos += 2
            if word == 0:
                break
            offset = (word >> 3) - 0x2000
            size = word & 7
            if size:
                size += 2
            else:
                size = src[pos] + 1
                pos += 1
        else:
            size = (bit() << 1 | bit()) + 2
            offset = src[pos] - 0x100
            pos += 1
        start = len(out) + offset
        for i in range(size):
            out.append(out[start + i])
    return bytes(out)


def afs_entries(data):
    """an AFS archive's (offset, size) of each file"""
    if data[:3] != b'AFS':
        raise ValueError('not an AFS archive')
    count = struct.unpack_from('<I', data, 4)[0]
    return [struct.unpack_from('<II', data, 8 + 8 * i) for i in range(count)]


def string_at(d, ptr):
    if not ptr:
        return None
    o = ptr - EVENT_BASE
    return d[o:d.index(b'\0', o)]


def language_tables(d):
    """each table of a US cutscene module: five parallel arrays (Japanese, English, French,
    Spanish, German) of strings, None for an empty slot"""
    words = [struct.unpack_from('<I', d, k)[0] for k in range(0, len(d) - 3, 4)]
    inside = lambda w: EVENT_BASE <= w < EVENT_BASE + len(d)
    found = []
    for k in range(len(words) - 4):
        w = words[k:k + 5]
        if not all(inside(x) for x in w):
            continue
        stride = w[1] - w[0]
        if stride <= 0 or stride % 4 or any(w[i + 1] - w[i] != stride for i in range(4)):
            continue
        n = stride // 4
        arrays = [[struct.unpack_from('<I', d, x - EVENT_BASE + 4 * i)[0] for i in range(n)] for x in w]
        if all(p == 0 or inside(p) for a in arrays for p in a) and any(arrays[1]):
            found.append([[string_at(d, p) for p in a] for a in arrays])
    return found


def slots(d):
    """the cutscene's subtitles in order: (table, first slot, slot count, Japanese bytes); a
    Japanese line owns the English slots up to the next Japanese line"""
    out = []
    for t, table in enumerate(language_tables(d)):
        japanese, english = table[0], table[1]
        for i, line in enumerate(japanese):
            if line is not None:
                out.append([t, i, 0, line])
            if out and out[-1][0] == t and english[i] is not None:
                out[-1][2] = i - out[-1][1] + 1
    return out


def english_lines(d, table, first, count):
    t = language_tables(d)[table]
    return [s for s in t[1][first:first + count] if s is not None]


def prs_store(data):
    """PRS that only copies bytes (every flag a literal), which prs_decompress reads back as data.
    The game reads PRS either way; a file twice the work to compress for no gain here."""
    out = bytearray()
    for i in range(0, len(data), 8):
        chunk = data[i:i + 8]
        if len(chunk) == 8:
            out.append(0xff)
            out += chunk
            continue
        # the last few literals, then the end: flags 0 then 1 (a long copy), and a zero word
        bits = [1] * len(chunk) + [0, 1]
        flags = 0
        for k, b in enumerate(bits[:8]):
            flags |= b << k
        out.append(flags)
        out += chunk
        if len(bits) > 8:
            rest = bits[8:]
            f2 = 0
            for k, b in enumerate(rest):
                f2 |= b << k
            out.append(f2)
        out += b'\0\0'
        return bytes(out)
    out += bytes([0b10]) + b'\0\0'  # flags 0, 1: the end
    return bytes(out)


def chunks(d, start):
    """a PVM's or PVR's chunks from start: (tag, offset, length with its 8-byte head)"""
    out = []
    pos = start
    while pos + 8 <= len(d):
        tag = d[pos:pos + 4]
        if tag not in (b'GBIX', b'PVRT', b'PVPL'):
            k = min([x for x in (d.find(b'GBIX', pos), d.find(b'PVRT', pos)) if x >= 0], default=-1)
            if k < 0:
                break
            pos = k
            continue
        size = struct.unpack_from('<I', d, pos + 4)[0]
        out.append((tag, pos, 8 + size))
        pos += 8 + size
    return out


def pvrt_dims(d, at):
    return struct.unpack_from('<HH', d, at + 12)


def swap_textures(jp, us, start_jp, start_us):
    """the Japanese textures (PVM or PVR, from start) with each PVRT whose size matches the US one's
    in the same place replaced by the US one; the Japanese GBIX (the texture's identity) stays.
    Returns (bytes, swapped, total)"""
    pj = [c for c in chunks(jp, start_jp) if c[0] == b'PVRT']
    pu = [c for c in chunks(us, start_us) if c[0] == b'PVRT']
    out = bytearray()
    last = 0
    swapped = 0
    for i, (_, at, n) in enumerate(pj):
        out += jp[last:at]
        if i < len(pu) and pvrt_dims(jp, at) == pvrt_dims(us, pu[i][1]):
            out += us[pu[i][1]:pu[i][1] + pu[i][2]]
            swapped += 1
        else:
            out += jp[at:at + n]
        last = at + n
    out += jp[last:]
    return bytes(out), swapped, len(pj)


def mlt_banks(d):
    count = struct.unpack_from('<i', d, 8)[0]
    return [struct.unpack_from('<8i', d, 0x20 + 0x20 * i) for i in range(count)]


def swap_banks(jp, us):
    """the Japanese sound unit with each bank's bytes from the US unit where the US holds the same
    bank (type, number, address and room); the layout stays the Japanese one. Most banks are rooms
    only (no bytes, data -1); the rest, a unit's own sounds, line up in both releases.
    Returns (bytes, swapped, total), counting the banks that hold bytes only; swapped < total means
    a bank with bytes has no US counterpart, and the unit is better left whole Japanese"""
    bj, bu = mlt_banks(jp), mlt_banks(us)
    by_key = {(b[0], b[1], b[2], b[3]): b for b in bu}
    head = bytearray(jp[:0x20 + 0x20 * len(bj)])
    blobs = []
    swapped = 0
    for b in bj:
        u = by_key.get((b[0], b[1], b[2], b[3]))
        if u and u[5] > 0 and b[5] > 0:
            blobs.append(us[u[4]:u[4] + u[5]])
            swapped += 1
        else:
            blobs.append(jp[b[4]:b[4] + b[5]] if b[5] > 0 else b'')
    total = sum(1 for b in bj if b[5] > 0)
    out = bytearray(head)
    for i, (b, blob) in enumerate(zip(bj, blobs)):
        offset = len(out) if blob else b[4]
        if blob:
            out += blob
            out += b'\0' * (-len(out) % 32)
        rec = list(b)
        rec[4], rec[5] = offset, len(blob) if blob else b[5]
        struct.pack_into('<8i', out, 0x20 + 0x20 * i, *rec)
    return bytes(out), swapped, total


SCRIPT_END, SCRIPT_BRANCH, SCRIPT_FLAG, SCRIPT_VOICE, SCRIPT_CHARACTERS, SCRIPT_EVENTS, SCRIPT_FLAGS = \
    -1, -2, -3, -4, -5, -6, -7


def messages_rebased(us, jp_base, voice_of):
    """a US field message file (MR_MES_A_E.BIN ...) for the Japanese game: its pointers moved to the
    Japanese file's base, and each script's voice turned from a US clip number to the number the
    Japanese game plays it by (voice_of). Returns (bytes, voices changed)"""
    d = bytearray(us)
    words = [struct.unpack_from('<I', d, k)[0] for k in range(0, len(d) - 3, 4)]
    ptrs = [w for w in words if 0x0c000000 <= w < 0x0d000000]
    us_base = min(ptrs) & ~0xfff
    delta = jp_base - us_base
    inside = lambda w: us_base <= w < us_base + len(d)
    # the scripts, found through the table before anything moves
    table = struct.unpack_from('<I', d, 4)[0]
    scripts = set()
    at = table - us_base
    while 0 <= at <= len(d) - 8:
        s, r = struct.unpack_from('<II', d, at)
        if not ((s == 0 or inside(s)) and (r == 0 or inside(r))) or (s == 0 and r == 0):
            break
        if s:
            scripts.add(s - us_base)
        at += 8
    changed = 0
    for s in sorted(scripts):
        k = s
        for _ in range(4096):
            if k + 2 > len(d):
                break
            w = struct.unpack_from('<h', d, k)[0]
            if w == SCRIPT_END:
                break
            if w == SCRIPT_VOICE:
                v = struct.unpack_from('<h', d, k + 2)[0]
                if v >= 0:
                    struct.pack_into('<h', d, k + 2, voice_of(v))
                    changed += 1
                k += 4
            elif w in (SCRIPT_FLAG, SCRIPT_CHARACTERS):
                k += 4
            elif w in (SCRIPT_EVENTS, SCRIPT_FLAGS):
                k += 2
                while k + 2 <= len(d) and struct.unpack_from('<h', d, k)[0] >= 0:
                    k += 2
            else:
                k += 2
    for k in range(0, len(d) - 3, 4):
        w = struct.unpack_from('<I', d, k)[0]
        if inside(w):
            struct.pack_into('<I', d, k, w + delta)
    return bytes(d), changed


# --- stage hints (Tikal's orbs, and the areas' hint pages): in each US stage module, page arrays
# (text, steps) ending (0, 0); a block of Japanese ones back to back, then the English ones

MODULE_BASE = 0x0c900000  # where a stage or area module loads


def module_base(module):
    """where a module loads: a stage or area at 0x0c900000, a garden or the Chao race at
    0x0cb80000 (above AL_MAIN, the code they share)"""
    m = module.lower()
    return 0x0cb80000 if m.startswith(('al_garden', 'al_race')) else 0x0c900000


def hint_runs(d):
    """the module's page arrays in order: (offset, [page text, ...])"""
    inside = lambda p: MODULE_BASE <= p < MODULE_BASE + len(d)

    def text(p):
        o = p - MODULE_BASE
        e = d.find(b'\0', o)
        return d[o:e] if e > o else None

    found, i = [], 0
    while i + 8 <= len(d):
        pages, k = [], i
        while k + 8 <= len(d):
            p, v = struct.unpack_from('<II', d, k)
            if not inside(p) or v > 0xffff:
                break
            t = text(p)
            if not t or len(t) < 3 or (t[0] not in (7, 9, 0x81, 0x82, 0x83) and not t[:1].isalpha()):
                break
            pages.append(t)
            k += 8
        if pages and k + 8 <= len(d) and struct.unpack_from('<II', d, k) == (0, 0):
            found.append((i, pages))
            i = k + 8
        else:
            i += 4
    return found


def is_japanese(t):
    return len(t) > 4 and t[0] in (7, 9) and sum(1 for b in t if b >= 0x81) > len(t) // 3


def hint_blocks(d):
    """[(Japanese runs, English runs)]: Japanese page arrays back to back, then as many more back
    to back in no Japanese (the US release's next language, English)"""
    rs = hint_runs(d)
    end = lambda r: r[0] + 8 * (len(r[1]) + 1)
    out, i = [], 0
    while i < len(rs):
        if not all(is_japanese(t) for t in rs[i][1]):
            i += 1
            continue
        j = i + 1
        while j < len(rs) and rs[j][0] == end(rs[j - 1]) and all(is_japanese(t) for t in rs[j][1]):
            j += 1
        n = j - i
        e = rs[j:j + n]
        if (len(e) == n and e[0][0] == end(rs[j - 1]) and all(e[k][0] == end(e[k - 1]) for k in range(1, n))
                and not any(is_japanese(t) for r in e for t in r[1])):
            out.append((rs[i:j], e))
        i = j
    return out


def hint_english(blocks, b, r, p):
    """the English parts for page p of run r of block b: the same page where the two runs have as
    many pages, all of a one-page run's English, else page p (the last page takes the rest)"""
    J, E = blocks[b]
    jr, er = J[r][1], E[r][1]
    if len(jr) == len(er):
        return [er[p]]
    if len(jr) == 1:
        return list(er)
    if len(er) > len(jr):
        return [er[p]] if p < len(jr) - 1 else list(er[p:])
    return None
