// Save files in the cartridge FlashRAM (128 KB, erased and written in sectors of 16 KB).
//
// The image is a log of records, each at a multiple of 16 bytes:
//   header  magic, file name, sequence number, sizes, CRC-32 of the data and of the header itself
//   data    the file, compressed
//   end     16 zero bytes after the last record, where a scan stops
// There is no directory: at boot the log is scanned, and a file is the record with its name and the highest sequence
// number whose CRCs check. Writing a file appends its new version and leaves the previous one where it was: if the power
// goes off half way, the new record fails its CRC and the previous version is still there. Only when the log is full are
// the live files packed together first (a rewrite of the image a power cut can damage). An append also rewrites the
// sector it starts in, which holds earlier records: FlashRAM has no smaller erase.
//
// ramfs_n64.cpp is its only user.
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "savefs_n64.h"

#include <libdragon.h>

#define RECORD_MAGIC 0x47325356u /* "G2SV" */
#define MAX_FILES    24          /* 8 profiles, 8 saved games, the high scores and room to spare */
#define CHUNK_SIZE   16384
#define DELETED      0xFFFFFFFFu /* usize of a record that removes the file */
#define ALIGN16(x)   (((x) + 15u) & ~15u)
#define PIECE        512

typedef struct {
    uint32_t magic;
    uint32_t seq;       /* the highest of a name is its current version */
    uint32_t csize;     /* compressed data after the header */
    uint32_t usize;     /* original file size */
    uint32_t data_crc;
    char name[40];      /* the engine's paths: rom:/player/plyslot0.dat */
    uint32_t header_crc;
} record_t;
_Static_assert(sizeof(record_t) == 64, "records must keep their data 16 byte aligned");

typedef struct {
    char name[40];
    uint32_t offset, csize, usize, seq;
} entry_t;

static entry_t g_files[MAX_FILES];
static int g_nfiles;
static uint32_t g_end;       /* where the next record goes */
static uint32_t g_next_seq = 1;
static uint32_t g_flash_size;

/* ---- LZ. Tokens: 0x00-0x7F literal run of (t+1) bytes; 0x80-0xFF match of ((t&0x7F)+3) bytes at distance d (2 bytes, LE) ---- */
#define LZ_MIN_MATCH 3
#define LZ_MAX_MATCH (0x7F + LZ_MIN_MATCH)
#define LZ_HASH_BITS 12
#define LZ_CHAIN     32

/* only needed while a file is being saved: the RAM is the limit of this port */
typedef struct {
    int32_t last[1 << LZ_HASH_BITS];
    int16_t prev[CHUNK_SIZE];
    uint8_t chunk[CHUNK_SIZE + CHUNK_SIZE / 64 + 16];
} lz_work_t;

static uint32_t lz_compress(const uint8_t *in, uint32_t len, uint8_t *out, lz_work_t *w)
{
    int32_t *last = w->last;
    int16_t *prev = w->prev;
    uint32_t i = 0, o = 0, lit_start = 0;

    memset(w->last, 0xff, sizeof(w->last));

#define FLUSH_LITERALS(end) do {                        \
        uint32_t s = lit_start;                         \
        while (s < (end)) {                             \
            uint32_t n = (end) - s; if (n > 128) n = 128; \
            out[o++] = n - 1;                           \
            memcpy(out + o, in + s, n); o += n; s += n; \
        }                                               \
    } while (0)

    while (i + LZ_MIN_MATCH <= len) {
        const uint32_t h = ((in[i] << 8) ^ (in[i + 1] << 4) ^ in[i + 2]) & ((1 << LZ_HASH_BITS) - 1);
        int32_t cand = last[h];
        uint32_t best = 0, dist = 0;
        int tries = LZ_CHAIN;

        prev[i] = cand;
        last[h] = i;

        if (i > 0 && in[i] == in[i - 1] && in[i + 1] == in[i - 1] && in[i + 2] == in[i - 1]) { /* run of one byte */
            uint32_t n = 0;
            while (i + n < len && n < LZ_MAX_MATCH && in[i + n] == in[i - 1]) n++;
            best = n; dist = 1;
        }
        while (cand >= 0 && i - cand <= 0xFFFF && tries-- > 0 && best < LZ_MAX_MATCH) {
            uint32_t n = 0;
            while (i + n < len && n < LZ_MAX_MATCH && in[cand + n] == in[i + n]) n++;
            if (n > best) { best = n; dist = i - cand; }
            cand = prev[cand];
        }

        if (best >= LZ_MIN_MATCH) {
            FLUSH_LITERALS(i);
            out[o++] = 0x80 | (best - LZ_MIN_MATCH);
            out[o++] = dist & 0xFF;
            out[o++] = dist >> 8;
            i += best;
            lit_start = i;
        } else {
            i++;
        }
    }
    FLUSH_LITERALS(len);
#undef FLUSH_LITERALS
    return o;
}

static int lz_decompress(const uint8_t *in, uint32_t len, uint8_t *out, uint32_t out_len)
{
    uint32_t i = 0, o = 0;

    while (i < len) {
        const uint8_t t = in[i++];
        if (t < 0x80) {
            const uint32_t n = t + 1;
            if (i + n > len || o + n > out_len) return -1;
            memcpy(out + o, in + i, n); i += n; o += n;
        } else {
            uint32_t n = (t & 0x7F) + LZ_MIN_MATCH, d;
            if (i + 2 > len) return -1;
            d = in[i] | (in[i + 1] << 8); i += 2;
            if (d == 0 || d > o || o + n > out_len) return -1;
            while (n--) { out[o] = out[o - d]; o++; } /* may overlap */
        }
    }
    return o == out_len ? 0 : -1;
}

/* ---- a file's data: chunks of [u16 stored size][u16 original size][data]; stored == original means not compressed ---- */
static uint8_t *pack_file(const uint8_t *data, uint32_t len, uint32_t *out_len)
{
    const uint32_t cap = len + (len / CHUNK_SIZE + 1) * 4;
    uint8_t *out = malloc(cap);
    lz_work_t *w = malloc(sizeof(*w));
    uint32_t o = 0;

    if (!out || !w) {
        free(out);
        free(w);
        return NULL;
    }
    for (uint32_t pos = 0; pos < len;) {
        const uint32_t n = len - pos < CHUNK_SIZE ? len - pos : CHUNK_SIZE;
        const uint32_t c = lz_compress(data + pos, n, w->chunk, w);
        const int packed = c < n;
        const uint32_t stored = packed ? c : n;
        out[o++] = stored >> 8; out[o++] = stored & 0xFF;
        out[o++] = n >> 8;      out[o++] = n & 0xFF;
        memcpy(out + o, packed ? w->chunk : data + pos, stored);
        o += stored; pos += n;
    }
    free(w);
    *out_len = o;
    return out;
}

static int unpack_file(const uint8_t *in, uint32_t len, uint8_t *dst, uint32_t dst_len)
{
    uint32_t i = 0, o = 0;

    while (i < len) {
        if (i + 4 > len) return -1;
        const uint32_t stored = ((uint32_t)in[i] << 8) | in[i + 1], n = ((uint32_t)in[i + 2] << 8) | in[i + 3];
        i += 4;
        if (i + stored > len || o + n > dst_len) return -1;
        if (stored == n) memcpy(dst + o, in + i, n);
        else if (lz_decompress(in + i, stored, dst + o, n) != 0) return -1;
        i += stored; o += n;
    }
    return o == dst_len ? 0 : -1;
}

/* ---- image ---- */
static uint32_t crc32_update(uint32_t c, const uint8_t *p, uint32_t len)
{
    static uint32_t table[256];

    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t t = i;
            for (int k = 0; k < 8; k++) t = (t & 1) ? 0xEDB88320u ^ (t >> 1) : t >> 1;
            table[i] = t;
        }
    }
    for (; len--; p++) c = table[(c ^ *p) & 0xFF] ^ (c >> 8);
    return c;
}

static uint32_t crc32(const void *p, uint32_t len) { return ~crc32_update(0xFFFFFFFFu, p, len); }
static uint32_t header_crc(const record_t *r) { return crc32(r, offsetof(record_t, header_crc)); }

/* any offset and destination: flashram_read() wants an even offset */
static void flash_copy(void *dst, uint32_t offset, uint32_t len)
{
    static uint8_t piece[PIECE + 16] __attribute__((aligned(16)));
    uint8_t *d = dst;

    while (len > 0) {
        const uint32_t odd = offset & 1, n = len < PIECE ? len : PIECE;
        flashram_read(piece, offset - odd, n + odd);
        memcpy(d, piece + odd, n);
        d += n; offset += n; len -= n;
    }
}

static uint32_t flash_crc32(uint32_t offset, uint32_t len)
{
    uint8_t buf[PIECE];
    uint32_t c = 0xFFFFFFFFu;

    while (len > 0) {
        const uint32_t n = len < PIECE ? len : PIECE;
        flash_copy(buf, offset, n);
        c = crc32_update(c, buf, n);
        offset += n; len -= n;
    }
    return ~c;
}

static int find_entry(const char *name)
{
    for (int i = 0; i < g_nfiles; i++)
        if (strcmp(g_files[i].name, name) == 0) return i;
    return -1;
}

static void forget_entry(int i)
{
    g_files[i] = g_files[--g_nfiles];
}

static void add_version(const record_t *r, uint32_t offset)
{
    int i = find_entry(r->name);
    if (i >= 0 && g_files[i].seq > r->seq) return;
    if (r->usize == DELETED) {
        if (i >= 0) forget_entry(i);
        return;
    }
    if (i < 0) {
        if (g_nfiles >= MAX_FILES) return;
        i = g_nfiles++;
    }
    memcpy(g_files[i].name, r->name, sizeof(r->name));
    g_files[i].offset = offset;
    g_files[i].csize = r->csize;
    g_files[i].usize = r->usize;
    g_files[i].seq = r->seq;
}

static void scan(void)
{
    uint32_t off = 0;

    g_nfiles = 0;
    while (off + sizeof(record_t) <= g_flash_size) {
        record_t r;
        flash_copy(&r, off, sizeof(r));
        if (r.magic != RECORD_MAGIC || r.header_crc != header_crc(&r) || r.csize > g_flash_size - off - sizeof(r)) break;
        if (r.name[sizeof(r.name) - 1] == 0 && r.data_crc == flash_crc32(off + sizeof(r), r.csize)) add_version(&r, off);
        /* a damaged record is ignored, but its header is good: the log goes on after it */
        if (r.seq >= g_next_seq) g_next_seq = r.seq + 1;
        off += ALIGN16(sizeof(r) + r.csize);
    }
    g_end = off;
}

/* Rewrites the live files from the start of the chip. Needs RAM for all of them. */
static int compact(void)
{
    uint32_t total = 16;
    for (int i = 0; i < g_nfiles; i++) total += ALIGN16(sizeof(record_t) + g_files[i].csize);
    uint8_t *buf = malloc(total);
    if (!buf) return -1;

    uint32_t o = 0;
    for (int i = 0; i < g_nfiles; i++) {
        const uint32_t size = sizeof(record_t) + g_files[i].csize;
        memset(buf + o, 0, ALIGN16(size));
        flash_copy(buf + o, g_files[i].offset, size);
        g_files[i].offset = o;
        o += ALIGN16(size);
    }
    memset(buf + o, 0, 16);
    const int r = flashram_write(buf, 0, o + 16);
    free(buf);
    if (r < 0) return -1;
    g_end = o;
    return 0;
}

/* Appends a record (and the end mark after it), packing the log first if it does not fit. */
static int append_record(const char *name, const uint8_t *cdata, uint32_t csize, uint32_t usize)
{
    const uint32_t need = ALIGN16(sizeof(record_t) + csize) + 16;

    if (g_end + need > g_flash_size) {
        if (compact() != 0) return -1;
        if (g_end + need > g_flash_size) return -1;
    }
    uint8_t *buf = calloc(1, need);
    if (!buf) return -1;

    record_t *r = (record_t *)buf;
    r->magic = RECORD_MAGIC;
    r->seq = g_next_seq++;
    r->csize = csize;
    r->usize = usize;
    r->data_crc = crc32(cdata, csize);
    strncpy(r->name, name, sizeof(r->name) - 1);
    r->header_crc = header_crc(r);
    if (csize) memcpy(buf + sizeof(record_t), cdata, csize);

    const uint32_t offset = g_end;
    const record_t copy = *r;
    const int w = flashram_write(buf, offset, need);
    free(buf);
    if (w < 0) return -1;
    add_version(&copy, offset);
    g_end = offset + ALIGN16(sizeof(record_t) + csize);
    return 0;
}

/* ---- API ---- */
int gta2_savefs_init(void)
{
    flashram_info_t info;
    if (!flashram_init(NULL, &info)) {
        debugf("savefs: no FlashRAM, saving disabled\n");
        return 0;
    }
    g_flash_size = info.total_size;
    scan();
    debugf("savefs: %d files, log %lu / %lu bytes\n", g_nfiles, (unsigned long)g_end, (unsigned long)g_flash_size);
    return 1;
}

int gta2_savefs_count(void) { return g_nfiles; }
uint32_t gta2_savefs_used(void) { return g_end; }
const char *gta2_savefs_name(int i) { return g_files[i].name; }
uint32_t gta2_savefs_size(int i) { return g_files[i].usize; }

int gta2_savefs_load(const char *name, void *dst, uint32_t len)
{
    const int i = find_entry(name);
    if (i < 0 || g_files[i].usize != len) return -1;
    uint8_t *c = malloc(g_files[i].csize ? g_files[i].csize : 1);
    if (!c) return -1;
    flash_copy(c, g_files[i].offset + sizeof(record_t), g_files[i].csize);
    const int r = unpack_file(c, g_files[i].csize, dst, len);
    free(c);
    return r;
}

int gta2_savefs_store(const char *name, const void *data, uint32_t len)
{
    if (strlen(name) >= sizeof(((record_t *)0)->name)) return -1;
    if (find_entry(name) < 0 && g_nfiles >= MAX_FILES) return -1;
    uint32_t csize = 0;
    uint8_t *c = pack_file(data, len, &csize);
    if (!c) return -1;
    const int r = append_record(name, c, csize, len);
    free(c);
    return r;
}

int gta2_savefs_remove(const char *name)
{
    if (find_entry(name) < 0) return 0;
    return append_record(name, NULL, 0, DELETED);
}
