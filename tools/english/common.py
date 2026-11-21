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
