#!/usr/bin/env python3
"""sfx_share.py - a single sound effect bank for ALL maps (bil, ste, wil).

The maps' .raw banks are ~99% identical. Instead of ~6 MB per map in the ROM, ONE bank is written (audio/sfx.raw):
the 1st map's bank (intact) + only the ranges not present in it. Each map gets its own .sdt pointing
at the right place and a 4-byte <map>.raw (placeholder: the engine only measures its size).

EXACT equivalence with the old pipeline (global 2-byte-per-sample swap of the whole file):
 - each "unit" (one effect, or the whole 69..89 / 115..135 range the game plays at once as a continuous block)
   is expanded to EVEN bounds [a,b), so each sample's byte partner comes along;
 - the unit is searched in the bank at an EVEN position (same parity) -> the result after the global swap is identical;
 - the relative layout inside the unit (distance between effects) is preserved -> PlaySampleRange stays valid.
Everything is checked byte by byte (after the swap) against each map's original bank; aborts if anything differs.
The bank written is then scaled as a whole to leave room for the mixer (audio_le2be.py, BANK_GAIN), except the
8-bit police radio, whose original bytes are put back: audio_n64.c widens and scales them as they are played.

usage: sfx_share.py <game_data_folder> <output_data_folder> <map1> [map2 ...]   (the 1st map is the base)
"""
import os, struct, sys

from audio_le2be import bank_headroom

RANGES = [(69, 89), (115, 135)]       # UpdateRadioChatterLoop_41FCA0 -> PlaySampleRange_58E8C0(idx, a3)
BYTES = (69, 135)                     # police radio: 8-bit unsigned (cSampleManager's field_98_hSample), not swapped nor scaled

def find_ci(folder, name):
    for f in os.listdir(folder):
        if f.lower() == name.lower():
            return os.path.join(folder, f)
    return None

def swap2(b):
    a = bytearray(b)
    a[0::2], a[1::2] = b[1::2], b[0::2]
    return bytes(a)

def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    src, out, maps = sys.argv[1], sys.argv[2], sys.argv[3:]
    adir = find_ci(src, 'audio')
    if not adir:
        sys.exit('ERROR: no audio folder in %s' % src)
    banks = {}
    for m in maps:
        rp, sp = find_ci(adir, m + '.raw'), find_ci(adir, m + '.sdt')
        if not rp or not sp:
            print("WARNING: bank '%s' does not exist in %s (ignored)" % (m, adir)); continue
        raw = open(rp, 'rb').read(); sdt = open(sp, 'rb').read()
        assert len(sdt) == 320 * 24 and len(raw) % 2 == 0, (m, len(sdt), len(raw))
        ents = [list(struct.unpack('<6i', sdt[i * 24:i * 24 + 24])) for i in range(320)]
        for e in ents:
            assert e[0] >= 0 and e[1] >= 0 and e[0] + e[1] <= len(raw), (m, e)
        banks[m] = (raw, ents)
    names = [m for m in maps if m in banks]
    if not names:
        sys.exit('ERROR: no bank found')
    base = names[0]
    bank = bytearray(banks[base][0])
    new_sdt = {base: [list(e) for e in banks[base][1]]}
    stats = {}
    for m in names[1:]:
        raw, ents = banks[m]
        groups = [[i] for i in range(320) if not any(a <= i <= b for a, b in RANGES)]
        groups += [list(range(a, b + 1)) for a, b in RANGES]
        res = [list(e) for e in ents]
        reused = added = 0
        for g in groups:
            live = [i for i in g if ents[i][1] > 0]
            if not live:
                continue
            lo = min(ents[i][0] for i in live) & ~1
            hi = max(ents[i][0] + ents[i][1] for i in live)
            hi += hi & 1
            chunk = raw[lo:hi]
            pos = -1
            p = bank.find(chunk)
            while p >= 0:
                if p % 2 == 0:
                    pos = p; break
                p = bank.find(chunk, p + 1)
            if pos < 0:
                pos = len(bank); bank += chunk; added += len(chunk)
            else:
                reused += len(chunk)
            for i in live:
                res[i][0] = pos + (ents[i][0] - lo)
        new_sdt[m] = res
        stats[m] = (reused, added)
    # check in the already-swapped domain (what the N64 reads)
    sb = swap2(bytes(bank))
    for m in names:
        raw, ents = banks[m]
        sr = swap2(raw)
        for i, (e, n) in enumerate(zip(ents, new_sdt[m])):
            assert e[1] == n[1] and e[2:] == n[2:], (m, i)
            if e[1] == 0: continue
            assert sr[e[0]:e[0] + e[1]] == sb[n[0]:n[0] + e[1]], (m, i, e, n)
        for a, b in RANGES:
            live = [i for i in range(a, b + 1) if ents[i][1] > 0]
            if not live: continue
            lo_i = min(live, key=lambda i: ents[i][0])
            hi_i = max(live, key=lambda i: ents[i][0] + ents[i][1])
            lo_e, lo_n, hi_e = ents[lo_i], new_sdt[m][lo_i], ents[hi_i]
            span = hi_e[0] + hi_e[1] - lo_e[0]
            assert sr[lo_e[0]:lo_e[0] + span] == sb[lo_n[0]:lo_n[0] + span], (m, 'range', a, b)
    odir = os.path.join(out, 'audio'); os.makedirs(odir, exist_ok=True)
    def put(path, data):
        if os.path.exists(path) and open(path, 'rb').read() == data: return
        open(path, 'wb').write(data)
    final = bytearray(bank_headroom(sb))
    for m in names:
        for i in range(BYTES[0], BYTES[1] + 1):
            pos, size = new_sdt[m][i][0], new_sdt[m][i][1]
            final[pos:pos + size] = bank[pos:pos + size]
    put(os.path.join(odir, 'sfx.raw'), bytes(final))
    for m in names:
        put(os.path.join(odir, m.lower() + '.sdt'), b''.join(struct.pack('>6i', *e) for e in new_sdt[m]))
        put(os.path.join(odir, m.lower() + '.raw'), b'SFX\0')
    tot = sum(len(banks[m][0]) for m in names)
    print('    [SFX] single bank sfx.raw = %d bytes (before: %d bytes in %d banks; saved %.2f MB)' %
          (len(bank), tot, len(names), (tot - len(bank)) / 1048576.0))
    for m, (r, a) in stats.items():
        print('          %s: %d bytes reused, %d new bytes' % (m, r, a))

main()
