#!/usr/bin/env python3
"""Prepares a language's .gxt: adds the N64 menu text.

usage: lang_gxt.py <input.gxt> <output.gxt> <e|s> <menu.tsv>

The GTA 2 .gxt format (PC, little-endian): 'GBL'+language+version(u16=100), then the chunks
TKEY (12 bytes per text: u32 offset + 8-char key) and TDAT (zero-terminated UTF-16LE texts).
The engine looks keys up with bsearch, so they stay sorted.
Accented letters are stored as the font's GLYPH CODES (128..176), as in s.gxt.
"""
import struct, sys

# Latin-1 -> font glyph code (same table as the engine: kExtendedWesternCharMap)
GLYPH = {}
for i, ch in enumerate("ÀÁÂÄÆÇÈÉÊËÌÍÎÏÒÓÔÖÙÚÛÜß"):          # 128..150
    GLYPH[ch] = 128 + i
for i, ch in enumerate("àáâäæçèéêëìíîïòóôöùúûü"):          # 151..172
    GLYPH[ch] = 151 + i
GLYPH.update({'Ñ': 173, 'ñ': 174, '¿': 175, '¡': 176})

MENU_COLUMN = {'e': 1, 's': 2}


def enc(t):
    out = []
    for ch in t:
        o = ord(ch)
        if o < 128 or o >= 0x2000:
            out.append(ch)
        elif ch in GLYPH:
            out.append(chr(GLYPH[ch]))
        else:
            sys.stderr.write("WARNING: character with no glyph %r in %r\n" % (ch, t[:40]))
            out.append('?')
    return ''.join(out)


def read_tsv(path):
    rows = []
    for ln in open(path, encoding='utf-8'):
        ln = ln.rstrip('\r\n')
        if ln and not ln.startswith('#'):
            rows.append(ln.split('\t'))
    return rows


def read_gxt(path):
    d = open(path, 'rb').read()
    assert d[:3] == b'GBL' and struct.unpack('<H', d[4:6])[0] == 100, "does not look like a GTA 2 .gxt"
    o = 6
    ch = {}
    while o + 8 <= len(d):
        t = d[o:o + 4]
        n = struct.unpack('<I', d[o + 4:o + 8])[0]
        ch[t] = d[o + 8:o + 8 + n]
        o += 8 + n
    tkey, tdat = ch[b'TKEY'], ch[b'TDAT']
    texts = {}
    for i in range(0, len(tkey), 12):
        off = struct.unpack('<I', tkey[i:i + 4])[0]
        key = tkey[i + 4:i + 12].split(b'\0')[0].decode('ascii')
        e = off
        while tdat[e:e + 2] != b'\0\0' or (e - off) % 2:
            e += 1
        texts[key] = tdat[off:e].decode('utf-16le')
    return texts


def main():
    src, dst, lang, menu = sys.argv[1:5]
    texts = read_gxt(src)
    for row in read_tsv(menu):
        assert len(row[0]) <= 7, "key too long, the engine does not find it: " + row[0]
        texts[row[0]] = enc(row[MENU_COLUMN[lang]])
    data = bytearray()
    ktab = bytearray()
    for key in sorted(texts, key=lambda k: k.encode('ascii')):
        ktab += struct.pack('<I', len(data)) + key.encode('ascii').ljust(8, b'\0')
        data += texts[key].encode('utf-16le') + b'\0\0'
    out = b'GBL' + lang.upper().encode('ascii') + struct.pack('<H', 100)
    out += b'TKEY' + struct.pack('<I', len(ktab)) + bytes(ktab)
    out += b'TDAT' + struct.pack('<I', len(data)) + bytes(data)
    open(dst, 'wb').write(out)
    print("%s.gxt: %d texts, %d bytes" % (lang, len(texts), len(out)))


if __name__ == '__main__':
    main()
