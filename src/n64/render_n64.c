/* N64 renderer (stage 3): draws directly on the RDP, via rdpq, the command list the gta2_re engine generates.
 *
 * The engine already projects everything into screen space (x,y in 640x480), so there is no 3D here: only 2D triangles
 * with Gouraud colour + texture. GTA 2 textures are 8-bit paletted (256 px pages); here each texture
 * is converted ON DEMAND to RGBA16, reduced with AREA AVERAGING (not point sampling) to at most 2048
 * texels (= the whole 4 KB of TMEM), and kept in a simple cache. Colour 0 is transparent; transparent
 * edges inherit the neighbours' colour so the bilinear filter does not darken outlines.
 *
 * Render "lab" (controller buttons, edge-triggered):
 *   L        toggles the bilinear filter
 *   R        perspective: 0 off, 1 on for everything (engine w), 2 on only for triangles with very different w
            between vertices (ratio >= PERSP_MIN_RATIO: posts, walls), the ground stays without perspective.
 *   C-up     debug: 0 normal, 1 hides translucents, 2 translucents only, 3 flat colour per texture,
            4 colour by the triangle's w ratio (green <1.1, yellow <1.25, orange <1.5, red >=1.5)
 *
 * Indicators: RAM bar at the top, blinking square (live loop), joypad buttons (bottom left),
 * option state (bottom right: filter, perspective, debug).
 */
#include <libdragon.h>
#include <malloc.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "render/n64tex.h"
#include <rdpq_tex.h>
#include <rdpq_text.h>
#include <rdpq_font.h>

typedef struct { float x, y, w, u, v; uint32_t rgba; } FVert;                              /* = FrameVertex (render/frame.h) */
typedef struct { uint32_t kind, texture, blend, first, count, colour; int32_t rect[4]; } FCmd; /* = FrameCommand */

#define CACHE_N     N64_CACHE_N   /* capture at draw time: copies from the same frame must coexist (192 x 4 KB = 768 KB; measured peak ~135 in use) */
#define SLOT_U16    2048          /* 4096 bytes per entry = whole TMEM in RGBA16 */
#define MAX_TEXELS  2048

typedef struct { const N64Tex *owner; uint16_t gen, tw, th, pw; uint32_t used; float req; uint8_t empty, add, text; } CEnt; /* empty = no visible texel */ /* used = g_frames+1 of the last use (0 = never); req = scale requested at conversion */
#define SCALE_MIN   0.25f  /* smallest reduction accepted by screen scale */
#define SCALE_UP    1.25f  /* reconverts (larger) if the texture starts appearing 25% larger on screen */
#define CACHE_GUARD 3  /* frames a slot stays protected after use: the RDP reads the texture later, asynchronously */
static CEnt g_c[CACHE_N];
static uint16_t *g_cbuf;
static int g_next;
static int g_active;
static uint32_t g_frames;

/* Per-slot upload blocks (see N64_UPLOAD_BLOCKS further down). DEFERRED release: a block may already be in the RSP/RDP queue, so
 * it is only released 4 frames after being dropped (or after rspq_wait if the list fills up). Called when reconverting the slot and when the owner
 * texture is forgotten (level change), so as not to leave hundreds of small blocks scattered around the heap (fragmentation). */
#define UBLK_PEND_N 64
static rspq_block_t *g_ublk_pend[UBLK_PEND_N];
static uint32_t g_ublk_pend_f[UBLK_PEND_N];
static int g_ublk_npend;
static rspq_block_t *g_ublk[CACHE_N];
static uint8_t g_ublk_n[CACHE_N];
static int g_ublk_live;
static void ublk_release(int slot)
{
    rspq_block_t *b = g_ublk[slot];
    g_ublk_n[slot] = 0;
    if (!b) return;
    g_ublk[slot] = NULL;
    --g_ublk_live;
    if (g_ublk_npend >= UBLK_PEND_N) {
        rspq_wait();
        for (int i = 0; i < g_ublk_npend; ++i) rspq_block_free(g_ublk_pend[i]);
        g_ublk_npend = 0;
    }
    g_ublk_pend[g_ublk_npend] = b;
    g_ublk_pend_f[g_ublk_npend] = g_frames;
    ++g_ublk_npend;
}
/* 1 if there is enough free memory to record a block (rdpq aborts the game with 'Out of memory' if it runs out mid-recording) */
/* RESERVE for the engine: upload/text blocks are only a speed cache and must not eat the headroom the game needs to allocate
 * at run time (on-demand textures, scripts, sounds): without this the heap slowly filled up and the engine aborted with 'Out of memory'. */
#ifndef N64_HEAP_RESERVE_KB
#define N64_HEAP_RESERVE_KB 320
#endif
#define UBLK_MAX_LIVE 128 /* upload blocks alive at the same time (~2.4 KB each) */
extern char *__heap_top;
static int heap_has_room(void)
{
    if ((long)(__heap_top - (char *)sbrk(0)) < (long)N64_HEAP_RESERVE_KB * 1024) return 0; /* the headroom not yet requested from the system (past the end of the current heap) */
    void *p = malloc(24 * 1024);
    if (!p) return 0;
    free(p);
    return 1;
}
static void ublk_reap(void)
{
    for (int i = 0; i < g_ublk_npend; ) {
        if ((uint32_t)(g_frames - g_ublk_pend_f[i]) >= 4u) {
            rspq_block_free(g_ublk_pend[i]);
            --g_ublk_npend;
            g_ublk_pend[i] = g_ublk_pend[g_ublk_npend];
            g_ublk_pend_f[i] = g_ublk_pend_f[g_ublk_npend];
        } else ++i;
    }
}

/* lab options */
#ifndef N64_DEBUG_OVERLAY
#define N64_DEBUG_OVERLAY 0 /* 1 = FPS and free RAM on screen */
#endif
#ifndef N64_HUD_SHARP
#define N64_HUD_SHARP 1
#endif
static int g_tex_hud; /* 1 while the game draws the HUD (frame_n64.cpp) */
static int g_tex_text; /* 1 while that HUD draws text */
static int g_conv_text; /* the texture being converted is a letter at half size */
void gta2_n64_tex_set_text(int on) { g_tex_text = N64_HUD_SHARP && on; }
void gta2_n64_tex_set_hud(int on) { g_tex_hud = N64_HUD_SHARP && on; }

/* audio (audio_n64.c) */
extern void gta2_n64_audio_init(void);
extern void gta2_n64_audio_poll(void);

int gta2_n64_gfx_active(void) { return g_active; }

void gta2_n64_tex_forget(const N64Tex *t)
{
    if (t && t->slot >= 0 && t->slot < CACHE_N && g_c[t->slot].owner == t) { g_c[t->slot].owner = 0; ublk_release(t->slot); }
}

/* returns the cache entry for the texture (converting if needed); -1 if there is no data */
/* scale = screen pixels per texel (<= 1): the texture is reduced with area averaging to the size it appears at */
/* ROM reads (pixels and palettes mapped on the cartridge): the CPU only reads ROM correctly in aligned 32-bit
 * words, and never during a PI DMA. A lone byte comes back from the wrong word position (broken sprites).
 * libdragon's io_read() waits for the PI to be free and reads 32 bits; from there the bytes are extracted at the right position. */
static inline int in_rom(const void *p) { const uintptr_t a = (uintptr_t)p; return a >= 0xB0000000u && a < 0xC0000000u; }
static uint8_t g_srcbuf[256 * 256 + 16] __attribute__((aligned(16))); /* RAM copy of the source rows (stride 256, like the .sty pages) */
/* copies len bytes from src (ROM or RAM) to dst; on ROM reads only aligned 32-bit words, via io_read */
void gta2_n64_rom_copy(void *dst, const void *src, unsigned len)
{
    if (!in_rom(src)) { memcpy(dst, src, len); return; }
    uint8_t *o = (uint8_t *)dst;
    const uint32_t pi = (uint32_t)((uintptr_t)src & 0x1FFFFFFFu);
    uint32_t p = pi & ~3u;
    unsigned k = 0;
    int j = (int)(pi & 3u);
    while (k < len) {
        const uint32_t v = io_read(p);
        for (; j < 4 && k < len; ++j) o[k++] = (uint8_t)(v >> (24 - 8 * j));
        j = 0;
        p += 4;
    }
}

/* reads big-endian 32 bits from ANY ROM address (physical). io_read requires 4-byte alignment, and DragonFS files
 * are only 2-byte aligned: the .sty's ROM position changes when other files come and go (e.g.: frontend removed). */
static inline uint32_t rom_rd32_any(uint32_t p)
{
    const uint32_t k = p & 3u;
    if (k == 0) return io_read(p);
    const uint32_t a = p - k;
    const uint32_t lo = io_read(a), hi = io_read(a + 4);
    return (lo << (8 * k)) | (hi >> (32 - 8 * k));
}

static const uint8_t *src_pixels(const N64Tex *t, int w, int h)
{
    if (!in_rom(t->pix)) return t->pix;
    if (w > 256) w = 256;
    if (h > 256) h = 256;
    /* PI DMA instead of io_read word by word. The destination keeps the same alignment (mod 8) as the ROM,
     * a dma_read requirement (parity) and the fastest path. Rows with stride 256 in ROM and in the copy. */
    const uint32_t pi = (uint32_t)((uintptr_t)t->pix & 0x1FFFFFFFu);
    uint8_t *base = g_srcbuf + (pi & 7u);
    const unsigned span = (unsigned)(h - 1) * 256u + (unsigned)w; /* from the 1st byte of the 1st row to the last of the last */
    data_cache_hit_writeback_invalidate(base, span); /* the CPU must not have stale lines of this range in cache */
    if (w >= 192) {
        dma_read(base, pi, span); /* nearly whole rows: a single DMA */
    } else {
        for (int y = 0; y < h; ++y) dma_read(base + y * 256, pi + (uint32_t)y * 256u, (unsigned long)w); /* only the w bytes of each row */
    }
    return base;
}

/* division-free average: 8.24 fixed-point inverse. Tested for all possible sums (0..255*n):
 * result identical to s/n for n <= 266; above that uses normal division. */
#define INV_N 267
static uint32_t g_inv[INV_N];
static inline unsigned avg_div(unsigned s, unsigned n)
{
    if (n < INV_N) {
        if (!g_inv[1]) for (unsigned i = 1; i < INV_N; ++i) g_inv[i] = ((1u << 24) + i - 1) / i;
        return (unsigned)(((uint64_t)s * g_inv[n]) >> 24);
    }
    return s / n;
}
/* last palette read from ROM (it never changes in ROM): avoids 256 PI reads when the next texture uses the same one */
#define PAL_CACHE 16 /* palettes already read from ROM (each read = 256 cartridge bus accesses) */
static struct { const uint32_t *src; uint32_t rgb[256]; } g_palc[PAL_CACHE];
static int g_palc_next;

/* additive: texture drawn with additive blending on PC. On N64 it becomes plain transparency (the RDP overflows the sum),
 * so the near-black pixels -- which did not show up in the sum -- become transparent at conversion. */
#define ADD_CUT 48u /* r+g+b below this = "black" of an additive texture */
/* BLOCK UPLOAD: each slot's upload (rdpq_tex_upload, ~40 us of CPU each, ~130 per frame) is recorded once in an RSP block
 * and then only executed (1 command). Records the 2nd time the slot is used with the same content; the block is released when the slot is
 * reconverted (the slot is only reused after CACHE_GUARD frames without use, so the RDP no longer executes it). 0 = off (back to the old way). */
#ifndef N64_UPLOAD_BLOCKS
#define N64_UPLOAD_BLOCKS 1
#endif
static int s_rec;                                /* 1 while recording a larger block (static text): recording does not nest */
static uint32_t g_slot_ver[CACHE_N];            /* bumped on each conversion: identifies the slot's current content (to skip the upload) */
extern const uint32_t *gta2_n64_palette_ptr(int id);
/* texture with no data (no descriptor / pixels / palette, or size 0): draws untextured */
static int texfail(const N64Tex *t, int why)
{
    (void)t; (void)why;
    return -1;
}
static int tex_slot(N64Tex *t, float scale, int additive)
{
    if (!t) return texfail(t, 0);
    if (t->palid >= 0) { /* the palette may have been registered (or changed) after the texture */
        const uint32_t *cur = gta2_n64_palette_ptr(t->palid);
        if (cur && cur != t->pal) {
            t->pal = cur;
            if (t->slot >= 0 && t->slot < CACHE_N && g_c[t->slot].owner == t) g_c[t->slot].owner = 0;
        }
    }
    if (!t->pix) return texfail(t, 1);
    if (!t->pal) return texfail(t, 2);
    if (t->w == 0 || t->h == 0) return texfail(t, 3);
    if (t->slot >= 0 && g_c[t->slot].owner == t && g_c[t->slot].gen == t->gen && (g_c[t->slot].add != (uint8_t)additive || g_c[t->slot].text != (uint8_t)g_conv_text))
        g_c[t->slot].owner = 0; /* same texture requested in the other mode (additive x normal, letter x sprite): reconverts */
    if (t->slot >= 0 && g_c[t->slot].owner == t && g_c[t->slot].gen == t->gen) {
        if (scale <= g_c[t->slot].req * SCALE_UP) { g_c[t->slot].used = g_frames + 1; return t->slot; }
        g_c[t->slot].owner = 0; /* appears larger than at conversion: reconverts at higher resolution (only grows) */
    }
    if (!g_cbuf) {
        g_cbuf = (uint16_t *)memalign(64, (size_t)CACHE_N * SLOT_U16 * 2);
        if (!g_cbuf) return -1;
        memset(g_c, 0, sizeof(g_c));
    }
    const int w = t->w, h = t->h;
    int tw = w, th = h;
    if (scale < 0.95f) { /* reduces to the on-screen size (sprites: ~half; blocks: ~44 of 64) */
        tw = (int)((float)w * scale + 0.5f);
        th = (int)((float)h * scale + 0.5f);
        if (tw < 1) tw = 1;
        if (th < 1) th = 1;
    }
    if (g_conv_text) { /* two texels per pixel, starting at the corner of the letter: the same letter always looks the same */
        tw = (w + 1) / 2;
        th = (h + 1) / 2;
    }
    if (tw * th > MAX_TEXELS) { /* TMEM limit (4 KB in RGBA16) */
        const float f = sqrtf((float)MAX_TEXELS / (float)(tw * th));
        tw = (int)((float)tw * f) & ~3;
        if (tw < 4) tw = 4;
        th = (int)((float)th * f + 0.5f);
        if (th < 1) th = 1;
    }
    if (tw > w) tw = w;
    if (th > h) th = h;
    if (tw > 256) tw = 256; /* limit of the xs[] array (.sty pages are 256 px) */
    int pw = (tw + 3) & ~3;
    while (pw * th > MAX_TEXELS && th > 1) --th;

    /* picks a slot the RDP will no longer read: never used or unused in the last CACHE_GUARD frames */
    int slot = -1;
    for (int i = 0; i < CACHE_N; ++i) {
        const int s = (g_next + i) % CACHE_N;
        if (g_c[s].used == 0 || (g_frames + 1) - g_c[s].used >= CACHE_GUARD) { slot = s; break; }
    }
    if (slot < 0) { /* all recently in use: uses one not captured in THIS frame, after waiting for the RDP */
        for (int i = 0; i < CACHE_N; ++i) {
            const int s = (g_next + i) % CACHE_N;
            if (g_c[s].used != g_frames + 1) { slot = s; break; }
        }
        if (slot < 0) { return -1; } /* cache full with this frame's textures only: draws untextured */

        rspq_wait();
    }
    g_next = (slot + 1) % CACHE_N;
    uint16_t *dst = g_cbuf + (size_t)slot * SLOT_U16;

    uint32_t prgb_ram[256];
    const uint32_t *prgb;
    const uint32_t *pal = t->pal;
    if (in_rom(pal)) {
        int pk = -1;
        for (int i = 0; i < PAL_CACHE; ++i) if (g_palc[i].src == pal) { pk = i; break; }
        if (pk < 0) {
            pk = g_palc_next;
            g_palc_next = (pk + 1) % PAL_CACHE;
            for (int i = 0; i < 256; ++i) g_palc[pk].rgb[i] = rom_rd32_any((uint32_t)((uintptr_t)&pal[i * 64] & 0x1FFFFFFFu));
            g_palc[pk].src = pal;

        }
        prgb = g_palc[pk].rgb;
    } else {
        for (int i = 0; i < 256; ++i) prgb_ram[i] = pal[i * 64]; /* in RAM the game may change the palette: always read */
        prgb = prgb_ram;
    }
    const uint8_t *src = src_pixels(t, w, h);
    uint16_t xs[260];
    for (int x = 0; x <= tw; ++x) xs[x] = (uint16_t)(g_conv_text ? (x * 2 < w ? x * 2 : w) : x * w / tw);

    int any_clear = 0, any_opaque = 0;
    if (tw == w && th == h) { /* no reduction: each texel is just a lookup in a 256-entry table (result identical to the general path) */
        uint16_t p16[256];
        p16[0] = 0;
        for (int i = 1; i < 256; ++i) {
            const uint32_t c = prgb[i];
            const unsigned r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            p16[i] = ((additive && r + g + b < ADD_CUT) || (i == 1 && t->key1)) ? 0 : (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
        }
        unsigned orv = 0, andv = 1;
        for (int y = 0; y < th; ++y) {
            const uint8_t *row = src + (size_t)y * 256;
            uint16_t *o = dst + y * pw;
            for (int x = 0; x < tw; ++x) {
                const uint16_t v = p16[row[x]];
                o[x] = v;
                orv |= v;
                andv &= v;
            }
            for (int x = tw; x < pw; ++x) o[x] = 0;
        }
        any_opaque = (orv & 1) != 0;
        any_clear = !(andv & 1);

        goto conv_done_loop;
    }
    for (int y = 0; y < th; ++y) {
        const int y0 = g_conv_text ? y * 2 : y * h / th;
        int y1 = g_conv_text ? (y * 2 + 2 < h ? y * 2 + 2 : h) : (y + 1) * h / th;
        if (y1 <= y0) y1 = y0 + 1;
        uint16_t *o = dst + y * pw;
        for (int x = 0; x < tw; ++x) {
            const int x0 = xs[x];
            int x1 = xs[x + 1];
            if (x1 <= x0) x1 = x0 + 1;
            unsigned sr = 0, sg = 0, sb = 0, n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t *row = src + (size_t)yy * 256;
                for (int xx = x0; xx < x1; ++xx) {
                    const uint8_t idx = row[xx];
                    if (idx && !(idx == 1 && t->key1)) {
                        const uint32_t c = prgb[idx];
                        sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255; ++n;
                    }
                }
            }
            const unsigned area = (unsigned)((x1 - x0) * (y1 - y0));
            unsigned r = 0, g = 0, b = 0;
            int vis = (n && n * 3 >= area);
            if (vis) {
                r = avg_div(sr, n); g = avg_div(sg, n); b = avg_div(sb, n);
                if (additive && r + g + b < ADD_CUT) vis = 0;
                if (g_conv_text) { /* lighter colours: more contrast over the map, black stays black */
                    r = 255 - (255 - r) * (255 - r) / 255;
                    g = 255 - (255 - g) * (255 - g) / 255;
                    b = 255 - (255 - b) * (255 - b) / 255;
                }
            }
            if (vis) {
                o[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
                any_opaque = 1;
            } else {
                o[x] = 0;
                any_clear = 1;
            }
        }
        for (int x = tw; x < pw; ++x) o[x] = 0;
    }
conv_done_loop:
    /* transparent texels inherit an opaque neighbour's colour (alpha stays 0): bilinear does not darken edges */
    if (any_clear) {
        for (int y = 0; y < th; ++y) {
            uint16_t *o = dst + y * pw;
            for (int x = 0; x < tw; ++x) {
                if (o[x] & 1) continue;
                uint16_t nb = 0;
                if (x > 0 && (o[x - 1] & 1)) nb = o[x - 1];
                else if (x + 1 < tw && (o[x + 1] & 1)) nb = o[x + 1];
                else if (y > 0 && (dst[(y - 1) * pw + x] & 1)) nb = dst[(y - 1) * pw + x];
                else if (y + 1 < th && (dst[(y + 1) * pw + x] & 1)) nb = dst[(y + 1) * pw + x];
                if (nb) o[x] = (uint16_t)(nb & ~1);
            }
        }
    }
    data_cache_hit_writeback(dst, (unsigned long)pw * th * 2);
    ++g_slot_ver[slot];
    ublk_release(slot);
    g_c[slot].owner = t; g_c[slot].gen = t->gen; g_c[slot].tw = (uint16_t)tw; g_c[slot].th = (uint16_t)th; g_c[slot].pw = (uint16_t)pw;
    g_c[slot].used = g_frames + 1;
    g_c[slot].req = scale;
    g_c[slot].empty = (uint8_t)!any_opaque;
    g_c[slot].add = (uint8_t)additive;
    g_c[slot].text = (uint8_t)g_conv_text;
    t->slot = (int16_t)slot;
    return slot;
}

static float SCR_W = 320.0f, SCR_H = 240.0f; /* set in present() from the real framebuffer (libdragon: 640x240) */

static const rdpq_trifmt_t kFmtTex = { .pos_offset = 0, .shade_offset = 2, .tex_offset = 6, .z_offset = -1 };
/* texture only, no per-vertex colour: for commands with all-white vertices (the RDP computes no colour gradient) */
static const rdpq_trifmt_t kFmtTexOnly = { .pos_offset = 0, .shade_offset = -1, .tex_offset = 6, .z_offset = -1 };

/* command scale: largest ratio (screen pixels / texels) among the triangles, limited to [SCALE_MIN, 1] */
static float cmd_scale(const FCmd *cm, const FVert *verts, const N64Tex *t, float sx, float sy)
{
    if (!t || t->w == 0 || t->h == 0) return 1.0f;
    const float texels = (float)t->w * (float)t->h;
    float best = 0.0f;
    for (uint32_t i = 0; i + 2 < cm->count; i += 3) {
        const FVert *p = &verts[cm->first + i];
        const float ax = (p[1].x - p[0].x) * sx, ay = (p[1].y - p[0].y) * sy;
        const float bx = (p[2].x - p[0].x) * sx, by = (p[2].y - p[0].y) * sy;
        const float a_scr = fabsf(ax * by - bx * ay);
        const float a_tex = fabsf((p[1].u - p[0].u) * (p[2].v - p[0].v) - (p[2].u - p[0].u) * (p[1].v - p[0].v)) * texels;
        if (a_scr < 1.0f || a_tex < 1.0f || !(a_scr < 4.0e6f)) continue;
        const float r = sqrtf(a_scr / a_tex);
        if (r > best) best = r;
    }
    if (best <= 0.0f) return 1.0f;
    if (best > 1.0f) best = 1.0f;
    if (best < SCALE_MIN) best = SCALE_MIN;
    return best;
}

/* Capture at draw time (called by gbh_n64.cpp/Draw): converts the texture NOW, before the engine
 * reuses pixels/palette/size for another sprite in the same frame, and returns a slot token (0 = no texture).
 * a_scr: triangle area on the reference screen (640x480); a_tex: area in texels. */
#define TEX_TOKEN_BASE 0x10000u
#define TEX_BOX 1u /* no texture, on purpose: the box behind a text (frame_n64.cpp) */
static float g_last_sx = 0.5f, g_last_sy = 0.5f; /* reference-screen -> framebuffer scale of the last present */
/* 1 if the token points to a texture with no visible texel (the draw would show nothing in any blend mode) */
int gta2_n64_tex_token_empty(uint32_t token)
{
    if (token < TEX_TOKEN_BASE || token >= TEX_TOKEN_BASE + CACHE_N) return 0;
    return g_c[token - TEX_TOKEN_BASE].empty;
}
uint32_t gta2_n64_tex_capture(N64Tex *t, float a_scr, float a_tex, int additive)
{
    if (!t) return 0;
    float scale = 1.0f;
    /* HUD: not reduced. Its thin details do not survive a reduction with 1-bit alpha; bilinear keeps the shapes when drawn smaller */
    if (!g_tex_hud && a_scr > 0.0f && a_tex >= 1.0f) {
        scale = sqrtf(a_scr * g_last_sx * g_last_sy / a_tex);
        if (scale > 1.0f) scale = 1.0f;
        if (scale < SCALE_MIN) scale = SCALE_MIN;
    }
    /* Letters at half size in the game: bilinear would skip every other texel and break their strokes. Reduced here instead,
     * and drawn one to one. Not in the menus: their fonts have strokes too thin for it. Text at any other size (the enlarged
     * HUD counters) stays a full-size texture, like the rest of the HUD. */
    extern int gta2_game_in_menu(void); /* game_entry.cpp */
    if (g_tex_text && !gta2_game_in_menu() && a_scr > 0.0f && a_tex >= 1.0f) {
        const float r = sqrtf(a_scr * g_last_sx * g_last_sy / a_tex);
        if (r > 0.45f && r < 0.55f) { g_conv_text = 1; scale = r; }
    }
    const int slot = tex_slot(t, scale, additive);
    g_conv_text = 0;
    return slot < 0 ? 0u : TEX_TOKEN_BASE + (uint32_t)slot;
}

uint32_t gta2_n64_frames(void) { return g_frames; }

static inline double ticks_ms(uint64_t t) { return (double)t * 1000.0 / (double)TICKS_PER_SECOND; }

static int s_persp = -1; /* current RDP perspective (-1 = unknown): on only for the triangles that need it; texture rectangle requires it off */

/* 6 consecutive vertices (2 triangles) forming an axis-aligned rectangle, with no rotation or perspective and with an
 * unmirrored texture: draws with ONE rdpq_texture_rectangle command instead of 2 rdpq_triangle (text, HUD, faces, signs).
 * Returns 1 if it handled the 6 vertices (drew or discarded for being off screen), 0 if it is not a rectangle (use triangles). */
static int s_pixel; /* the rectangle shows its texels one to one: see the letters in tex_slot */
static float s_tex_ofs; /* added to the texture coordinates: see 'menu_text' in draw_cmd */
static int try_texrect(const FVert *p, float sx, float sy, float ts, float tt, int snap)
{
    float X[6], Y[6], minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, wmin = 1e30f, wmax = 0.0f;
    for (int i = 0; i < 6; ++i) {
        X[i] = p[i].x * sx; Y[i] = p[i].y * sy;
        if (!(X[i] > -1500.0f && X[i] < 1500.0f && Y[i] > -1500.0f && Y[i] < 1500.0f)) return 0;
        if (X[i] < minx) minx = X[i];
        if (X[i] > maxx) maxx = X[i];
        if (Y[i] < miny) miny = Y[i];
        if (Y[i] > maxy) maxy = Y[i];
        if (p[i].w > wmax) wmax = p[i].w;
        if (p[i].w < wmin) wmin = p[i].w;
    }
    if (maxx - minx < 0.25f || maxy - miny < 0.25f) return 0;
    if (wmin > 0.0f && wmax < 1e20f && wmax > wmin * 1.001f) return 0; /* real perspective: triangles only */
    const float ex = 0.02f;
    float cu[4] = {0}, cv[4] = {0};
    int seen = 0;
    for (int i = 0; i < 6; ++i) {
        int col, row;
        if (fabsf(X[i] - minx) < ex) col = 0; else if (fabsf(X[i] - maxx) < ex) col = 1; else return 0;
        if (fabsf(Y[i] - miny) < ex) row = 0; else if (fabsf(Y[i] - maxy) < ex) row = 1; else return 0;
        const int c = col + 2 * row; /* 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right */
        if (seen & (1 << c)) {
            if (fabsf(cu[c] - p[i].u) > 0.002f || fabsf(cv[c] - p[i].v) > 0.002f) return 0;
        } else {
            cu[c] = p[i].u; cv[c] = p[i].v; seen |= 1 << c;
        }
    }
    if (seen != 15) return 0;
    const float ue = 0.002f;
    if (fabsf(cu[0] - cu[2]) > ue || fabsf(cu[1] - cu[3]) > ue || fabsf(cv[0] - cv[1]) > ue || fabsf(cv[2] - cv[3]) > ue) return 0; /* rotated/skewed */
    if (!(cu[1] > cu[0] + 0.0005f) || !(cv[2] > cv[0] + 0.0005f)) return 0; /* mirrored: leave it to the triangles */
    if (maxx < 0.0f || minx > SCR_W || maxy < 0.0f || miny > SCR_H) { return 1; }
    if (snap) { /* whole pixels: see 'hud' and 'tile' in draw_cmd */
        const float rx0 = floorf(minx + 0.5f), rx1 = floorf(maxx + 0.5f), ry0 = floorf(miny + 0.5f), ry1 = floorf(maxy + 0.5f);
        if (rx1 <= rx0 || ry1 <= ry0) return 0; /* would vanish: leave it to the triangles */
        minx = rx0; maxx = rx1; miny = ry0; maxy = ry1;
    }
    float s0 = cu[0] * ts + s_tex_ofs, s1 = cu[1] * ts + s_tex_ofs, t0 = cv[0] * tt + s_tex_ofs, t1 = cv[2] * tt + s_tex_ofs;
    if (s_pixel) {
        const float pw = floorf(s1 - s0 + 0.5f), ph = floorf(t1 - t0 + 0.5f);
        if (fabsf(maxx - minx - pw) <= 1.5f) maxx = minx + pw;
        if (fabsf(maxy - miny - ph) <= 1.5f) maxy = miny + ph;
    }
    const float dsdx = (s1 - s0) / (maxx - minx), dtdy = (t1 - t0) / (maxy - miny);
    float x0 = minx, x1 = maxx, y0 = miny, y1 = maxy;
    if (x0 < 0.0f) { s0 += (0.0f - x0) * dsdx; x0 = 0.0f; }
    if (y0 < 0.0f) { t0 += (0.0f - y0) * dtdy; y0 = 0.0f; }
    if (x1 > SCR_W) { s1 -= (x1 - SCR_W) * dsdx; x1 = SCR_W; }
    if (y1 > SCR_H) { t1 -= (y1 - SCR_H) * dtdy; y1 = SCR_H; }
    if (x1 - x0 < 0.25f || y1 - y0 < 0.25f) { return 1; }
    if (s_persp != 0) { rdpq_mode_persp(0); s_persp = 0; } /* rectangle + perspective on came out flat */
    rdpq_texture_rectangle_scaled(TILE0, x0, y0, x1, y1, s0, t0, s1, t1);

    return 1;
}

/* ---- drawing one command (used directly and inside recorded RSP blocks) ---- */
static int s_hud;
static int s_valid, s_tex = -1, s_blend = -1;      /* mode already set on the RDP */
static int s_up_slot = -1; static uint32_t s_up_ver; /* texture already in TMEM (slot + version): avoids resending */
#define PERSP_MIN_RATIO 1.25f

/* kind 3: menu image (gbh_BlitImage). The half-size copy (RGBA5551, RAM) comes from gbh_n64.cpp; rdpq_tex_blit splits it into
 * pieces that fit in TMEM. Source in pixels of the whole image (cm->rect), destination (cm->first, cm->count) in 640x480 space. */
extern const uint16_t *gta2_n64_image_get(int idx, int *w, int *h);
static void blit_img(const FCmd *cm, float sx, float sy)
{
    int hw = 0, hh = 0;
    const uint16_t *px = gta2_n64_image_get((int)cm->texture, &hw, &hh);
    if (!px || hw <= 0 || hh <= 0) return;
    const int l = cm->rect[0], t = cm->rect[1], r = cm->rect[2], b = cm->rect[3];
    if (r <= l || b <= t) return;
    const int s0 = l / 2, t0 = t / 2;
    int sw = (r - l + 1) / 2, sh = (b - t + 1) / 2;
    if (s0 + sw > hw) sw = hw - s0;
    if (t0 + sh > hh) sh = hh - t0;
    if (sw <= 0 || sh <= 0) return;
    surface_t surf = surface_make((void *)px, FMT_RGBA16, (uint16_t)hw, (uint16_t)hh, (uint16_t)(hw * 2));
    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_blitparms_t bp;
    memset(&bp, 0, sizeof(bp));
    bp.s0 = s0; bp.t0 = t0; bp.width = sw; bp.height = sh;
    bp.scale_x = (float)(r - l) * sx / (float)sw;
    bp.scale_y = (float)(b - t) * sy / (float)sh;
    bp.filtering = true;
    rdpq_tex_blit(&surf, (float)(int32_t)cm->first * sx, (float)(int32_t)cm->count * sy, &bp);
}

static void draw_cmd(const FCmd *cm, const FVert *verts, float sx, float sy, int IW, int IH)
{
    if (cm->kind == 1) { /* clear rectangle */
        int x0 = (int)(cm->rect[0] * sx), y0 = (int)(cm->rect[1] * sy);
        int x1 = (int)(cm->rect[2] * sx), y1 = (int)(cm->rect[3] * sy);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > IW) x1 = IW;
        if (y1 > IH) y1 = IH;
        if (x1 > x0 && y1 > y0) {
            rdpq_set_mode_fill(RGBA32(cm->colour & 0xFF, (cm->colour >> 8) & 0xFF, (cm->colour >> 16) & 0xFF, 255));
            rdpq_fill_rectangle(x0, y0, x1, y1);
            s_valid = 0; s_up_slot = -1; s_persp = -1; /* fill mode replaced the draw mode */
        }
        return;
    }
    if (cm->kind == 3) { blit_img(cm, sx, sy); s_valid = 0; s_up_slot = -1; s_persp = -1; return; } /* menu image */
    if (cm->kind != 0 || cm->count < 3) return;

    int slot;
    if (cm->texture >= TEX_TOKEN_BASE && cm->texture < TEX_TOKEN_BASE + CACHE_N) { /* captured at draw time */
        slot = (int)(cm->texture - TEX_TOKEN_BASE);
        if (slot >= 0) g_c[slot].used = g_frames + 1;
    } else if (cm->texture == TEX_BOX) {
        slot = -1;
    } else {
        N64Tex *tx = (N64Tex *)(uintptr_t)cm->texture;
        slot = tex_slot(tx, cmd_scale(cm, verts, tx, sx, sy), cm->blend == 2);
    }
    if (slot < 0 && cm->blend != 0 && cm->texture != TEX_BOX) return; /* no data and translucent: skip */

    /* HUD: fixed on screen, so whole pixels and rectangles (no sub-pixel positions to blur it) */
    const int hud = N64_HUD_SHARP && (cm->colour == 1 || cm->colour == 3);
    const int text = hud && slot >= 0 && g_c[slot].text; /* a letter reduced at capture: no filter */
    /* Map tiles on whole pixels too: an edge in mid-pixel can leave that pixel unpainted by both neighbours, a thin
     * black line. All of them round the same shared corners the same way, so rectangles and triangles fit together. */
    const int tile = cm->colour == 2;
    const int snap = hud || tile;
    /* type: 0 = untextured, 1 = texture x vertex colour, 2 = texture only (all vertices white and opaque) */
    int is_tex = slot >= 0 ? 1 : 0;
    if (is_tex) {
        int white = 1;
        for (uint32_t k = 0; k < cm->count && white; ++k) {
            const uint32_t d = verts[cm->first + k].rgba;
            if ((d & 0xFFFFFFu) != 0xFFFFFFu || (cm->blend == 1 && (d >> 24) != 0xFFu) || cm->blend == 2) white = 0;
        }
        if (white) is_tex = 2;
    }
    if (!s_valid || is_tex != s_tex || cm->blend != s_blend || hud + text != s_hud) {
        rdpq_set_mode_standard();
        rdpq_mode_persp(0); s_persp = 0; /* enabled triangle by triangle, only where w varies */
        rdpq_mode_filter(text ? FILTER_POINT : FILTER_BILINEAR);
        if (is_tex) {
            rdpq_mode_combiner(is_tex == 2 ? RDPQ_COMBINER_TEX : RDPQ_COMBINER_TEX_SHADE);
            /* HUD: drawn at about half its size, a hard alpha cut drops rows of the thin letters; blended instead */
            rdpq_mode_alphacompare(cm->blend == 0 && !hud ? 128 : 0);
        } else {
            rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
            rdpq_mode_alphacompare(0);
        }
        if (cm->blend == 1 || (hud && cm->blend == 0 && is_tex)) rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
        else if (cm->blend == 2) rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA))); /* PC additive: the RDP overflows the sum (rainbow), so it becomes 75% transparency */
        else                     rdpq_mode_blender(0);
        s_valid = 1; s_tex = is_tex; s_blend = cm->blend; s_hud = hud + text;

    }
    if (is_tex) {
        if (s_up_slot == slot && s_up_ver == g_slot_ver[slot]) {
            /* the same texture is already in TMEM */
        } else {
            const CEnt *e = &g_c[slot];
            surface_t surf = surface_make(g_cbuf + (size_t)slot * SLOT_U16, FMT_RGBA16, e->pw, e->th, e->pw * 2);
#if N64_UPLOAD_BLOCKS
            if (g_ublk[slot] && !s_rec) {
                rspq_block_run(g_ublk[slot]);

            } else if (!s_rec && g_ublk_n[slot] < 255 && ++g_ublk_n[slot] >= 2 && g_ublk_live < UBLK_MAX_LIVE && heap_has_room()) {
                rspq_block_begin();
                rdpq_tex_upload(TILE0, &surf, NULL);
                rspq_block_t *ub = rspq_block_end();
                if (ub) { g_ublk[slot] = ub; ++g_ublk_live; rspq_block_run(ub); }
                else rdpq_tex_upload(TILE0, &surf, NULL);
            } else
#endif
            rdpq_tex_upload(TILE0, &surf, NULL);
            s_up_slot = slot; s_up_ver = g_slot_ver[slot];

        }
    }

    const float ts = slot >= 0 ? (float)g_c[slot].tw : 1.0f, tt = slot >= 0 ? (float)g_c[slot].th : 1.0f;
    s_pixel = text;
    /* Menu text at half size: on whole texels the filter returns just one of every two, and thin strokes vanish. Half a texel
     * further it mixes the neighbours, so every stroke leaves a mark. */
    extern int gta2_game_in_menu(void); /* game_entry.cpp */
    const int menu_text = cm->colour == 3 && !text && gta2_game_in_menu();
    s_tex_ofs = menu_text ? 0.5f : 0.0f;
    for (uint32_t t = 0; t + 2 < cm->count; t += 3) {
        const FVert *p = &verts[cm->first + t];
        /* Not for other opaque quads: unlike map tiles, they are not on whole pixels (see 'tile') */
        if (is_tex == 2 && (cm->blend != 0 || snap) && t + 5 < cm->count) {
            if (try_texrect(p, sx, sy, ts, tt, snap)) { t += 3; continue; } /* +3 from the for = 6 vertices */
        }
        float v[3][9];
        int bad = 0;
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        float wmax = 0.0f, wmin = 1e30f;
        for (int k = 0; k < 3; ++k) {
            float x = p[k].x * sx, y = p[k].y * sy;
            if (!(x > -1500.0f && x < 1500.0f && y > -1500.0f && y < 1500.0f)) { bad = 1; break; }
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
            if (p[k].w > wmax) wmax = p[k].w;
            if (p[k].w < wmin) wmin = p[k].w;
        }
        if (bad || maxx < 0 || minx > SCR_W || maxy < 0 || miny > SCR_H) { continue; }
        const float wr = (wmin > 0.0f && wmax < 1e20f) ? wmax / wmin : 1.0f;
        const int persp_ok = wmin > 0.0f && wmax < 1e20f;
        for (int k = 0; k < 3; ++k) {
            const uint32_t d = p[k].rgba;
            v[k][0] = p[k].x * sx; v[k][1] = p[k].y * sy;
            if (snap) { v[k][0] = floorf(v[k][0] + 0.5f); v[k][1] = floorf(v[k][1] + 0.5f); }
            v[k][2] = (float)(d & 0xFF) / 255.0f;
            v[k][3] = (float)((d >> 8) & 0xFF) / 255.0f;
            v[k][4] = (float)((d >> 16) & 0xFF) / 255.0f;
            v[k][5] = (cm->blend == 0) ? 1.0f : (cm->blend == 2) ? 0.75f : (float)(d >> 24) / 255.0f; /* additive: the PC ignored alpha */
            const float iw = persp_ok ? p[k].w / wmax : 1.0f;      /* normalized: what matters is the ratio between vertices */
            const float pm = 1.0f;
            v[k][6] = p[k].u * ts * pm + s_tex_ofs; v[k][7] = p[k].v * tt * pm + s_tex_ofs; v[k][8] = iw;
        }
        if (is_tex) { /* perspective only where w really varies; with equal w the result is the same without it */
            const int want = (persp_ok && wr > 1.001f) ? 1 : 0;
            if (want != s_persp) { rdpq_mode_persp(want != 0); s_persp = want; }
        }

        
        if (is_tex == 2)  { rdpq_triangle(&kFmtTexOnly, v[0], v[1], v[2]); }
        else if (is_tex)  rdpq_triangle(&kFmtTex, v[0], v[1], v[2]);
        else              rdpq_triangle(&TRIFMT_SHADE, v[0], v[1], v[2]);
    }
}

/* ---- RSP blocks for static text (briefing etc.) ----
 * frame_n64.cpp marks the commands of a phrase replayed from the cache with rect[3] = index+1 and rect[2] = stamp. The 1st time
 * the drawing is recorded in an rspq_block (and executed); in the following frames only the block runs: zero CPU work
 * per letter/upload. The block is rebuilt if the phrase, the options, the screen scale or the content of any texture slot changes. */
#define BLK_N 8
#define BLK_DEP 48
typedef struct {
    rspq_block_t *blk;
    uint32_t stamp, nruns, sxb, syb;
    int ndep;
    const N64Tex *down[BLK_DEP];
    uint16_t dslot[BLK_DEP], dgen[BLK_DEP];
} Blk;
static Blk g_blk[BLK_N];

static void run_cmds(const FCmd *cm, int n, const FVert *verts, float sx, float sy, int IW, int IH)
{
    for (int i = 0; i < n; ++i) draw_cmd(&cm[i], verts, sx, sy, IW, IH);
}

static void blk_run(const FCmd *cm, int n, const FVert *verts, float sx, float sy, int IW, int IH)
{
    const uint32_t id = (uint32_t)cm->rect[3] - 1u;
    if (id >= BLK_N || n > BLK_DEP) { run_cmds(cm, n, verts, sx, sy, IW, IH); return; }
    Blk *b = &g_blk[id];
    const uint32_t stamp = (uint32_t)cm->rect[2];
        uint32_t sxb, syb;
    memcpy(&sxb, &sx, 4);
    memcpy(&syb, &sy, 4);
    int ok = b->blk && b->stamp == stamp && b->nruns == (uint32_t)n && b->sxb == sxb && b->syb == syb;
    if (ok) {
        for (int d = 0; d < b->ndep; ++d) {
            const CEnt *e = &g_c[b->dslot[d]];
            if (e->owner != b->down[d] || e->gen != b->dgen[d]) { ok = 0; break; }
        }
    }
    if (ok) {
        for (int d = 0; d < b->ndep; ++d) g_c[b->dslot[d]].used = g_frames + 1; /* keeps the slots protected */
        rspq_block_run(b->blk);
        s_valid = 0; s_up_slot = -1; s_persp = -1;

        return;
    }
    if (b->blk) { rspq_block_free(b->blk); b->blk = NULL; }
    /* only records if all commands use valid texture tokens (no conversion/wait inside the block) */
    int ndep = 0;
    int dslot[BLK_DEP];
    for (int i = 0; i < n; ++i) {
        const FCmd *c = &cm[i];
        if (c->kind != 0 || c->texture < TEX_TOKEN_BASE || c->texture >= TEX_TOKEN_BASE + CACHE_N) { run_cmds(cm, n, verts, sx, sy, IW, IH); return; }
        const int sl = (int)(c->texture - TEX_TOKEN_BASE);
        if (!g_c[sl].owner) { run_cmds(cm, n, verts, sx, sy, IW, IH); return; }
        int j = 0;
        while (j < ndep && dslot[j] != sl) ++j;
        if (j == ndep) dslot[ndep++] = sl;
    }
    if (!heap_has_room()) { run_cmds(cm, n, verts, sx, sy, IW, IH); return; } /* no free memory: draws directly */
    s_valid = 0; s_up_slot = -1; s_persp = -1;
    s_rec = 1;
    rspq_block_begin();
    run_cmds(cm, n, verts, sx, sy, IW, IH);
    rspq_block_t *nb = rspq_block_end();
    s_rec = 0;
    s_valid = 0; s_up_slot = -1; s_persp = -1;
    if (!nb) { return; } /* should not happen; without a block this frame goes without the text */
    b->blk = nb;
    b->stamp = stamp; b->nruns = (uint32_t)n; b->sxb = sxb; b->syb = syb; b->ndep = ndep;
    for (int d = 0; d < ndep; ++d) {
        b->dslot[d] = (uint16_t)dslot[d];
        b->down[d] = g_c[dslot[d]].owner;
        b->dgen[d] = g_c[dslot[d]].gen;
    }

    rspq_block_run(nb);
}

/* Quitting a game takes a while: its last frames, and those of the menu loading, must not replace the loading screen */
static int s_hold_until_menu;
void gta2_n64_hold_until_menu(void) { s_hold_until_menu = 1; }
void gta2_n64_release_hold(void) { s_hold_until_menu = 0; }

void gta2_n64_present(const void *cmds_, int cmdCount, const void *verts_, int vertCount, int refWidth)
{
    static int inited;
    if (!inited) {
        rdpq_init();
        gta2_n64_audio_init(); /* after rdpq_init: the mixer uses the RSP queue */
        inited = 1;
        g_active = 1;
    }
    ublk_reap();

    const FCmd *cmds = (const FCmd *)cmds_;
    const FVert *verts = (const FVert *)verts_;
    if (refWidth <= 0) refWidth = 640;

    gta2_n64_audio_poll();
    /* the engine presents empty frames while it starts: they would replace the loading screen with a black one */
    static int s_has_image;
    if (s_hold_until_menu) {
        extern int gta2_game_in_menu(void); /* game_entry.cpp */
        if (!gta2_game_in_menu()) return;
        s_hold_until_menu = 0;
        s_has_image = 0;
    }
    for (int i = 0; i < cmdCount && !s_has_image; ++i) s_has_image = cmds[i].kind == 0 && cmds[i].count >= 3 && cmds[i].texture != 0;
    if (!s_has_image) return;
    extern void gta2_n64_update_video_mode(void); /* main_n64.c */
    gta2_n64_update_video_mode();
    surface_t *disp = display_get();
    SCR_W = (float)disp->width;
    SCR_H = (float)disp->height;
    const int IW = (int)disp->width, IH = (int)disp->height;
    const float sx = SCR_W / (float)refWidth, sy = SCR_H / 480.0f;
    g_last_sx = sx; g_last_sy = sy;
    rdpq_attach(disp, NULL);
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
    rdpq_fill_rectangle(0, 0, IW, IH);

    s_valid = 0; s_tex = -1; s_blend = -1; s_up_slot = -1; s_persp = -1;
    for (int c = 0; c < cmdCount; ) {
        const FCmd *cm = &cmds[c];
        if (cm->kind == 0 && cm->rect[3] != 0) { /* marked static text: tries the RSP block */
            int n = 1;
            while (c + n < cmdCount && cmds[c + n].kind == 0 && cmds[c + n].rect[3] == cm->rect[3] && cmds[c + n].rect[2] == cm->rect[2]) ++n;
            blk_run(cm, n, verts, sx, sy, IW, IH);
            c += n;
            continue;
        }
        draw_cmd(cm, verts, sx, sy, IW, IH);
        ++c;
    }

#if N64_DEBUG_OVERLAY
    {
        extern int gta2_n64_debug_font(void); /* intro_n64.c */
        const struct mallinfo mi = mallinfo();
        const long unclaimed = (long)(__heap_top - (char *)sbrk(0));
        rdpq_set_mode_standard();
        extern void gta2_ramfs_status(int *flash, int *files, int *dirty, int *stored, int *failed, unsigned *lastSize, unsigned *lastPacked, unsigned *used); /* ramfs_n64.cpp */
        extern void gta2_n64_audio_status(unsigned *clip, unsigned *click, unsigned *rom, unsigned *late); /* audio_n64.c */
        unsigned sndClip, sndClick, sndRom, sndLate;
        gta2_n64_audio_status(&sndClip, &sndClick, &sndRom, &sndLate);
        extern void gta2_peds_status(int *walkers, int *limit, int *onScreen); /* Char_Pool.cpp */
        int pedWalkers, pedLimit, pedOnScreen;
        gta2_peds_status(&pedWalkers, &pedLimit, &pedOnScreen);
        int flash, files, dirty, stored, failed;
        unsigned lastSize, lastPacked, used;
        gta2_ramfs_status(&flash, &files, &dirty, &stored, &failed, &lastSize, &lastPacked, &used);
        rdpq_text_printf(NULL, gta2_n64_debug_font(), 24, 88, "%.1f FPS\nRAM free %ldK\n in heap %dK\n cmds %d\nflash %s f%d\n d%d ok%d err%d\nlast %uB>%uB\nlog %uK/128K\npeds %d/%d scr %d\nsnd c%u k%u r%u l%u",
                         display_get_fps(), (unclaimed + mi.fordblks) / 1024, mi.fordblks / 1024, cmdCount,
                         flash ? "YES" : "NO", files, dirty, stored, failed, lastSize, lastPacked, used / 1024, pedWalkers, pedLimit, pedOnScreen, sndClip, sndClick, sndRom, sndLate);
    }
#endif
    rdpq_detach_show();
    ++g_frames;
#ifdef N64_MEMLOG
    { static unsigned peak; unsigned n = 0, cur = 0, wasted = 0; for (int i = 0; i < CACHE_N; ++i) { if (g_c[i].used && g_frames - g_c[i].used < CACHE_GUARD) { ++n; wasted += 4096 - (unsigned)g_c[i].pw * g_c[i].th * 2; } if (g_c[i].used) ++cur; }
      if (n > peak) { peak = n; }
      if ((g_frames % 120) == 0) { unsigned nb = 0; for (int i = 0; i < CACHE_N; ++i) if (g_ublk[i]) ++nb; debugf("[tcache] inuse=%u peak=%u everused=%u wasted=%uKB ublk=%u live=%d headroom=%ldKB\n", n, peak, cur, wasted / 1024, nb, g_ublk_live, (long)(__heap_top - (char *)sbrk(0)) / 1024); } }
#endif
    gta2_n64_audio_poll();
}
