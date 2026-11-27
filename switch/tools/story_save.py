#!/usr/bin/env python3
"""Put one save of a card image at a point in a character's story, for testing.

    story_save.py CARD.BIN OUT.BIN --slot 1 --amy-zero

The card image is the port's card.bin: a 128 KB memory card holding SONICADV_SYS. The game's
saves are struct save (src/save/save.h, 0x4a0 bytes each, three of them after the file's
header and two icon frames). Each carries a CRC-16 (save.c's save_checksum) and the file a
CRC-16 in its header (the card's own, polynomial 0x1021), and both are made again here.

--amy-zero: Amy's story at its last section, ZERO (src/progress/story.c's amy_story[8]),
on the Egg Carrier at seq 0, as story_save leaves it there: the next step plays event 113 and
goes to the Zero fight, then event 114, ev0072, the end of her story. Amy is made playable,
and her Station Square flags, Twinkle Park's clear and the sunk Egg Carrier are set.

--complete: every story ended, the last story too (src/progress/progress.h's FLAG_PLAYABLE_*,
FLAG_ALL_CLEAR, FLAG_CLEAR_*), every character's upgrades, all 130 emblems, and each stage
counted cleared at least once for each character (stage_clears), for testing what opens
after the game is finished. The story records are left as they are.
"""
import argparse
import struct

BLOCK = 512
SAVE_SIZE = 0x4A0
SAVES_AT = 0x480  # after the 0x80 header and two 0x200 icon frames
EVENT_FLAGS = 0x260
STORY_RECORDS = 0x2E8
RECORD = 12
CHARACTER_AMY = 5
STAGE_EGG_CARRIER = 29
AMY_ZERO_SECTION = 8
STORY_NO_WAIT = 0xFFFF
FLAG_PLAYABLE_SONIC = 65
FLAG_PLAYABLE_AMY = 68
AMY_FLAGS = [320, 321, 323, 324, 325, 326, 327, 333, 342]
COMPLETE_FLAGS = list(range(65, 79))  # playable 65-70, all clear 71, clears 72-78
UPGRADE_FLAGS = [141, 142, 159, 205, 219, 281, 282, 338, 350, 393, 394, 464, 465,
                 478, 479, 480, 481, 482]
EMBLEMS_AT = 0x240
EMBLEMS = 130
STAGE_CLEARS = 0x348  # after the eight story records
CHARACTERS = 8
STAGES = 43


def save_crc(buf):
    crc = 0xFFFF
    for x in buf:
        crc ^= x
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


def file_crc(buf):
    crc = 0
    for x in buf:
        crc ^= x << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def file_blocks(card, name):
    root = 255 * BLOCK
    fat_block, dir_block, dir_count = (struct.unpack_from('<H', card, root + o)[0]
                                       for o in (0x46, 0x4A, 0x4C))
    fat = [struct.unpack_from('<H', card, fat_block * BLOCK + 2 * i)[0] for i in range(256)]
    for block in range(dir_block, dir_block - dir_count, -1):
        for entry in range(16):
            o = block * BLOCK + entry * 32
            if card[o] == 0 or bytes(card[o + 4:o + 16]).rstrip(b'\0') != name:
                continue
            b, size, chain = struct.unpack_from('<H', card, o + 2)[0], struct.unpack_from('<H', card, o + 0x18)[0], []
            while b < 256 and len(chain) < size:
                chain.append(b)
                b = fat[b]
            return chain
    raise SystemExit(f'no {name.decode()} on the card')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('card')
    ap.add_argument('out')
    ap.add_argument('--slot', type=int, default=1)
    ap.add_argument('--amy-zero', action='store_true')
    ap.add_argument('--complete', action='store_true')
    a = ap.parse_args()
    card = bytearray(open(a.card, 'rb').read())
    blocks = file_blocks(card, b'SONICADV_SYS')
    f = bytearray(b''.join(card[b * BLOCK:(b + 1) * BLOCK] for b in blocks))
    at = SAVES_AT + a.slot * SAVE_SIZE
    s = f[at:at + SAVE_SIZE]
    if struct.unpack_from('<H', s, 0)[0] != save_crc(s[4:]):
        raise SystemExit(f'save {a.slot} fails its checksum')
    if a.amy_zero:
        for flag in [FLAG_PLAYABLE_SONIC, FLAG_PLAYABLE_AMY] + AMY_FLAGS:
            s[EVENT_FLAGS + flag // 8] |= 1 << flag % 8
        struct.pack_into('<bbhhhHH', s, STORY_RECORDS + RECORD * CHARACTER_AMY, 0, 0, 0,
                         AMY_ZERO_SECTION, 0, STAGE_EGG_CARRIER << 8, STORY_NO_WAIT)
    if a.complete:
        for flag in COMPLETE_FLAGS + UPGRADE_FLAGS:
            s[EVENT_FLAGS + flag // 8] |= 1 << flag % 8
        for e in range(EMBLEMS):
            s[EMBLEMS_AT + e // 8] |= 1 << e % 8
        for i in range(CHARACTERS * STAGES):
            if s[STAGE_CLEARS + i] == 0:
                s[STAGE_CLEARS + i] = 1
    struct.pack_into('<H', s, 0, save_crc(s[4:]))
    f[at:at + SAVE_SIZE] = s
    size = 0x80 + struct.unpack_from('<H', f, 0x40)[0] * 0x200 + struct.unpack_from('<I', f, 0x48)[0]
    f[0x46:0x48] = b'\0\0'
    struct.pack_into('<H', f, 0x46, file_crc(f[:size]))
    for i, b in enumerate(blocks):
        card[b * BLOCK:(b + 1) * BLOCK] = f[i * BLOCK:(i + 1) * BLOCK]
    open(a.out, 'wb').write(card)
    print(f'{a.out}: save {a.slot}' + (' at Amy before Zero' if a.amy_zero else '') +
          (', everything complete' if a.complete else ''))


if __name__ == '__main__':
    main()
