#!/usr/bin/env python3
"""Converts GTA 2 .scr scripts (PC, little-endian) to N64 (big-endian).

The engine reads the .scr as raw memory blocks. Each script command is a different structure
(92 types in gta2_re); the table below says, per opcode, which 16/32-bit fields the engine reads.
It was generated from the gta2_re code (tools/scr_table.json); 8-bit fields do not change.
Command bytes outside the table are swapped as 16 bits (the common case), except known 8-bit ones.

usage: scr_le2be.py <input> <output>     (detects: main = 82656 bytes; mission = anything else)
"""
import json, os, struct, sys

TAB = {int(k): v for k, v in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'scr_table.json'))).items()}

def swap16(buf, o):
    buf[o], buf[o + 1] = buf[o + 1], buf[o]

def swap32(buf, o):
    buf[o:o + 4] = buf[o:o + 4][::-1]

def convert_commands(data, offs):
    """data: bytearray with the commands; offs: offsets (within data) of all commands"""
    so = sorted(set(offs))
    nxt = {so[i]: (so[i + 1] if i + 1 < len(so) else len(data)) for i in range(len(so))}
    unknown = set()
    for o in so:
        typ = struct.unpack_from('<H', data, o + 2)[0]       # read the type BEFORE swapping
        size = nxt[o] - o
        for k in range(0, 8, 2):                             # header: 4 16-bit fields
            swap16(data, o + k)
        ent = TAB.get(typ)
        if ent is None:
            unknown.add(typ)
            ent = {'p': [], 'b': []}
        done = set()
        for fo, fs in ent['p']:
            if fo + fs > size: continue
            (swap32 if fs == 4 else swap16)(data, o + fo)
            done.update(range(fo, fo + fs))
        u8 = set(ent['b'])
        for k in range(8, size - 1, 2):                      # rest: 16 bits, without touching 8-bit bytes
            if k in done or k + 1 in done or k in u8 or k + 1 in u8: continue
            swap16(data, o + k)
    return unknown

def convert_main(b):
    out = bytearray(b)
    bp = struct.unpack_from('<6000H', b, 0)
    for i in range(6000): swap16(out, i * 2)
    data = out[12000:12000 + 65536]
    unk = convert_commands(data, [bp[i] for i in range(1, 6000) if bp[i]])
    out[12000:12000 + 65536] = data
    t = 12000 + 65536
    tsz = struct.unpack_from('<H', b, t)[0]
    swap16(out, t)
    p = t + 2; end = p + tsz
    while p < end:                                           # texts: id, zone, type, field 6 (16 bits) + size (8 bits)
        ln = out[p + 8]
        for k in range(0, 8, 2): swap16(out, p + k)
        p += 9 + ln
    assert p == end, 'inconsistent text table'
    return bytes(out), unk

def convert_mission(b):
    out = bytearray(b)
    base, start, rem = struct.unpack_from('<HHI', b, 0)
    swap16(out, 0); swap16(out, 2); swap32(out, 4)
    bp = struct.unpack_from('<1536H', b, 8)
    for i in range(1536): swap16(out, 8 + i * 2)
    data = out[8 + 0xC00:]
    o0 = bp[0]
    unk = convert_commands(data, [bp[k] - o0 for k in range(1536) if bp[k]])
    out[8 + 0xC00:] = data
    return bytes(out), unk

def convert(b):
    return convert_main(b) if len(b) == 82656 else convert_mission(b)

if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    b = open(src, 'rb').read()
    out, unk = convert(b)
    if unk: print('WARNING %s: opcodes with no table: %s' % (src, sorted(unk)))
    open(dst, 'wb').write(out)
