/* GTA 2 sound on N64: the effects the engine fires through the Miles layer (miles_n64.cpp) play on the
 * libdragon mixer (mixing on the RSP). The samples stay in ROM (mapped .raw bank, already big-endian) and each channel reads
 * only the range being played, via DMA. None of the bank takes up RAM.
 *
 * Supports mono PCM: 16-bit (the effects) and 8-bit unsigned (the police radio, tools/sfx_share.py leaves those bytes as they are).
 * Stereo formats are ignored. Sounds requested before audio starts (gta2_n64_audio_init) are ignored.
 */
#include <libdragon.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>

#define NVOICE      8     /* mixer channels for effects (0..7); full = steals the oldest sound (formerly 14: fewer channels = less memory and less work) */
#define NSTREAM     2     /* Miles streams: 0 = radio, 1 = speech; channels NVOICE.. */
#define NWAVE       384   /* distinct waveforms (the bank has 320 entries) */
/* Mixer output. The resampler has no filter, so what does not fit under half this rate folds back as noise: at 12 kHz, 20 effects lost more
 * than 5% of their energy that way (one, mostly a 10.3 kHz tone, 87%). At 22 kHz, none lost more than 1%. */
#define OUT_FREQ    22050
/* CHAOS CAPS */
#define CAP_SFX_VOICES   5   /* effects playing at the same time, loops included (of NVOICE=8); full: see CAP_SFX_SHOTS */
#define CAP_SFX_SHOTS    3   /* with that many playing, effects that end by themselves stop adding up: a new one replaces the quietest of them if it is not quieter,
                              * or is dropped. Sirens and engines alone must not silence the gunshots */
#define CAP_SFX_VOL_LOUD 100 /* from this volume on an effect is near the player: CAP_SFX_STARTS does not drop it, it competes for a channel like any other */
#define CAP_SFX_VOL_MIN 20  /* new effect (not infinitely looping) with volume 0..127 below this is dropped: too far away to hear */
#define CAP_SFX_STARTS 3   /* at most this many new effects per 33 ms window (gunfire/explosion bursts); the rest are dropped */
#define CAP_SFX_UPD_MS  80  /* volume/pan/pitch of a running effect only update every this many ms (engines) */
#define CAP_MS(ms)      ((uint32_t)((ms) * (TICKS_PER_SECOND / 1000)))
static uint32_t g_ini_t0, g_ini_n;
#ifndef N64_DEBUG_OVERLAY
#define N64_DEBUG_OVERLAY 0
#endif
#if N64_DEBUG_OVERLAY
/* for the diagnostic overlay: what each kind of crackle would leave behind */
#define DIAG_CLICK_DELTA 16000       /* a jump this big between two output samples is a click, not sound */
#define DIAG_LATE_MS     200         /* without a poll for this long, the audio buffers (240 ms) ran dry or nearly so */
static unsigned g_diag_clip, g_diag_click, g_diag_rom, g_diag_late, g_diag_rom_len;
static int g_diag_prev;
static uint32_t g_diag_poll_t;
void gta2_n64_audio_status(unsigned *clip, unsigned *click, unsigned *rom, unsigned *late)
{
    *clip = g_diag_clip; *click = g_diag_click; *rom = g_diag_rom; *late = g_diag_late;
}
static void diag_block(const short *b, int n)
{
    for (int i = 0; i < n; ++i) {
        const int s = b[i * 2];
        if (s >= 32767 || s <= -32768) ++g_diag_clip;
        if (abs(s - g_diag_prev) >= DIAG_CLICK_DELTA) ++g_diag_click;
        g_diag_prev = s;
    }
}
#endif
/* HEADROOM against overflow: the mixer sums up to 10 channels in 16 bits and saturates (clipping = hiss). Each channel uses only a fraction
 * of the volume and, the more effects are playing, the lower the overall volume goes (ducking). Peaks of the sources (tools/stage_data.py,
 * audio_le2be.py): effects 0.78, radio 0.72. So on each side an effect peaks at 0.18 when centered and 0.35 when fully panned, and the radio
 * at 0.36. Clipping inside the mixer's resampler is prevented in those tools, not here. */
#define GAIN_SFX     0.45f  /* scale of each effect (1.0 = full game volume) */
#define GAIN_MUSIC  0.50f  /* scale of radio/speech */
#define DUCK_START   2      /* from this many simultaneous effects the overall volume starts to drop */
#define DUCK_STEP    0.16f  /* drop per effect above the start: 5 effects -> 1/(1+0.16*3) = 0.68 */
/* A volume that jumps from one sample to the next is a step in the waveform, heard as a click. Lengths in output samples (22 kHz) */
#define SFX_DECLICK  60     /* 2.7 ms: fade in when an effect starts, fade out when it stops or is replaced */
#define SFX_UPD_RAMP 128    /* 5.8 ms: volume changes of a running effect, like the mixer's own mixer_ch_set_vol */
#define DUCK_RAMP    128
#define MAX_FREQ    36000.0f /* per-channel cap, which sets the size of its buffer; the game asks for up to ~50 kHz, so very high pitches are limited */

typedef struct {
    const uint8_t *data;  /* sample start (ROM KSEG1 or RAM) */
    uint32_t len;         /* bytes */
    int32_t loop_start, loop_end; /* bytes, relative to the start; loop_end -1 = end */
    int loops;            /* 0 = infinite */
    waveform_t wf;
    uint8_t *ram;         /* copy in RAM (effects cache) or NULL */
    void *ram_alloc;      /* allocated block (ram may be 1 byte ahead, due to DMA parity) */
    uint32_t last_use;    /* to evict the least recently used from the cache, and to reuse this entry when the table is full */
    int bits8;            /* 8-bit samples, widened to 16 while reading: len and positions are then in bytes = samples */
} Wave;

/* effects cache in RAM: the mixer asks for ~140 bytes at a time, and each ROM DMA costs ~45 us in setup alone */
#define FXCACHE_MAX      (192u * 1024u) /* total in RAM. 384 KB ate the heap headroom (only ~100 KB free) and caused 'Out of memory'; 256 KB was still tight */
#define FXCACHE_ITEM_MAX (80u * 1024u)  /* largest sound that enters the cache: the police siren is 79 KB and plays through a whole chase */
/* One block, reserved before the engine starts. Asking the heap for each sound left it in pieces after a few minutes of play: with 550 KB free the
 * largest piece was 33 KB, so the larger effects stopped fitting and the mixer went back to reading them from ROM. */
static uint8_t *g_fxpool;
static uint32_t g_fxcache_bytes, g_fxcache_tick;

typedef struct {
    int id;               /* Miles handle (0 = free) */
    uint32_t order;       /* to steal the oldest channel */
    Wave *wave;           /* what the channel plays: that table entry cannot be reused meanwhile */
    uint32_t upd_t;       /* ticks of the last volume/pan/pitch update */
    int fading;           /* fading out: silent soon, stopped by the poll once g_mixed reaches fade_end */
    uint32_t fade_end;
    int volume;           /* last volume asked for */
    int loop;             /* repeats until the game stops it */
} Voice;

static Wave g_wave[NWAVE];
static int g_nwave;
static Voice g_voice[NVOICE];
static uint32_t g_order;
static uint32_t g_mixed;  /* samples mixed so far: the mixer's ramps run on these, not on time */
static int g_inited;

extern void gta2_n64_rom_copy(void *dst, const void *src, unsigned len); /* render_n64.c */

static inline int in_rom(const void *p) { const uintptr_t a = (uintptr_t)p; return a >= 0xB0000000u && a < 0xC0000000u; }


/* Each byte becomes a sample, with room left for the mixer's resampler: see BANK_GAIN in tools/audio_le2be.py (0.78 x 256 = 200) */
#define BITS8_GAIN 200
static void wave_read_8(Wave *w, samplebuffer_t *sbuf, int wpos, int wlen)
{
    int16_t *dst = (int16_t *)samplebuffer_append(sbuf, wlen);
    uint8_t *bytes = (uint8_t *)dst + wlen; /* widens forward: no sample is written over a byte not yet read */
    int have = 0;
    if ((uint32_t)wpos < w->len) have = (int)(w->len - (uint32_t)wpos);
    if (have > wlen) have = wlen;
    if (have && w->ram) memcpy(bytes, w->ram + wpos, (size_t)have);
    else if (have) gta2_n64_rom_copy(bytes, w->data + wpos, (unsigned)have);
    for (int i = 0; i < have; ++i) dst[i] = (int16_t)(((int)bytes[i] - 128) * BITS8_GAIN);
    for (int i = have; i < wlen; ++i) dst[i] = 0;
}

static void wave_read(void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking)
{
    (void)seeking;
    Wave *w = (Wave *)ctx;
    if (w->bits8) { wave_read_8(w, sbuf, wpos, wlen); return; }
    uint8_t *dst = (uint8_t *)samplebuffer_append(sbuf, wlen);
    const uint32_t off = (uint32_t)wpos * 2u;
    uint32_t n = (uint32_t)wlen * 2u, have = 0;
    if (off < w->len) have = w->len - off;
    if (have > n) have = n;
    if (have && w->ram) {
        memcpy(dst, w->ram + off, have); /* sound in the RAM cache */
    } else if (have) {
        const uint8_t *src = w->data + off;
#if N64_DEBUG_OVERLAY
        ++g_diag_rom;
        if (w->len > g_diag_rom_len) g_diag_rom_len = w->len;
#endif
        if (in_rom(src) && (((uintptr_t)src ^ (uintptr_t)dst) & 1u) == 0u)
            dma_read(dst, (uint32_t)((uintptr_t)src & 0x1FFFFFFFu), have); /* waits for the PI and reads via DMA */
        else
        {
            gta2_n64_rom_copy(dst, src, have);                              /* RAM, or different parity */
        }
    }
    if (have < n) memset(dst + have, 0, n - have);
}

static void fxcache_free(Wave *w);

/* Table full: gives back the entry used longest ago among those not playing. The three maps and the menu have 383 sounds, and
 * one played both looping and not takes two entries, so a long session could fill the table; new sounds were then silent for good. */
static Wave *wave_reuse(void)
{
    Wave *best = NULL;
    for (int i = 0; i < NWAVE; ++i) {
        Wave *w = &g_wave[i];
        int busy = 0;
        for (int c = 0; c < NVOICE; ++c) if (g_voice[c].wave == w && mixer_ch_playing(c)) { busy = 1; break; }
        if (!busy && (!best || w->last_use < best->last_use)) best = w;
    }
    if (best) fxcache_free(best);
    return best;
}

static Wave *wave_get(const void *data, uint32_t len, int loops, int32_t ls, int32_t le, int bits8)
{
    for (int i = 0; i < g_nwave; ++i) {
        Wave *w = &g_wave[i];
        if (w->data == data && w->len == len && w->loops == loops && w->loop_start == ls && w->loop_end == le && w->bits8 == bits8) return w;
    }
    Wave *w = g_nwave < NWAVE ? &g_wave[g_nwave++] : wave_reuse();
    if (!w) return NULL;
    memset(w, 0, sizeof(*w));
    w->data = (const uint8_t *)data; w->len = len; w->loops = loops; w->loop_start = ls; w->loop_end = le; w->bits8 = bits8;
    w->wf.name = "gta2";
    w->wf.bits = 16;
    w->wf.channels = 1;
    w->wf.frequency = 22050.0f;
    w->wf.len = bits8 ? (int)len : (int)(len / 2u);
    if (loops == 0) { /* Miles: 0 = repeats until the game stops the sound */
        int32_t s = ls > 0 ? ls : 0, e = (le < 0 || (uint32_t)le > len) ? (int32_t)len : le;
        if (e - s >= 4) {
            w->wf.loop_len = (e - s) / 2;
            w->wf.loop_end = (e == (int32_t)len) ? 0 : e / 2;
        }
    }
    w->wf.read = wave_read;
    w->wf.ctx = w;
    return w;
}


void gta2_n64_fx_pool_init(void)
{
    g_fxpool = (uint8_t *)memalign(16, FXCACHE_MAX); /* if it fails, every sound plays from ROM */
}

/* sounds in the pool, by position */
static Wave *g_fxlist[NWAVE];
static int g_fxn;

/* whole cache lines for each block only; dma_read requires the same parity in RAM and ROM */
static inline uint32_t fx_block_size(const Wave *w)
{
    return (w->len + (uint32_t)((uintptr_t)w->data & 1u) + 15u) & ~15u;
}

static void fxcache_free(Wave *w)
{
    if (!w->ram) return;
    w->ram = NULL; /* before freeing: a following read goes back to ROM */
    int i = 0;
    while (g_fxlist[i] != w) ++i;
    --g_fxn;
    memmove(&g_fxlist[i], &g_fxlist[i + 1], (size_t)(g_fxn - i) * sizeof(g_fxlist[0]));
    w->ram_alloc = NULL;
    g_fxcache_bytes -= fx_block_size(w);
}

/* A place for sz bytes; there is room in total (see fxcache_touch). If it is split in pieces, moves the sounds down to join them. Safe at any moment:
 * the mixer copies from the pool only inside wave_read, in this same thread, and keeps no pointer to it. */
static uint8_t *fxpool_place(uint32_t sz, int *pos)
{
    uint8_t *end = g_fxpool;
    for (int i = 0; i < g_fxn; ++i) {
        uint8_t *blk = (uint8_t *)g_fxlist[i]->ram_alloc;
        if ((uint32_t)(blk - end) >= sz) { *pos = i; return end; }
        end = blk + fx_block_size(g_fxlist[i]);
    }
    *pos = g_fxn;
    if ((uint32_t)(g_fxpool + FXCACHE_MAX - end) >= sz) return end;
    end = g_fxpool;
    for (int i = 0; i < g_fxn; ++i) {
        Wave *m = g_fxlist[i];
        const uint32_t msz = fx_block_size(m);
        const uint32_t odd = (uint32_t)(m->ram - (uint8_t *)m->ram_alloc);
        memmove(end, m->ram_alloc, msz);
        m->ram_alloc = end;
        m->ram = end + odd;
        end += msz;
    }
    return end;
}

/* puts the sound in the RAM cache the first time it plays, so that the mixer does not wait on the ROM in the middle of it */
static void fxcache_touch(Wave *w)
{
    w->last_use = ++g_fxcache_tick;
    if (w->ram || !g_fxpool || !in_rom(w->data) || w->len == 0 || w->len > FXCACHE_ITEM_MAX) return;
    const uint32_t sz = fx_block_size(w);
    while (g_fxcache_bytes + sz > FXCACHE_MAX) { /* frees the least recently used, except what is playing: a loop that started long ago looks old, and would go on reading from ROM */
        Wave *old = NULL;
        for (int i = 0; i < g_fxn; ++i) {
            Wave *c = g_fxlist[i];
            if (old && c->last_use >= old->last_use) continue;
            int busy = 0;
            for (int v = 0; v < NVOICE; ++v) if (g_voice[v].wave == c && mixer_ch_playing(v)) { busy = 1; break; }
            if (!busy) old = c;
        }
        if (!old) return;
        fxcache_free(old);
    }
    int pos;
    uint8_t *blk = fxpool_place(sz, &pos);
    memmove(&g_fxlist[pos + 1], &g_fxlist[pos], (size_t)(g_fxn - pos) * sizeof(g_fxlist[0]));
    g_fxlist[pos] = w;
    ++g_fxn;
    uint8_t *ram = blk + ((uintptr_t)w->data & 1u);
    data_cache_hit_writeback_invalidate(blk, sz);
    dma_read(ram, (uint32_t)((uintptr_t)w->data & 0x1FFFFFFFu), w->len);
    w->ram_alloc = blk;
    w->ram = ram;
    g_fxcache_bytes += sz;
}

static int voice_find(int id)
{
    for (int c = 0; c < NVOICE; ++c) if (g_voice[c].id == id) return c;
    return -1;
}

/* Options menu volumes (0..10): music = radios/speech (streams), effects = game sounds */
static int g_vol_music = 10, g_vol_sfx = 10;
static void streams_refresh(void);
int gta2_n64_get_volume(int which) { return which == 0 ? g_vol_music : g_vol_sfx; }
void gta2_n64_set_volume(int which, int v)
{
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    if (which == 0) { g_vol_music = v; streams_refresh(); }
    else g_vol_sfx = v;
}

static void apply_mix(int ch, int volume, int pan, int rate, int ramp)
{
    float v = (float)volume / 127.0f * ((float)g_vol_sfx * 0.1f) * GAIN_SFX, p = (float)pan / 127.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    mixer_ch_set_vol_ramp(ch, v * (1.0f - p), v * p, ramp);
    if (rate > 0) {
        float f = (float)rate;
        if (f > MAX_FREQ) f = MAX_FREQ; /* above the limit the mixer halts the game (assert) */
        mixer_ch_set_freq(ch, f);
    }
}

void gta2_n64_audio_init(void)
{
    if (g_inited) return;
    audio_init(OUT_FREQ, AUDIO_INIT_LATENCY_MS(240)); /* more buffer headroom: a slow frame (explosions) does not drain the audio = no pops */
    mixer_init(NVOICE + NSTREAM);
    for (int c = 0; c < NVOICE + NSTREAM; ++c) mixer_ch_set_limits(c, 16, MAX_FREQ, 0);
    wav64_init_compression(2); /* radios in ULC (audioconv64 --wav-compress ulc) */
    g_inited = 1;
}

static float g_duck = 1.0f;
static int g_duck_n;
/* Per channel with a ramp: the master volume is an instant step, which clicks on everything playing every time an effect starts or ends */
static void duck_apply(void)
{
    g_duck = 1.0f;
    if (g_duck_n > DUCK_START) g_duck = 1.0f / (1.0f + DUCK_STEP * (float)(g_duck_n - DUCK_START));
    for (int c = 0; c < NVOICE + NSTREAM; ++c)
        if (mixer_ch_playing(c)) mixer_ch_set_gain_ramp(c, g_duck, DUCK_RAMP, mixer_ramp_linear, 0);
}

/* A channel keeps the volume of its last sound, and mixer_init leaves them all at 1.0. The volume set after playing is reached
 * through a ramp from there, so the first effect on each channel started several times too loud and the next ones at the level of the
 * one before. Starting from silence turns that ramp into a short fade in. */
static void channel_start(int ch)
{
    mixer_ch_set_vol_ramp(ch, 0.0f, 0.0f, 0);
    mixer_ch_set_gain(ch, g_duck);
}

/* Instead of cutting the sound wherever it is, which clicks */
static void voice_fade_out(int ch)
{
    mixer_ch_set_vol_ramp(ch, 0.0f, 0.0f, SFX_DECLICK);
    g_voice[ch].id = 0;
    g_voice[ch].fading = 1;
    g_voice[ch].fade_end = g_mixed + SFX_DECLICK;
}

#if N64_DEBUG_OVERLAY
/* largest sound read from ROM since the last call, voices playing now, KB of sounds in RAM */
void gta2_n64_audio_detail(unsigned *rom_kb, unsigned *voices, unsigned *pool_kb)
{
    *rom_kb = (g_diag_rom_len + 1023u) / 1024u;
    g_diag_rom_len = 0;
    unsigned n = 0;
    for (int c = 0; c < NVOICE; ++c) if (mixer_ch_playing(c)) ++n;
    *voices = n;
    *pool_kb = g_fxcache_bytes / 1024u;
}
#endif

/* feeds the AI: called a few times per frame (render_n64.c) */
void gta2_n64_audio_poll(void)
{
    if (!g_inited) return;
#if N64_DEBUG_OVERLAY
    const uint32_t diag_now = timer_ticks();
    if (g_diag_poll_t && (uint32_t)(diag_now - g_diag_poll_t) > CAP_MS(DIAG_LATE_MS)) ++g_diag_late;
    g_diag_poll_t = diag_now;
#endif
    while (audio_can_write()) {
        short *buf = audio_write_begin();
        const int n = audio_get_buffer_length();
        int ativo = 0, nsfx = 0;
        for (int c = 0; c < NVOICE; ++c) {
            if (!g_voice[c].fading) continue;
            if (!mixer_ch_playing(c)) g_voice[c].fading = 0;
            else if ((int32_t)(g_mixed - g_voice[c].fade_end) >= 0) { mixer_ch_stop(c); g_voice[c].fading = 0; }
        }
        for (int c = 0; c < NVOICE + NSTREAM; ++c) if (mixer_ch_playing(c)) { ativo = 1; if (c < NVOICE && !g_voice[c].fading) ++nsfx; }
        if (nsfx != g_duck_n) { g_duck_n = nsfx; duck_apply(); }
        if (ativo) { mixer_poll(buf, n); g_mixed += (uint32_t)n; }
        else memset(buf, 0, (size_t)n * 2u * sizeof(short)); /* nothing playing: direct silence, without waking the RSP */
#if N64_DEBUG_OVERLAY
        diag_block(buf, n);
#endif
        audio_write_end();
    }
}

void gta2_n64_sfx_start(int id, const void *data, int len, int rate, int format, int loops, int loop_start,
                        int loop_end, int volume, int pan)
{
    if (!g_inited || !id || len < 4) return;
    if (format & 2) return;                              /* mono only */
    const int bits8 = !(format & 1);
    if (!in_rom(data)) return;                           /* bank not yet mapped from ROM */
    if (loops != 0) { /* CAPS: effects that end by themselves (gunshot, crash, explosion): drops the inaudible ones and the bursts */
        const uint32_t now_ticks = timer_ticks();
        if (volume < CAP_SFX_VOL_MIN) { return; }
        if ((uint32_t)(now_ticks - g_ini_t0) > CAP_MS(33)) { g_ini_t0 = now_ticks; g_ini_n = 0; }
        if (g_ini_n >= CAP_SFX_STARTS && volume < CAP_SFX_VOL_LOUD) { return; }
        ++g_ini_n;
    }
    Wave *w = wave_get(data, bits8 ? (uint32_t)len : (uint32_t)len & ~1u, loops, loop_start, loop_end, bits8);
    if (!w) return;
    fxcache_touch(w);
    const int old = voice_find(id);
    if (old < 0) {
        int active_count = 0, shots = 0, quietest = -1;
        for (int c = 0; c < NVOICE; ++c) {
            if (!mixer_ch_playing(c) || g_voice[c].fading) continue;
            ++active_count;
            if (g_voice[c].loop) continue;
            ++shots;
            if (quietest < 0 || g_voice[c].volume < g_voice[quietest].volume) quietest = c;
        }
        if (active_count >= CAP_SFX_VOICES && loops != 0 && shots >= CAP_SFX_SHOTS) { /* infinitely looping ones, like engine/siren, always enter */
            if (g_voice[quietest].volume > volume) { return; }
            voice_fade_out(quietest);
        }
    } else if (mixer_ch_playing(old)) {
        voice_fade_out(old); /* restart: the old one fades out here and the new one starts on another channel */
    } else {
        g_voice[old].id = 0;
    }
    int ch = -1;
    for (int c = 0; c < NVOICE; ++c) if (!mixer_ch_playing(c)) { ch = c; break; }
    if (ch < 0) for (int c = 0; c < NVOICE; ++c) if (g_voice[c].fading) { ch = c; break; }
    if (ch < 0) { /* all busy: steals the oldest */
        ch = 0;
        for (int c = 1; c < NVOICE; ++c) if (g_voice[c].order < g_voice[ch].order) ch = c;
    }
    g_voice[ch].id = id;
    g_voice[ch].wave = w;
    g_voice[ch].fading = 0;
    g_voice[ch].volume = volume;
    g_voice[ch].loop = loops == 0;
    g_voice[ch].order = ++g_order;
    g_voice[ch].upd_t = timer_ticks();
    mixer_ch_play(ch, &w->wf);
    channel_start(ch);
    apply_mix(ch, volume, pan, rate, SFX_DECLICK);
}

void gta2_n64_sfx_update(int id, int volume, int pan, int rate)
{
    if (!g_inited) return;
    const int ch = voice_find(id);
    if (ch >= 0 && mixer_ch_playing(ch)) {
        g_voice[ch].volume = volume;
        const uint32_t now_ticks = timer_ticks();
        if ((uint32_t)(now_ticks - g_voice[ch].upd_t) < CAP_MS(CAP_SFX_UPD_MS)) return; /* cap: update too frequent */
        g_voice[ch].upd_t = now_ticks;
        apply_mix(ch, volume, pan, rate, SFX_UPD_RAMP);
    }
}

void gta2_n64_sfx_stop(int id)
{
    if (!g_inited) return;
    const int ch = voice_find(id);
    if (ch < 0) return;
    if (mixer_ch_playing(ch)) voice_fade_out(ch);
    else g_voice[ch].id = 0;
}

void gta2_n64_master_volume(int volume)
{
    if (!g_inited) return;
    float v = (float)volume / 127.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    mixer_set_vol(v);
}

/* ---- Streams (radios in gtaudio, .wav64 files converted at build time) ---- */
typedef struct {
    int id;          /* Miles handle (0 = free) */
    int open;
    wav64_t wav;
    int volume, rate;
} Strm;
static Strm g_st[NSTREAM];

static Strm *stream_find(int id)
{
    for (int i = 0; i < NSTREAM; ++i) if (g_st[i].open && g_st[i].id == id) return &g_st[i];
    return NULL;
}
static inline int stream_ch(const Strm *s) { return NVOICE + (int)(s - g_st); }
static void stream_apply(Strm *s);
static void streams_refresh(void)
{
    for (int i = 0; i < NSTREAM; ++i)
        if (g_st[i].open && mixer_ch_playing(stream_ch(&g_st[i]))) stream_apply(&g_st[i]);
}

static void stream_apply(Strm *s)
{
    const int ch = stream_ch(s);
    float v = (float)s->volume / 127.0f * ((float)g_vol_music * 0.1f) * GAIN_MUSIC;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    mixer_ch_set_vol(ch, v, v);
    if (s->rate > 0 && s->rate != 22050) {
        float f = s->wav.wave.frequency * (float)s->rate / 22050.0f;
        if (f > MAX_FREQ) f = MAX_FREQ;
        mixer_ch_set_freq(ch, f);
    }
}

#define MISS_MAX 64
static uint32_t g_miss[MISS_MAX];
static int g_nmiss;

/* path already normalized by the port ("rom:/data/gtaudio/1.wav"); the file on the DFS is the .wav64 */
int gta2_n64_stream_open(int id, const char *path)
{
    if (!g_inited || !id || !path) return 0;
    char p[200];
    const size_t n = strlen(path);
    if (n + 3 >= sizeof(p)) return 0;
    memcpy(p, path, n + 1);
    if (n >= 4 && strcmp(p + n - 4, ".wav") == 0) strcat(p, "64");
    uint32_t h = 2166136261u;           /* files that do not exist (speech, d/m.wav64): remembers and does not try to open them again */
    for (const char *q = p; *q; ++q) h = (h ^ (uint8_t)*q) * 16777619u;
    for (int i = 0; i < g_nmiss; ++i) if (g_miss[i] == h) return 0;
    FILE *f = fopen(p, "rb");           /* wav64_open halts the game if the file does not exist */
    if (!f) { if (g_nmiss < MISS_MAX) g_miss[g_nmiss++] = h; return 0; }
    fclose(f);
    Strm *s = stream_find(id);
    if (!s) for (int i = 0; i < NSTREAM; ++i) if (!g_st[i].open) { s = &g_st[i]; break; }
    if (!s) return 0;
    if (s->open) { mixer_ch_stop(stream_ch(s)); wav64_close(&s->wav); s->open = 0; }
    wav64_open(&s->wav, p);
    s->id = id; s->open = 1; s->volume = 127; s->rate = 22050;
    return 1;
}

void gta2_n64_stream_close(int id)
{
    Strm *s = stream_find(id);
    if (!s) return;
    mixer_ch_stop(stream_ch(s));
    wav64_close(&s->wav);
    s->open = 0; s->id = 0;
}

void gta2_n64_stream_start(int id, int loops)
{
    Strm *s = stream_find(id);
    if (!s) return;
    wav64_set_loop(&s->wav, loops == 0); /* Miles: 0 = always repeats (radio) */
    wav64_play(&s->wav, stream_ch(s));
    channel_start(stream_ch(s));
    stream_apply(s);
}

int gta2_n64_stream_playing(int id)
{
    Strm *s = stream_find(id);
    return s && mixer_ch_playing(stream_ch(s));
}

void gta2_n64_stream_set(int id, int volume, int rate)
{
    Strm *s = stream_find(id);
    if (!s) return;
    s->volume = volume; s->rate = rate;
    if (mixer_ch_playing(stream_ch(s))) stream_apply(s);
}

void gta2_n64_stream_seek(int id, int ms)
{
    Strm *s = stream_find(id);
    if (!s || ms < 0) return;
    if (mixer_ch_playing(stream_ch(s))) wav64_seek(&s->wav, stream_ch(s), (double)ms / 1000.0);
}

/* which: 0 = total duration, 1 = current position (ms) */
int gta2_n64_stream_ms(int id, int which)
{
    Strm *s = stream_find(id);
    if (!s || s->wav.wave.frequency <= 0.0f) return 0;
    const double f = s->wav.wave.frequency;
    if (which == 0) return s->wav.wave.len > 0 ? (int)(s->wav.wave.len * 1000.0 / f) : 0;
    return mixer_ch_playing(stream_ch(s)) ? (int)(mixer_ch_get_pos(stream_ch(s)) * 1000.0 / f) : 0;
}
