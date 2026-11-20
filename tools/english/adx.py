"""CRI ADX reading for the English layer (SonicAdventureRe-nx): an AFS archive's entries and a
standard ADX stream decoded to 16-bit samples (mono or interleaved stereo)."""
import math
import struct


def afs_entries(data):
    """[(offset, size)] of an AFS archive"""
    if data[:4] != b'AFS\x00':
        raise ValueError('not an AFS archive')
    n = struct.unpack_from('<I', data, 4)[0]
    return [struct.unpack_from('<II', data, 8 + 8 * i) for i in range(n)]


def header(adx):
    """(channels, rate, samples, block size, data start)"""
    if adx[0] != 0x80:
        raise ValueError('not ADX')
    offset = struct.unpack_from('>H', adx, 2)[0]
    block = adx[5]
    channels = adx[7]
    rate, samples = struct.unpack_from('>II', adx, 8)
    return channels, rate, samples, block, offset + 4


def decode(adx, limit=None):
    """the samples of channel 0, as ints, up to limit"""
    channels, rate, samples, block, start = header(adx)
    cutoff = struct.unpack_from('>H', adx, 0x10)[0]
    a = math.sqrt(2) - math.cos(2 * math.pi * cutoff / rate)
    b = math.sqrt(2) - 1
    c = (a - math.sqrt((a + b) * (a - b))) / b
    coef1 = int(c * 8192)
    coef2 = int(-(c * c) * 4096)
    want = samples if limit is None else min(samples, limit)
    out = []
    h1 = h2 = 0
    per_block = (block - 2) * 2
    pos = start
    while len(out) < want and pos + block * channels <= len(adx):
        scale = struct.unpack_from('>H', adx, pos)[0] + 1
        for i in range(per_block):
            byte = adx[pos + 2 + i // 2]
            nib = (byte >> 4) if i % 2 == 0 else (byte & 15)
            if nib & 8:
                nib -= 16
            s = nib * scale + ((coef1 * h1 + coef2 * h2) >> 12)
            s = max(-32768, min(32767, s))
            h2, h1 = h1, s
            out.append(s)
        pos += block * channels
    return out[:want]
