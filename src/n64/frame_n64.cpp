// Draw-list storage and presentation (see frame.h).
#include "render/frame.h"
#include "render/n64tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "platform/port_api.h"

extern "C" void gta2_n64_present(const void* cmds, int cmdCount, const void* verts, int vertCount, int refWidth);

namespace
{
std::vector<FrameVertex> gVerts;
std::vector<FrameCommand> gCmds;
uint32_t gPresents = 0;

// Gameplay frames are as wide as the selected view (widescreen); everything
// else (menus, loading screens, movies) is laid out in the 640x480 box and
// shown pillarboxed. The game sets the layout per frame (Game_0x40::Draw).
int32_t gFrameWidth = 640;

// N64: cache of HUD pieces that only change now and then (e.g.: gang respect bars). The piece is drawn
// normally and the generated commands/vertices are kept; in the following frames, if the key (draw inputs) is the
// same, the kept commands are replayed without going through the sprite engine (which costs ~40 us per sprite).
// Only keeps pieces made purely of triangles (kind 0). Only valid for consecutive frames: if the piece goes frames
// without being requested, the commands' texture slots may have been reused and the cache is discarded.
struct HudCacheSlot
{
    std::vector<FrameVertex> v;
    std::vector<FrameCommand> c;
    uint32_t key = 0, frame = 0;
    float offx = 0.0f;
    bool valid = false;
    size_t cs = 0, vs = 0;
};
HudCacheSlot gHudCache[4];

// N64: RAM is the limit (the heap stack touches the end of memory). If a mission screen with a lot of text asks for more vertices
// than the vector holds, the vector doubles in size and, without RAM for the copy, operator new returns null and the game hangs
// (write to an invalid address). Before growing, tests whether the larger copy fits; if not, drops whatever exceeds the limit.
// Grows by half, not by double: the old and the new copy coexist in one piece each, and free RAM is split into many pieces. A busy scene
// with 600 KB free still could not double the vertices. If even that does not fit, tries just enough for the next few, then gives up:
// what is left out of this frame is not drawn.
// volatile: without it the compiler removes a malloc whose result is only tested and freed, and the test with it.
uint32_t gDropped = 0;

template <class T>
bool roomFits(size_t count)
{
    void* volatile probe = malloc(count * sizeof(T));
    if (!probe)
    {
        return false;
    }
    free(probe);
    return true;
}

template <class T>
bool ensureRoom(std::vector<T>& v, size_t extra)
{
    const size_t need = v.size() + extra;
    if (need <= v.capacity())
    {
        return true;
    }
    size_t cap = v.capacity() + v.capacity() / 2;
    if (cap < 64u * 1024u / sizeof(T))
    {
        cap = 64u * 1024u / sizeof(T);
    }
    if (cap < need)
    {
        cap = need;
    }
    if (!roomFits<T>(cap))
    {
        cap = need + 4096u / sizeof(T);
        if (!roomFits<T>(cap))
        {
            ++gDropped;
            return false;
        }
    }
    v.reserve(cap);
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// N64: text grouping and cache (used by Draw.cpp::DrawText_5D8A10).
// Each letter is a quad with its own texture; a 150-letter phrase generated ~150 commands (one texture
// upload each) and cost ~25 ms per frame while the mission briefing was on screen.
//  1) the letters of a DrawText call are regrouped by texture (a repeated letter becomes a single command);
//  2) the result is kept: if the next frame draws exactly the same text (same phrase, position, font,
//     palette), it is replayed directly, without repeating the engine's per-letter work.
// Kept texture tokens stay valid as long as the text is drawn every frame (the renderer marks the
// slot as in use); if a frame passes without it, the entry is discarded and rebuilt.
namespace
{
constexpr uint32_t kTxtMaxVerts = 1200; // ~28 KB: up to 200 stored letters
constexpr uint32_t kTxtMinVerts = 48;   // fewer than 8 letters: not worth it
constexpr int kTxtMaxCmds = 48;         // distinct textures per phrase
constexpr int kTxtEntries = 8;
constexpr uint32_t kTokBase = 0x10000u; // same value as TEX_TOKEN_BASE (render_n64.c)
constexpr uint32_t kTokCount = N64_CACHE_N; // same value as CACHE_N (render_n64.c)

struct TxtCmd
{
    uint32_t texture, count;
};
struct TxtEntry
{
    uint64_t key;
    uint32_t last, vfirst, nverts, blend;
    uint32_t stamp; // changes each time the entry is (re)recorded: the renderer uses it to know whether the recorded block still holds
    int ncmds;
    bool used;
    TxtCmd cmds[kTxtMaxCmds];
};

FrameVertex gTxtPool[kTxtMaxVerts];
TxtEntry gTxtEnt[kTxtEntries];
uint32_t gTxtPoolUsed = 0;
bool gTxtActive = false;
bool gForceNewCmd = false;
uint32_t gHudCmdFlag = 0; // kind 0 commands: colour = 1 while the HUD is being drawn, 2 for a map tile, 3 for the text of the HUD
uint64_t gTxtKey = 0;
size_t gTxtCmd0 = 0, gTxtVert0 = 0;
uint32_t gTxtStamp = 0;


inline uint64_t txtMix(uint64_t h, uint32_t v)
{
    h ^= v;
    return h * 1099511628211ULL;
}
inline uint32_t txtBits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}
void txtReset()
{
    for (int i = 0; i < kTxtEntries; ++i)
    {
        gTxtEnt[i].used = false;
    }
    gTxtPoolUsed = 0;
}
} // namespace

extern "C" {

// For the diagnostic overlay: draw requests left out for lack of RAM
uint32_t gta2_frame_dropped(void)
{
    return gDropped;
}

void gta2_frame_push_vertex(uint32_t texture, uint32_t blend, const FrameVertex& v)
{
    if (!ensureRoom(gVerts, 1))
    {
        return;
    }
    if (gForceNewCmd || gCmds.empty() || gCmds.back().kind != 0 || gCmds.back().texture != texture || gCmds.back().blend != blend ||
        gCmds.back().colour != gHudCmdFlag)
    {
        if (!ensureRoom(gCmds, 1))
        {
            return;
        }
        FrameCommand c = {0, texture, blend, static_cast<uint32_t>(gVerts.size()), 0, gHudCmdFlag, {0, 0, 0, 0}};
        gCmds.push_back(c);
        gForceNewCmd = false;
    }
    gCmds.back().count++;
    gVerts.push_back(v);
}

// A translucent black rectangle behind a text (units of the 640x480 screen). Texture 1 = the renderer's mark for it.
void gta2_frame_box_colour(float x0, float y0, float x1, float y1, uint32_t rgba)
{
    const FrameVertex v[4] = {{x0, y0, 1.0f, 0.0f, 0.0f, rgba},
                              {x1, y0, 1.0f, 0.0f, 0.0f, rgba},
                              {x1, y1, 1.0f, 0.0f, 0.0f, rgba},
                              {x0, y1, 1.0f, 0.0f, 0.0f, rgba}};
    static const int kTris[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i)
    {
        gta2_frame_push_vertex(1u, 1u, v[kTris[i]]);
    }
}

void gta2_frame_box(float x0, float y0, float x1, float y1)
{
    gta2_frame_box_colour(x0, y0, x1, y1, 0x90000000u);
}

uint64_t gta2_text_key(const uint16_t* s, float x, float y, unsigned font, float scale, int ptype, unsigned pal, int alpha, unsigned flags)
{
    if (!s)
    {
        return 0;
    }
    uint64_t h = 1469598103934665603ULL;
    for (const uint16_t* p = s; *p; ++p) // game wchar_t = 16 bits (-fshort-wchar)
    {
        h = txtMix(h, *p);
    }
    h = txtMix(h, txtBits(x));
    h = txtMix(h, txtBits(y));
    h = txtMix(h, font);
    h = txtMix(h, txtBits(scale));
    h = txtMix(h, static_cast<uint32_t>(ptype));
    h = txtMix(h, pal);
    h = txtMix(h, static_cast<uint32_t>(alpha));
    h = txtMix(h, flags);
    return h;
}

// 1 if the phrase was replayed from the cache (the caller must not draw it again)
int gta2_text_replay(uint64_t key)
{
    for (int i = 0; i < kTxtEntries; ++i)
    {
        TxtEntry& e = gTxtEnt[i];
        if (!e.used || e.key != key)
        {
            continue;
        }
        if (e.last + 1u < gPresents) // a frame passed without drawing: the texture slots may have been recycled
        {
            e.used = false;
            return 0;
        }
        e.last = gPresents;
        if (!ensureRoom(gVerts, e.nverts) || !ensureRoom(gCmds, static_cast<size_t>(e.ncmds)))
        {
            return 1; // no RAM: the phrase disappears this frame (neither replayed nor rebuilt)
        }
        const uint32_t base = static_cast<uint32_t>(gVerts.size());
        gVerts.insert(gVerts.end(), gTxtPool + e.vfirst, gTxtPool + e.vfirst + e.nverts);
        uint32_t first = base;
        for (int j = 0; j < e.ncmds; ++j)
        {
            // rect[2] = entry stamp, rect[3] = entry index + 1: marks "static text" for the renderer to record an RSP block
            FrameCommand c = {0, e.cmds[j].texture, e.blend, first, e.cmds[j].count, gHudCmdFlag,
                              {0, 0, static_cast<int32_t>(e.stamp), i + 1}};
            gCmds.push_back(c);
            first += e.cmds[j].count;
        }
        gForceNewCmd = true; // the next drawn triangle must not be appended to a marked command
        return 1;
    }
    return 0;
}

void gta2_text_begin(uint64_t key)
{
    gTxtActive = true;
    gTxtKey = key;
    gTxtCmd0 = gCmds.size();
    gTxtVert0 = gVerts.size();
    gForceNewCmd = true; // this phrase's letters never join a previous command
}

void gta2_text_end()
{
    if (!gTxtActive)
    {
        return;
    }
    gTxtActive = false;
    gForceNewCmd = false;

    const size_t c0 = gCmds.size() >= gTxtCmd0 ? gTxtCmd0 : gCmds.size();
    const size_t v0 = gTxtVert0;
    const size_t nc = gCmds.size() - c0;
    const size_t nv = gVerts.size() - v0;
    if (nc == 0 || nv < kTxtMinVerts || nv > kTxtMaxVerts)
    {
        return;
    }

    // clean phrases only: triangles, same blending, vertices in sequence
    const uint32_t blend = gCmds[c0].blend;
    uint32_t expect = static_cast<uint32_t>(v0);
    for (size_t i = 0; i < nc; ++i)
    {
        const FrameCommand& c = gCmds[c0 + i];
        if (c.kind != 0 || c.blend != blend || c.first != expect)
        {
            return;
        }
        expect += c.count;
    }
    if (expect != static_cast<uint32_t>(gVerts.size()))
    {
        return;
    }

    // distinct textures, in order of appearance, and the vertex total of each
    uint32_t tex[kTxtMaxCmds];
    uint32_t cnt[kTxtMaxCmds];
    int nt = 0;
    bool cacheable = true;
    for (size_t i = 0; i < nc; ++i)
    {
        const FrameCommand& c = gCmds[c0 + i];
        if (c.texture - kTokBase >= kTokCount) // no valid texture token: not kept
        {
            cacheable = false;
        }
        int j = 0;
        while (j < nt && tex[j] != c.texture)
        {
            ++j;
        }
        if (j == nt)
        {
            if (nt == kTxtMaxCmds)
            {
                return;
            }
            tex[nt] = c.texture;
            cnt[nt] = 0;
            ++nt;
        }
        cnt[j] += c.count;
    }

    // cache entry: the one with the same phrase, else a free one, else clears everything
    int ei = -1;
    for (int i = 0; i < kTxtEntries; ++i)
    {
        if (gTxtEnt[i].used && gTxtEnt[i].key == gTxtKey)
        {
            ei = i;
            break;
        }
    }
    if (ei < 0)
    {
        for (int i = 0; i < kTxtEntries; ++i)
        {
            if (!gTxtEnt[i].used)
            {
                ei = i;
                break;
            }
        }
    }
    if (ei < 0 || gTxtPoolUsed + static_cast<uint32_t>(nv) > kTxtMaxVerts)
    {
        txtReset();
        ei = 0;
    }

    // the pool's free area holds a copy of the original order; it then receives the grouped version
    FrameVertex* tmp = gTxtPool + gTxtPoolUsed;
    memcpy(tmp, &gVerts[v0], nv * sizeof(FrameVertex));
    size_t out = v0;
    for (int j = 0; j < nt; ++j)
    {
        for (size_t i = 0; i < nc; ++i)
        {
            const FrameCommand& c = gCmds[c0 + i];
            if (c.texture != tex[j])
            {
                continue;
            }
            memcpy(&gVerts[out], tmp + (c.first - static_cast<uint32_t>(v0)), c.count * sizeof(FrameVertex));
            out += c.count;
        }
    }
    FrameCommand merged = gCmds[c0];
    uint32_t first = static_cast<uint32_t>(v0);
    for (int j = 0; j < nt; ++j)
    {
        merged.texture = tex[j];
        merged.first = first;
        merged.count = cnt[j];
        gCmds[c0 + static_cast<size_t>(j)] = merged;
        first += cnt[j];
    }
    gCmds.resize(c0 + static_cast<size_t>(nt));

    if (!cacheable)
    {
        gTxtEnt[ei].used = false;
        return;
    }
    memcpy(tmp, &gVerts[v0], nv * sizeof(FrameVertex)); // grouped version
    TxtEntry& e = gTxtEnt[ei];
    e.key = gTxtKey;
    e.last = gPresents;
    e.vfirst = gTxtPoolUsed;
    e.nverts = static_cast<uint32_t>(nv);
    e.blend = blend;
    e.ncmds = nt;
    e.stamp = ++gTxtStamp;
    for (int j = 0; j < nt; ++j)
    {
        e.cmds[j].texture = tex[j];
        e.cmds[j].count = cnt[j];
    }
    e.used = true;
    gTxtPoolUsed += static_cast<uint32_t>(nv);
}

void gta2_frame_clear(uint8_t r, uint8_t g, uint8_t b, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    FrameCommand c = {1, 0, 0, 0, 0, static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16),
                      {left, top, right, bottom}};
    if (!ensureRoom(gCmds, 1))
    {
        return;
    }
    gCmds.push_back(c);
}

// N64: draws image 'img' (gbh image table, menu) -- source left,top,right,bottom in image pixels, destination x,y in 640x480 space.
// kind 3: texture = index, first/count = x/y (signed integers), rect = source.
void gta2_frame_blit(int img, int l, int t, int r, int b, int x, int y)
{
    FrameCommand c = {3, static_cast<uint32_t>(img), 0, static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0, {l, t, r, b}};
    if (!ensureRoom(gCmds, 1))
    {
        return;
    }
    gCmds.push_back(c);
}

void gta2_frame_present()
{
    gta2_n64_present(gCmds.data(), static_cast<int>(gCmds.size()), gVerts.data(), static_cast<int>(gVerts.size()), gFrameWidth);
    gCmds.clear();
    gVerts.clear();
    gFrameWidth = 640;
    ++gPresents;
}


// N64: what the game draws between these calls is the HUD (Game_0x40::Draw): the renderer keeps it sharp.
extern "C" void gta2_n64_tex_set_hud(int on); // render_n64.c
extern "C" void gta2_n64_tex_set_text(int on);
void gta2_frame_set_text(int on)
{
    if (on && gHudCmdFlag == 1u)
    {
        gHudCmdFlag = 3u;
        gta2_n64_tex_set_text(1);
    }
    else if (!on && gHudCmdFlag == 3u)
    {
        gHudCmdFlag = 1u;
        gta2_n64_tex_set_text(0);
    }
}

void gta2_frame_set_tile(int on)
{
    gHudCmdFlag = on ? 2u : 0u;
}

void gta2_frame_set_hud(int on)
{
    gHudCmdFlag = on ? 1u : 0u;
    gta2_n64_tex_set_hud(on);
    gForceNewCmd = true;
}

// 1 = the kept commands were replayed (the caller must not draw the piece)
int Gta2Port_CacheTry(int slot, uint32_t key, uint32_t frame)
{
    HudCacheSlot& s = gHudCache[slot & 3];
    if (!s.valid || s.key != key || static_cast<uint32_t>(frame - s.frame) > 1u)
    {
        return 0;
    }
    if (!ensureRoom(gVerts, s.v.size()) || !ensureRoom(gCmds, s.c.size()))
    {
        return 0;
    }
    const uint32_t base = static_cast<uint32_t>(gVerts.size());
    gVerts.insert(gVerts.end(), s.v.begin(), s.v.end());
    for (FrameCommand c : s.c)
    {
        c.first += base;
        gCmds.push_back(c);
    }
    s.frame = frame;
    return 1;
}

void Gta2Port_CacheBegin(int slot, uint32_t key, uint32_t frame)
{
    HudCacheSlot& s = gHudCache[slot & 3];
    s.valid = false;
    s.key = key;
    s.frame = frame;
    s.cs = gCmds.size();
    s.vs = gVerts.size();
    gForceNewCmd = true; // the piece's first vertex must not join a previous command
}

void Gta2Port_CacheEnd(int slot)
{
    HudCacheSlot& s = gHudCache[slot & 3];
    if (gCmds.size() < s.cs || gVerts.size() < s.vs)
    {
        return;
    }
    s.v.assign(gVerts.begin() + static_cast<long>(s.vs), gVerts.end());
    s.c.assign(gCmds.begin() + static_cast<long>(s.cs), gCmds.end());
    bool ok = !s.c.empty() && s.v.size() < 600 && s.c.size() < 60;
    for (FrameCommand& c : s.c)
    {
        if (c.kind != 0)
        {
            ok = false;
            break;
        }
        c.first -= static_cast<uint32_t>(s.vs);
    }
    s.valid = ok;
}

uint32_t gta2_port_present_count()
{
    return gPresents;
}

} // extern "C"
