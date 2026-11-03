#!/usr/bin/env python3
"""audio_le2be.py - GTA 2 sound banks (PC, little-endian) to N64 (big-endian).
   .raw: 16-bit mono PCM -> swaps the 2 bytes of each sample and leaves room for the mixer (see BANK_GAIN).
   .sdt: 320 entries of 6 x s32 (offset, size, rate, shift, loop start/end) -> swaps 4 bytes.
usage: audio_le2be.py <input> <output>"""
import array
import sys

# libdragon's mixer resamples each channel with a Hermite that overshoots up to 25% between samples, and saturates that
# value BEFORE applying any volume: a sound near full scale clips however low it plays. 243 of the 317 effects peak above
# 1/1.25 = 0.8, and measured through that resampler at their own rates, 1 in 3 goes over full scale (up to 1.242).
# The same gain for the whole bank keeps the original balance between effects.
BANK_GAIN = 0.78


def bank_headroom(be):
    """Applies BANK_GAIN to big-endian 16-bit samples."""
    s = array.array("h", be)
    if sys.byteorder == "little":
        s.byteswap()
    s = array.array("h", [round(v * BANK_GAIN) for v in s])
    if sys.byteorder == "little":
        s.byteswap()
    return s.tobytes()


def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = bytearray(open(src, 'rb').read())
    w = 4 if src.lower().endswith('.sdt') else 2
    assert len(d) % w == 0, "%s: size %d is not a multiple of %d" % (src, len(d), w)
    for i in range(0, len(d), w):
        d[i:i + w] = d[i:i + w][::-1]
    open(dst, 'wb').write(d if w == 4 else bank_headroom(bytes(d)))


if __name__ == "__main__":
    main()
