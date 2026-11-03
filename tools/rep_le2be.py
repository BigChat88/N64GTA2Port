#!/usr/bin/env python3
"""rep_le2be.py - GTA 2 attract mode recordings (PC, little-endian) to N64 (big-endian).
   A text header of 268 bytes (stays as it is), then records of 3 x s32: frame, inputs, random value.
usage: rep_le2be.py <input> <output>"""
import os
import sys

HEADER = 268
RECORD = 12

src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
assert len(d) >= HEADER and (len(d) - HEADER) % RECORD == 0, "%s: size %d is not a header plus records" % (src, len(d))
for i in range(HEADER, len(d), 4):
    d[i:i + 4] = d[i:i + 4][::-1]
if os.path.exists(dst):
    os.remove(dst)  # an unconverted copy from an older build may be read-only, as game files can be
open(dst, 'wb').write(d)
