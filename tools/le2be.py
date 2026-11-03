#!/usr/bin/env python3
"""
le2be.py - converts GTA 2 data files from little-endian (PC) to big-endian (N64).

usage:  le2be.py <input> <output> [--from be]     converts (format from the extension)

The byte swap is an involution: applying it twice returns the original.
The parser reads size/count fields in the INPUT byte order (--from),
so BE->LE also works.

Chunks the engine does NOT load (it only seeks past them) are dropped from the output to save ROM.
"""
import sys, struct

class Conv:
    def __init__(self, data, src='le'):
        self.d = bytearray(data)
        self.e = '<' if src == 'le' else '>'

    def u16(self, o): return struct.unpack_from(self.e + 'H', self.d, o)[0]
    def u32(self, o): return struct.unpack_from(self.e + 'I', self.d, o)[0]
    def s32(self, o): return struct.unpack_from(self.e + 'i', self.d, o)[0]

    def sw(self, o, w, n=1):
        d = self.d
        for i in range(n):
            a = o + i * w
            d[a:a + w] = d[a:a + w][::-1]

# ------------------------------------------------------------------ chunks

def all16(c, o, n):
    assert n % 2 == 0, "odd size"
    c.sw(o, 2, n // 2)

def nothing(c, o, n): pass

# ---- STY
def all32(c, o, n):
    assert n % 4 == 0, "size not a multiple of 4"
    c.sw(o, 4, n // 4)

def sty_sprx(c, o, n):
    assert n % 8 == 0
    for i in range(n // 8):
        c.sw(o + i * 8, 4)
        c.sw(o + i * 8 + 6, 2)

def sty_delx(c, o, n):
    p = 0
    while p < n:
        cnt = c.d[o + p + 2]
        c.sw(o + p, 2)                 # which_sprite
        c.sw(o + p + 4, 2, cnt)        # sizes
        p += 4 + 2 * cnt
    assert p == n, "DELX does not add up (%d != %d)" % (p, n)

def sty_dels(c, o, n):
    p = 0
    while p < n:
        ln = c.d[o + p + 2]
        c.sw(o + p, 2)                 # offset
        p += 3 + ln
    assert p == n, "DELS does not add up (%d != %d)" % (p, n)

STY = {
    b'PALB': all16, b'SPRB': all16, b'FONB': all16, b'SPRX': sty_sprx,
    b'OBJI': nothing, b'PALX': all16, b'PPAL': all32, b'SPRG': nothing,
    b'TILE': nothing, b'DELS': sty_dels, b'DELX': sty_delx, b'CARI': nothing,
    b'SPEC': all16, b'RECY': nothing,
}
STY_DROP = {b'PSXT', b'OVLY'}

# ---- GXT
def gxt_tkey(c, o, n):
    assert n % 12 == 0
    for i in range(n // 12):
        c.sw(o + i * 12, 4)

GXT = {b'TKEY': gxt_tkey, b'TDAT': all16}
GXT_DROP = set()

# ---- GMP
def gmp_dmap(c, o, n):
    p = 0
    c.sw(o, 4, 256 * 256); p += 0x40000
    col_words = c.u32(o + p)            # read BEFORE swapping
    c.sw(o + p, 4); p += 4
    col_bytes = col_words * 4
    q = 0
    while q < col_bytes:                # columns: u8 height,u8 offset,u16 pad, u32 blockd[height-offset]
        h = c.d[o + p + q]; off = c.d[o + p + q + 1]
        nb = h - off
        assert nb >= 0
        c.sw(o + p + q + 2, 2)
        c.sw(o + p + q + 4, 4, nb)
        q += 4 + 4 * nb
    assert q == col_bytes, "DMAP columns do not add up (%d != %d)" % (q, col_bytes)
    p += col_bytes
    nblocks = c.u32(o + p)
    c.sw(o + p, 4); p += 4
    for i in range(nblocks):
        c.sw(o + p + i * 12, 2, 5)
    p += nblocks * 12
    assert p == n, "DMAP does not add up (%d != %d)" % (p, n)

def gmp_mobj(c, o, n):
    assert n % 6 == 0
    for i in range(n // 6):
        c.sw(o + i * 6, 2, 2)

def gmp_lght(c, o, n):
    assert n % 16 == 0
    for i in range(n // 16):
        b = o + i * 16
        c.sw(b, 4); c.sw(b + 4, 2, 4)

def gmp_anim(c, o, n):
    p = 0
    while p < n:
        ln = c.d[o + p + 4]
        c.sw(o + p, 2)
        c.sw(o + p + 6, 2, ln)
        p += 6 + 2 * ln
    assert p == n, "ANIM does not add up"

def gmp_rgen(c, o, n):
    p = 0
    for i in range(545):                # Junction_10: 4x u16, s32, 4x u8
        b = o + p + i * 16
        c.sw(b, 2, 4); c.sw(b + 8, 4)
    p += 0x2210
    for _ in range(2):                  # horz and vert segments
        for i in range(545):
            c.sw(o + p + i * 8, 2, 2)
        p += 0x1108
    c.sw(o + p, 2, 3); p += 6
    assert p == n, "RGEN does not add up (%d != %d)" % (p, n)

GMP = {b'DMAP': gmp_dmap, b'ZONE': nothing, b'MOBJ': gmp_mobj, b'ANIM': gmp_anim,
       b'LGHT': gmp_lght, b'RGEN': gmp_rgen}
GMP_KEEP_ONLY = True      # drops chunks that are not loaded (UMAP, CMAP, PSXM, EDIT...)

FORMATS = {
    'sty': (STY, STY_DROP, False),
    'gxt': (GXT, GXT_DROP, False),
    'gmp': (GMP, set(), True),
}

def convert(data, ext, src='le'):
    table, drop, keep_only = FORMATS[ext]
    c = Conv(data, src)
    d = c.d
    out = bytearray()
    c.sw(4, 2)                              # file header version
    out += d[0:6]
    p = 6
    n_total = len(d)
    while p + 8 <= n_total:
        typ = bytes(d[p:p + 4]); size = c.s32(p + 4)
        body = p + 8
        if size < 0 or body + size > n_total:
            raise ValueError("chunk %r with invalid size %d at %d" % (typ, size, p))
        if size > 0 and typ in table:
            table[typ](c, body, size)
        elif typ in drop or (keep_only and typ not in table):
            p = body + size                  # dropped
            continue
        c.sw(p + 4, 4)                       # chunk size
        out += d[p:body + size]
        p = body + size
    return bytes(out)

def convert_tga(data, src='le'):
    """TGA: u16 header fields (3,5,8,10,12,14) and 16-bit pixels. ID and footer stay as they are."""
    d = bytearray(data)
    e = '<' if src == 'le' else '>'
    idlen = d[0]
    w, h = struct.unpack_from(e + 'HH', d, 12)      # read BEFORE swapping
    bpp = d[16]
    for o in (3, 5, 8, 10, 12, 14):
        d[o:o + 2] = d[o:o + 2][::-1]
    if bpp == 16 and d[2] == 2:
        base = 18 + idlen
        n = w * h
        assert base + 2 * n <= len(d), "truncated TGA"
        for i in range(n):
            a = base + 2 * i
            d[a], d[a + 1] = d[a + 1], d[a]
    return bytes(d)

def main():
    a = sys.argv[1:]
    src = 'le'
    if '--from' in a:
        i = a.index('--from'); src = a[i + 1]; del a[i:i + 2]
    inp, outp = a
    ext = inp.rsplit('.', 1)[-1].lower()
    raw = open(inp, 'rb').read()
    data = convert_tga(raw, src) if ext == 'tga' else convert(raw, ext, src)
    open(outp, 'wb').write(data)

if __name__ == '__main__':
    main()
