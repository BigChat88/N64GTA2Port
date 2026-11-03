#!/usr/bin/env python3
"""fix_zone_font.py - redraws the O of the zone name font in a converted (big-endian) city style.

The game's own font 5 has an O made of a short ring with a separate bar under it. This makes it a plain O as tall as the
other letters, using the ring's own pixels. A style without that font or without that shape is left as it is.

usage: fix_zone_font.py <style.sty>
"""
import struct, sys

ZONE_FONT = 5
LETTER_ROWS = 20
RING_ROWS = 15  # rows of the ring, before the bar


def chunks(d):
    o, out = 6, {}
    while o + 8 <= len(d):
        n = struct.unpack('>I', d[o + 4:o + 8])[0]
        out[d[o:o + 4]] = (o + 8, n)
        o += 8 + n
    return out


def fix(data):
    """Returns the fixed bytes, or None if nothing applies."""
    ch = chunks(bytes(data))
    if not all(k in ch for k in (b'SPRB', b'FONB', b'SPRX', b'SPRG')):
        return None
    o, _ = ch[b'FONB']
    count = struct.unpack_from('>H', data, o)[0]
    if count != 7:  # the menu style has other fonts
        return None
    bases = struct.unpack_from('>%dH' % count, data, o + 2)
    o, _ = ch[b'SPRB']
    first = sum(struct.unpack_from('>5H', data, o)) + sum(bases[:ZONE_FONT])
    sprite = first + ord('O') - 33
    o, _ = ch[b'SPRX']
    ptr, w, h = struct.unpack_from('>IBB', data, o + sprite * 8)
    g, _ = ch[b'SPRG']
    row = lambda y: bytes(data[g + ptr + y * 256:g + ptr + y * 256 + w])
    used = [y for y in range(h) if any(row(y))]
    # the expected shape: a ring, one empty row, a bar
    if len(used) != RING_ROWS + 4 or used[RING_ROWS] != used[RING_ROWS - 1] + 2 or used[RING_ROWS - 1] != used[0] + RING_ROWS - 1:
        return None
    ring = [row(y) for y in used[:RING_ROWS]]
    side = ring[5]
    new = ring[:5] + [side] * (LETTER_ROWS - 10) + ring[-5:]
    out = bytearray(data)
    for i, r in enumerate(new):
        y = used[0] + i
        out[g + ptr + y * 256:g + ptr + y * 256 + w] = r
    return out


def main():
    path = sys.argv[1]
    data = bytearray(open(path, 'rb').read())
    fixed = fix(data)
    if fixed is not None:
        open(path, 'wb').write(fixed)


if __name__ == '__main__':
    main()
