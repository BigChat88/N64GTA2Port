// The GBH renderer interface (d3ddll.dll) for the N64.
//
// The game hands this layer pre-transformed vertices in a 640x480 reference
// space, 8-bit paletted textures and per-draw flags. Each draw becomes
// triangles in a per-frame vertex array, batched while texture and blend mode
// stay the same; render_n64.c draws the batches when the frame is presented.
// Textures are converted when they are drawn. No depth buffer: draw order is
// the game's.
//
// Semantics (flags, palette layout, lighting, tile UV rotation) follow the
// documented behaviour of the original driver interface.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#ifdef N64
#include "render/n64tex.h"
extern "C" void gta2_n64_tex_forget(const N64Tex* t);
extern "C" uint32_t gta2_n64_tex_capture(N64Tex* t, float a_scr, float a_tex, int additive);
#ifndef N64_HUD_SCALE
#define N64_HUD_SCALE 1.25f /* size of the HUD markers (bars, hearts, money, wanted level). 1.0 = as on PC */
#endif
#ifndef N64_PAGER_SCALE
#define N64_PAGER_SCALE 1.5f /* size of the pagers (mission clock and counter): their digits are tiny */
#endif
#ifndef N64_HUD_INSET_LEFT
#define N64_HUD_INSET_LEFT 8.0f
#endif
#ifndef N64_HUD_INSET_RIGHT
#define N64_HUD_INSET_RIGHT 16.0f
#endif
#ifndef N64_HUD_INSET_TOP
#define N64_HUD_INSET_TOP 6.0f
#endif
#ifndef N64_TILE_OVERLAP
#define N64_TILE_OVERLAP 1.0f /* in units of the 640x480 screen: half a pixel at 320x240 */
#endif
static int gHudScaleOn;
static int gTileDraw;
extern "C" void gta2_frame_set_tile(int on); // frame_n64.cpp
extern "C" void gta2_gbh_set_hud_scale(int on) { gHudScaleOn = on; }
extern "C" int gta2_n64_tex_token_empty(uint32_t token);
#include <libdragon.h>
#include <malloc.h>
#include <stdio.h>
extern "C" void gta2_frame_blit(int img, int l, int t, int r, int b, int x, int y); /* frame_n64.cpp: image blit command (menu) */
#endif

#include "gbh_graphics.hpp"
#include "platform/port_api.h"
#include "render/frame.h"

namespace
{
constexpr u32 kFitToTexture = 0x10000;
constexpr u32 kAdditive = 0x200;
constexpr u32 kAlphaBlend = 0x180;
constexpr u32 kLighting = 0x8000;
constexpr u32 kUseVertexDiffuse = 0x2000;
constexpr u32 kTileUVGiven = 0x4000;

// Palettes: the game passes 256 32-bit colours stored as a column (stride 64
// DWORDs). The 16-bit copy the game can read through STexture is kept, and
// the palette id is stored just before it so textures can find the source.
struct Palette
{
    DWORD* original = nullptr;
    u16* data565 = nullptr; // points 2 u16 past the allocation (id stored there)
    u32 loaded = 0;
};
#ifdef N64
// N64: the capacity is sized at run time from the level's REAL number of physical palettes
// (gta2_n64_palettes_reserve, called by TextureCache_15D8::LoadPals). It used to be fixed at 1024: a texture whose physical
// palette went past 1024 had no palette and was drawn flat (red gang car with no details).
int kMaxPalettes = 0;
Palette* gPalettes = nullptr;
u16 (*gStub)[4] = nullptr; // [0..1] = id (read by PaletteIdOf), [2..3] = reserved
unsigned gPalRejects = 0;
void PalReject(const char* who, s32 id)
{
    (void)who;
    (void)id;
    ++gPalRejects;
}
#else
constexpr int kMaxPalettes = 16384;
Palette gPalettes[kMaxPalettes];
#endif

u16 ToRGB565(u32 rgb)
{
    const u32 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return static_cast<u16>(((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) | (b * 31 / 255));
}

int PaletteIdOf(const u16* data565)
{
    if (!data565)
    {
        return -1;
    }
    u32 id;
    memcpy(&id, data565 - 2, 4);
    return static_cast<int>(id);
}

struct ImageEntry
{
    BOOL loaded;
    DWORD w, h;
    STexture* tex;
    u8* pixels; // a copy of the 16-bit image, to upload it again after a quick load
};
ImageEntry* gImages = nullptr;
u32 gImageCount = 0;

// Paletted textures point into the style file's 256-pixel-wide pages, so
// every row is 256 bytes apart (the game indexes them as data[row * 256]).
constexpr int kPagePitch = 256;

void UploadPaletted(STexture* t)
{
#ifdef N64
    // N64: does not convert here. Only stores (pixels, palette, size) in a descriptor; the renderer converts on demand.
    {
        N64Tex* info = reinterpret_cast<N64Tex*>(t->field_1C_ptr);
        if (!info)
        {
            info = static_cast<N64Tex*>(calloc(1, sizeof(N64Tex)));
            if (!info)
            {
                return;
            }
            info->slot = -1;
            t->field_1C_ptr = info;
        }
        const int palId = PaletteIdOf(t->field_18_pPaltData);
        info->pix = t->field_14_original_pixel_data_ptr;
        info->w = t->field_E_width;
        info->h = t->field_10_height;
        info->pal = (palId >= 0 && palId < kMaxPalettes && gPalettes[palId].loaded) ? reinterpret_cast<const uint32_t*>(gPalettes[palId].original) : nullptr;
        info->palid = static_cast<int16_t>(palId);
        info->gen++;
        return;
    }
#endif
}

STexture* NewTexture(s32 w, s32 h, BYTE* data)
{
    auto* t = static_cast<STexture*>(calloc(1, sizeof(STexture)));
    static u16 sNextId = 0;
    t->field_0_id = sNextId++;
    t->field_E_width = static_cast<u16>(w);
    t->field_10_height = static_cast<u16>(h);
    t->field_14_original_pixel_data_ptr = data;
    return t;
}

// Window and lights
float gWinLeft = 0, gWinTop = 0, gWinRight = 640, gWinBottom = 480;
float gAmbient = 255.0f; // ambient * 255, as the original driver keeps it
// Lights: on at dusk, off at noon (menu TIME OF DAY option): 0 = no light (fastest), 2 = smooth per block corner (default, ~9%). Mode 1 (flat per quad) exists in the code but is not in the menu.
#ifndef N64_LIGHTS
#define N64_LIGHTS 0 /* default = noon (no lights); the menu changes it */
#endif
#if N64_DEBUG_OVERLAY
extern "C" u32 gta2_prof_now(void); // render_n64.c: where the time of a frame goes
extern "C" void gta2_prof_add(int which, u32 t0);
#endif
int gN64Lights = N64_LIGHTS;
extern "C" int gta2_n64_get_lights(void)
{
    return gN64Lights >= 2 ? 1 : 0; // menu: 0 = off, 1 = on (smooth)
}
extern "C" void gta2_n64_set_lights(int v)
{
    gN64Lights = v > 0 ? 2 : 0;
}

struct Light
{
    u32 flags;
    float brightness, radius, radiusSq, invRadius, x, y, z, r, g, b;
    float invRadiusSq, pr, pg, pb; // N64: 1/radius^2 and colour*brightness (per light, computed once)
};
Light gLights[256];
u32 gLightCount = 0;
// N64: only lights passing the flags filter (0x30000 == 0x10000) enter the list; the rest never contribute.
u16 gActiveLights[256];
u32 gActiveCount = 0;

// N64: spatial grid of active lights (4x4 block cells, 32x32 wrapping cells = 128 blocks, more than the screen).
// Each light enters all cells its radius touches; a draw only looks at the cells under its box instead of testing all
// lights (before: ~N lights x ~hundreds of draws per frame). The result is the same: candidate lights go through the same exact
// distance test. Dedupe by stamp (a light appears in several cells).
constexpr int kBinDim = 32;
constexpr int kBinPool = 3072;
int16_t gBinHead[kBinDim * kBinDim];
int16_t gBinNext[kBinPool];
uint8_t gBinLight[kBinPool];
u32 gBinUsed = 0;
u32 gLightStamp[256];
u32 gDrawStamp = 0;
inline int BinCell(float v)
{
    const int c = static_cast<int>(v * 0.25f);
    return c < 0 ? 0 : c;
}
void BinInsert(u32 idx)
{
    const Light& l = gLights[idx];
    const int x0 = BinCell(l.x - l.radius), x1 = BinCell(l.x + l.radius);
    const int y0 = BinCell(l.y - l.radius), y1 = BinCell(l.y + l.radius);
    for (int cy = y0; cy <= y1; ++cy)
    {
        for (int cx = x0; cx <= x1; ++cx)
        {
            if (gBinUsed >= static_cast<u32>(kBinPool))
            {
                return; // pool full: the light only disappears from the remaining cells (should not happen)
            }
            const int cell = (cx & (kBinDim - 1)) + (cy & (kBinDim - 1)) * kBinDim;
            gBinLight[gBinUsed] = static_cast<uint8_t>(idx);
            gBinNext[gBinUsed] = gBinHead[cell];
            gBinHead[cell] = static_cast<int16_t>(gBinUsed++);
        }
    }
}

// 'ls' = lights reaching this draw (already culled by distance to the quad): the result is the same as walking all of them.
// N64: the light sum is computed ONCE per draw (at the quad center) and applied to the 4 vertices: "flat" lighting per
// quad, ~4x cheaper than per vertex. Each vertex still multiplies by its own colour (diff).
struct LightSum
{
    float r, g, b;
};
LightSum SumLights(const Vert& world, const Light* const* ls, u32 nls)
{
    LightSum o = {0, 0, 0};
    for (u32 i = 0; i < nls; ++i)
    {
        const Light& l = *ls[i];
        const float dx = world.x - l.x, dy = world.y - l.y, dz = world.z - l.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        // quadratic falloff (1 - d2/r2)^2 instead of the linear (r - d)/r: no sqrtf, similar look (slightly softer).
        const float t = 1.0f - d2 * l.invRadiusSq;
        if (t > 0.0f)
        {
            const float k = t * t;
            o.r += l.pr * k;
            o.g += l.pg * k;
            o.b += l.pb * k;
        }
    }
    return o;
}
// Per-frame cache of the light sum per world POINT: neighbouring block corners coincide (each corner is shared by up to 4 quads),
// so each corner is computed once. Since each draw's light list covers the quad's whole box, the sum at a corner
// does not depend on which draw computed it. Keeps the smooth (gouraud) lighting at the cost of ~1 computation per quad.
struct LCacheEnt
{
    int16_t qx, qy, qz;
    uint16_t epoch;
    LightSum s;
};
constexpr int kLCacheN = 1024;
LCacheEnt gLCache[kLCacheN];
uint16_t gLEpoch = 1; // bumped on each ResetLights (once per frame)
inline const LightSum& CachedSum(const Vert& w, const Light* const* ls, u32 nls)
{
    const int qx = static_cast<int>(w.x * 32.0f + 0.5f), qy = static_cast<int>(w.y * 32.0f + 0.5f), qz = static_cast<int>(w.z * 32.0f + 0.5f);
    const u32 h = (static_cast<u32>(qx) * 73856093u ^ static_cast<u32>(qy) * 19349663u ^ static_cast<u32>(qz) * 83492791u) >> 7;
    LCacheEnt& e = gLCache[h & (kLCacheN - 1)];
    if (e.epoch != gLEpoch || e.qx != static_cast<int16_t>(qx) || e.qy != static_cast<int16_t>(qy) || e.qz != static_cast<int16_t>(qz))
    {
        e.s = SumLights(w, ls, nls);
        e.qx = static_cast<int16_t>(qx), e.qy = static_cast<int16_t>(qy), e.qz = static_cast<int16_t>(qz);
        e.epoch = gLEpoch;
    }
    return e.s;
}
inline u32 LitApply(const LightSum& L, float m, u32 colour)
{
    const float amb = gAmbient;
    float r = m * static_cast<float>((colour >> 16) & 0xFF) * L.r + amb;
    float g = m * static_cast<float>((colour >> 8) & 0xFF) * L.g + amb;
    float b = m * static_cast<float>(colour & 0xFF) * L.b + amb;
    r = r > 255.0f ? 255.0f : r;
    g = g > 255.0f ? 255.0f : g;
    b = b > 255.0f ? 255.0f : b;
    return (colour & 0xFF000000) | (static_cast<u32>(r) << 16) | (static_cast<u32>(g) << 8) | static_cast<u32>(b);
}

void Draw(u32 flags, STexture* tex, Vert* v, int count, u32 baseColour)
{
    if (count < 3 || count > 4 || !tex || v[0].z <= 0.0f)
    {
        return;
    }
    const float tw = tex->field_E_width, th = tex->field_10_height;
    if (flags & kFitToTexture)
    {
        static const float k[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int i = 0; i < count; ++i)
        {
            v[i].x = v[0].x + k[i][0] * tw;
            v[i].y = v[0].y + k[i][1] * th;
            v[i].z = v[0].z;
            v[i].u = k[i][0] * tw;
            v[i].v = k[i][1] * th;
        }
    }
    if (gTileDraw && !(flags & (kAdditive | kAlphaBlend)))
    {
        // N64: neighbouring tiles do not always round a shared corner the same way, which leaves a thin line between them.
        // Each one covers a little of its neighbours: they are opaque, so nothing shows through.
        float cx = 0.0f, cy = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            cx += v[i].x;
            cy += v[i].y;
        }
        cx /= count;
        cy /= count;
        for (int i = 0; i < count; ++i)
        {
            v[i].x += v[i].x > cx ? N64_TILE_OVERLAP : -N64_TILE_OVERLAP;
            v[i].y += v[i].y > cy ? N64_TILE_OVERLAP : -N64_TILE_OVERLAP;
        }
    }
    if (gHudScaleOn)
    {
        // N64: each quad grows from the zone of the 640x480 screen it is in, so the marker stays in its corner.
        float cx = 0.0f, cy = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            cx += v[i].x;
            cy += v[i].y;
        }
        cx /= count;
        cy /= count;
        // gHudScaleOn: 1 = markers, 2 = pagers, which always grow from the top left corner
        const bool pager = gHudScaleOn == 2;
        const float scale = pager ? N64_PAGER_SCALE : N64_HUD_SCALE;
        const float ax = pager || cx < 213.0f ? 0.0f : (cx > 427.0f ? 640.0f : 320.0f);
        const float ay = pager || cy < 240.0f ? 0.0f : 480.0f;
        // The PC layout touches the screen edges and some parts go past them: moved inside (units of the 640x480 screen).
        const float inset_x = ax == 0.0f ? N64_HUD_INSET_LEFT : (ax == 640.0f ? -N64_HUD_INSET_RIGHT : 0.0f);
        const float inset_y = ay == 0.0f ? N64_HUD_INSET_TOP : -N64_HUD_INSET_TOP;
        for (int i = 0; i < count; ++i)
        {
            v[i].x = ax + (v[i].x - ax) * scale + inset_x;
            v[i].y = ay + (v[i].y - ay) * scale + inset_y;
        }
    }
    for (int i = 0; i < count; ++i)
    {
        v[i].w = v[i].z;
    }
    if (!(flags & kUseVertexDiffuse))
    {
        const u32 g = baseColour & 0xFF;
        for (int i = 0; i < count; ++i)
        {
            v[i].diff = 0xFF000000u | (g << 16) | (g << 8) | g;
            v[i].spec = 255;
        }
    }
#if N64_DEBUG_OVERLAY
    const u32 prof_lights_t0 = gta2_prof_now();
#endif
    if (gN64Lights && (flags & kLighting) && gAmbient != 255.0f)
    {
        // The game appends the world-space positions after the screen vertices.
        // Box of the quad's world points; only the lights of the cells under the box get in, and among those only the ones whose radius reaches the box.
        static const Light* near_ls[48]; // static: no compiler fill on each draw
        u32 nnear = 0;
        if (gActiveCount)
        {
            float bx0 = v[4].x, bx1 = v[4].x, by0 = v[4].y, by1 = v[4].y, bz0 = v[4].z, bz1 = v[4].z;
            for (int i = 1; i < count; ++i)
            {
                const Vert& w = v[i + 4];
                if (w.x < bx0) bx0 = w.x;
                if (w.x > bx1) bx1 = w.x;
                if (w.y < by0) by0 = w.y;
                if (w.y > by1) by1 = w.y;
                if (w.z < bz0) bz0 = w.z;
                if (w.z > bz1) bz1 = w.z;
            }
            ++gDrawStamp;
            const int cx0 = BinCell(bx0), cx1 = BinCell(bx1), cy0 = BinCell(by0), cy1 = BinCell(by1);
            for (int cy = cy0; cy <= cy1 && nnear < 48; ++cy)
            {
                for (int cx = cx0; cx <= cx1; ++cx)
                {
                    const int cell = (cx & (kBinDim - 1)) + (cy & (kBinDim - 1)) * kBinDim;
                    for (int e = gBinHead[cell]; e >= 0 && nnear < 48; e = gBinNext[e])
                    {
                        const u32 li = gBinLight[e];
                        if (gLightStamp[li] == gDrawStamp)
                        {
                            continue;
                        }
                        gLightStamp[li] = gDrawStamp;
                        const Light& l = gLights[li];
                        const float dx = l.x < bx0 ? bx0 - l.x : (l.x > bx1 ? l.x - bx1 : 0.0f);
                        const float dy = l.y < by0 ? by0 - l.y : (l.y > by1 ? l.y - by1 : 0.0f);
                        const float dz = l.z < bz0 ? bz0 - l.z : (l.z > bz1 ? l.z - bz1 : 0.0f);
                        if (dx * dx + dy * dy + dz * dz <= l.radiusSq)
                        {
                            near_ls[nnear++] = &l;
                        }
                    }
                }
            }
        }
        if (nnear == 0)
        {
            // no light nearby: ambient only (same for the 4 vertices)
            const u32 a = static_cast<u32>(gAmbient > 255 ? 255 : gAmbient < 0 ? 0 : gAmbient);
            const u32 amb = a | (a << 8) | (a << 16);
            for (int i = 0; i < count; ++i)
            {
                v[i].diff = (v[i].diff & 0xFF000000) | amb;
            }
        }
        else
        {
            const float m = static_cast<u8>(baseColour) * (1.0f / 255.0f);
            if (gN64Lights >= 2)
            {
            // smooth (gouraud) mode: sum per world corner, with a per-frame cache
            for (int i = 0; i < count; ++i)
            {
                v[i].diff = LitApply(CachedSum(v[i + 4], near_ls, nnear), m, v[i].diff);
            }
            }
            else
            {
            // default mode: one light sum at the quad center, applied to the 4 vertices (flat per-block lighting)
            Vert ctr = v[4];
            for (int i = 1; i < count; ++i)
            {
                ctr.x += v[i + 4].x, ctr.y += v[i + 4].y, ctr.z += v[i + 4].z;
            }
            const float inv = 1.0f / static_cast<float>(count);
            ctr.x *= inv, ctr.y *= inv, ctr.z *= inv;
            const LightSum L = SumLights(ctr, near_ls, nnear);
            for (int i = 0; i < count; ++i)
            {
                v[i].diff = LitApply(L, m, v[i].diff);
            }
            }
        }
    }
#if N64_DEBUG_OVERLAY
    gta2_prof_add(18, prof_lights_t0); // PROF_LIGHTS
    const u32 prof_capture_t0 = gta2_prof_now();
#endif
    const u32 blend = (flags & kAdditive) ? kBlendAdditive : (flags & kAlphaBlend) ? kBlendAlpha : kBlendOpaque;
#ifdef N64
    // N64: converts the texture now (the engine reuses shared, damage and digit textures within the same frame)
    u32 texId;
    {
        const float ax = v[1].x - v[0].x, ay = v[1].y - v[0].y, bx = v[2].x - v[0].x, by = v[2].y - v[0].y;
        const float a_scr = fabsf(ax * by - bx * ay);
        const float a_tex = fabsf((v[1].u - v[0].u) * (v[2].v - v[0].v) - (v[2].u - v[0].u) * (v[1].v - v[0].v));
        texId = gta2_n64_tex_capture(reinterpret_cast<N64Tex*>(tex->field_1C_ptr), a_scr, a_tex, blend == kBlendAdditive);
    }
#if N64_DEBUG_OVERLAY
    gta2_prof_add(17, prof_capture_t0); // PROF_CAPTURE
#endif
    if (gta2_n64_tex_token_empty(texId)) // texture with no visible pixel: would draw nothing
    {
        return;
    }
#else
    const u32 texId = static_cast<u32>(reinterpret_cast<intptr_t>(tex->field_1C_ptr));
#endif
    static const int kFan[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (int t = 0; t < count - 2; ++t)
    {
        for (int c = 0; c < 3; ++c)
        {
            const Vert& s = v[kFan[t][c]];
            const u32 d = s.diff;
            FrameVertex fv = {s.x - 0.5f, s.y - 0.5f, s.w, s.u / tw, s.v / th,
                              ((d >> 16) & 0xFF) | (d & 0xFF00) | ((d & 0xFF) << 16) | (d & 0xFF000000u)};
            gta2_frame_push_vertex(texId, blend, fv);
        }
    }
}

// Tiles: default UVs cover the 64x64 tile; flags rotate (0x20/0x40/0x60) and
// flip (0x08 vertical, 0x10 horizontal) the mapping.
void DrawTile(u32 flags, STexture* tex, Vert* p, u32 colour)
{
    u32 drawFlags = flags;
    if (!(flags & kTileUVGiven))
    {
        const float lo = 0.5f, hi = 63.4999f;
        p[0].u = lo, p[0].v = lo;
        p[1].u = hi, p[1].v = lo;
        p[2].u = hi, p[2].v = hi;
        p[3].u = lo, p[3].v = hi;
    }
    float u[4], w[4];
    for (int i = 0; i < 4; ++i)
    {
        u[i] = p[i].u, w[i] = p[i].v;
    }
    auto assign = [&](const int (&order)[4]) {
        for (int i = 0; i < 4; ++i)
        {
            p[i].u = u[order[i]], p[i].v = w[order[i]];
        }
        for (int i = 0; i < 4; ++i)
        {
            u[i] = p[i].u, w[i] = p[i].v;
        }
    };
    switch (flags & 0x60)
    {
        case 0x20:
            assign({3, 0, 1, 2});
            break;
        case 0x40:
            drawFlags = flags ^ 0x18; // 180 degrees: both flips
            break;
        case 0x60:
            assign({1, 2, 3, 0});
            break;
    }
    if (drawFlags & 0x08)
    {
        assign({1, 0, 3, 2});
    }
    if (drawFlags & 0x10)
    {
        assign({3, 2, 1, 0});
    }
    Draw(drawFlags, tex, p, 4, colour);
}

u32 gGlobals[64];
int gScreenTable[1700];
} // namespace

// ---------------------------------------------------------------------------
// Exported interface. Signatures match the game's T_* typedefs exactly
// (gbh_graphics.hpp); exports.cpp checks that at compile time.
extern "C" {

void CC ConvertColourBank(BYTE*)
{
}
s32 CC DrawLine(s32, s32 a2, s32, s32 a4, s32)
{
    return abs(a4 - a2);
}
void CC SetShadeTableA(s32, s32, s32, s32, s32)
{
}
s32* CC MakeScreenTable(s32 value, s32 step, u32 count)
{
    s32* p = gScreenTable;
    for (u32 i = 0; i < count && i < 1700; ++i)
    {
        *p++ = value;
        value += step;
    }
    return p;
}

u32 CC gbh_InitDLL(SVideo*)
{
    static const u32 kCacheDefaults[12] = {10, 62, 62, 88, 1, 0, 110, 126, 126, 40, 10, 0};
    memcpy(&gGlobals[40], kCacheDefaults, sizeof(kCacheDefaults));
    return 1;
}
s32 CC gbh_Init(s32)
{
    return 0;
}
void CC gbh_CloseDLL()
{
}
void CC gbh_CloseScreen(SVideo*)
{
}
u32* CC gbh_GetGlobals()
{
    return gGlobals;
}
s32 CC gbh_GetUsedCache(s32)
{
    return 0;
}
s32 CC gbh_SetColourDepth()
{
    return 1;
}
s32 CC gbh_SetCamera(float, float, float, float)
{
    return 0;
}
void CC gbh_SetWindow(float left, float top, float right, float bottom)
{
    gWinLeft = left, gWinTop = top, gWinRight = right, gWinBottom = bottom;
}
#ifndef N64_AMBIENT_FLOOR
#define N64_AMBIENT_FLOOR 100 /* 0..255: minimum light (the PC ambient is quite dark; on N64 the picture is small and the TV darkens it further). 0 = like PC */
#endif
void CC gbh_SetAmbient(float a)
{
    gAmbient = a * 255.0f;
#ifdef N64
    if (gAmbient < static_cast<float>(N64_AMBIENT_FLOOR))
    {
        gAmbient = static_cast<float>(N64_AMBIENT_FLOOR);
    }
#endif
}
void CC gbh_ResetLights()
{
    gLightCount = 0;
    gActiveCount = 0;
    gBinUsed = 0;
    if (++gLEpoch == 0) { memset(gLCache, 0, sizeof(gLCache)); gLEpoch = 1; }
    memset(gBinHead, 0xFF, sizeof(gBinHead)); // -1 = celula vazia
}
s32 CC gbh_AddLight(SLight* l)
{
    if (gLightCount >= 256)
    {
        return -1;
    }
    Light& d = gLights[gLightCount];
    const float k = 1.0f / 255.0f;
    d.flags = l->field_0;
    d.brightness = (l->field_0 & 0xFF) * k;
    d.x = l->field_4_x, d.y = l->field_8_y, d.z = l->field_C_z;
    d.r = ((l->field_10_colour >> 16) & 0xFF) * k;
    d.g = ((l->field_10_colour >> 8) & 0xFF) * k;
    d.b = (l->field_10_colour & 0xFF) * k;
    d.radius = ((l->field_0 >> 8) & 0xFF) * k * 8.0f;
    d.radiusSq = d.radius * d.radius;
    d.invRadius = d.radius > 0 ? 1.0f / d.radius : 0;
    d.invRadiusSq = d.radiusSq > 0 ? 1.0f / d.radiusSq : 0;
#ifndef N64_LIGHT_GAIN
#define N64_LIGHT_GAIN 0.6f /* strength of coloured lights (1.0 = PC). The N64 picture is smaller/more saturated: below 1 avoids blown-out red/neon */
#endif
    d.pr = d.r * d.brightness * N64_LIGHT_GAIN, d.pg = d.g * d.brightness * N64_LIGHT_GAIN, d.pb = d.b * d.brightness * N64_LIGHT_GAIN;
    if ((d.flags & 0x30000) == 0x10000)
    {
        gActiveLights[gActiveCount++] = static_cast<u16>(gLightCount);
        BinInsert(gLightCount);
    }
    return static_cast<s32>(gLightCount++ * 0x2C);
}

void CC gbh_BeginScene()
{
}
void CC gbh_EndScene()
{
}
void CC gbh_BeginLevel()
{
}
void CC gbh_EndLevel()
{
}

#ifdef N64
// Current physical palette pointer (nullptr if not yet registered). The renderer calls this on each draw: a texture
// created BEFORE its palette was registered (or with the palette re-registered later) still finds the right palette.
extern "C" const uint32_t* gta2_n64_palette_ptr(int id)
{
    if (id < 0 || id >= kMaxPalettes || !gPalettes || !gPalettes[id].loaded)
    {
        return nullptr;
    }
    return reinterpret_cast<const uint32_t*>(gPalettes[id].original);
}

// Called before registering a level's palettes. Never frees or moves the old tables (pointers already handed to the
// engine stay valid); only grows, copying the entries.
extern "C" void gta2_n64_palettes_reserve(int n)
{
    if (n > 16384)
    {
        n = 16384;
    }
    if (n <= kMaxPalettes)
    {
        return;
    }
    Palette* np = static_cast<Palette*>(calloc(static_cast<size_t>(n), sizeof(Palette)));
    u16 (*ns)[4] = static_cast<u16 (*)[4]>(calloc(static_cast<size_t>(n), 4 * sizeof(u16)));
    if (!np || !ns)
    {
        free(np);
        free(ns);
        return;
    }
    if (gPalettes)
    {
        memcpy(np, gPalettes, static_cast<size_t>(kMaxPalettes) * sizeof(Palette));
        memcpy(ns, gStub, static_cast<size_t>(kMaxPalettes) * 4 * sizeof(u16));
        // the old palettes already point at the old stub: we copy the pointers too, so the old stub stays alive (not freed)
    }
    gPalettes = np;
    gStub = ns;
    kMaxPalettes = n;
}
#endif

u32 CC gbh_RegisterPalette(s32 id, DWORD* data)
{
    if (id < 0 || id >= kMaxPalettes)
    {
#ifdef N64
        PalReject("RegisterPalette", id);
#endif
        return 0;
    }
    DWORD* p = data;
#ifdef N64
    // Palettes mapped from ROM are read-only: writing to them becomes a cartridge bus write
    // ("PI writing in progress"). The renderer already treats index 0 as transparent.
    if (reinterpret_cast<uintptr_t>(data) < 0xB0000000u)
#endif
    for (int i = 0; i < 256; ++i, p += 64)
    {
        if (i == 0)
        {
            *p = 0; // colour 0 is transparent
        }
        else if (*p == 0)
        {
            *p = 0x10000; // keeps other black entries opaque
        }
    }
    Palette& pal = gPalettes[id];
#ifdef N64
    // No renderer: does not convert or allocate 1 KB per palette; all point to a shared block.
    {
        const u32 sid = static_cast<u32>(id);
        memcpy(&gStub[id][0], &sid, 4);
        pal.data565 = &gStub[id][2];
        pal.original = data;
        pal.loaded = 1;
        return static_cast<u32>(id);
    }
#endif
    free(pal.data565 ? pal.data565 - 2 : nullptr);
    u16* block = static_cast<u16*>(malloc((2 + 512) * sizeof(u16)));
    const u32 id32 = static_cast<u32>(id);
    memcpy(block, &id32, 4);
    pal.data565 = block + 2;
    pal.original = data;
    pal.loaded = 1;
    for (int i = 0; i < 256; ++i)
    {
        pal.data565[i] = pal.data565[256 + i] = ToRGB565(data[i * 64]);
    }
    return static_cast<u32>(id);
}
void CC gbh_FreePalette(s32 id)
{
    if (id >= 0 && id < kMaxPalettes && gPalettes[id].data565)
    {
#ifndef N64
        free(gPalettes[id].data565 - 2);
#endif
        gPalettes[id] = Palette();
    }
}


STexture* CC gbh_RegisterTexture(s32 width, s32 height, BYTE* data, s32 palId, char_type)
{
    STexture* t = NewTexture(width, height, data);
    if (palId >= 0 && palId < kMaxPalettes)
    {
        t->field_12_bPalIsValid = static_cast<u8>(gPalettes[palId].loaded);
        t->field_18_pPaltData = gPalettes[palId].data565;
    }
#ifdef N64
    else
    {
        PalReject("RegisterTexture", palId);
    }
#endif
    UploadPaletted(t);
    return t;
}
void CC gbh_AssignPalette(STexture* t, s32 palId)
{
    if (palId < 0 || palId >= kMaxPalettes)
    {
#ifdef N64
        PalReject("AssignPalette", palId);
#endif
        return;
    }
    t->field_18_pPaltData = gPalettes[palId].data565;
    t->field_12_bPalIsValid = static_cast<u8>(gPalettes[palId].loaded);
    if (!(t->field_13_flags & 1))
    {
        UploadPaletted(t);
    }
}
STexture* CC gbh_LockTexture(STexture* t)
{
    t->field_8_locked_pixels_ptr = t->field_14_original_pixel_data_ptr;
    t->field_13_flags |= 1;
    t->field_6_pal_size = 256;
    return t;
}
STexture* CC gbh_UnlockTexture(STexture* t)
{
    t->field_6_pal_size = 0;
    t->field_8_locked_pixels_ptr = nullptr;
    t->field_13_flags &= 0xFE;
    UploadPaletted(t);
    return t;
}
void CC gbh_FreeTexture(STexture* t)
{
#ifdef N64
    gta2_n64_tex_forget(reinterpret_cast<const N64Tex*>(t->field_1C_ptr));
    free(t->field_1C_ptr); // N64Tex descriptor
#endif
    free(t);
}

s32 CC gbh_InitImageTable(s32 size)
{
    gImages = static_cast<ImageEntry*>(calloc(size, sizeof(ImageEntry)));
    gImageCount = gImages ? static_cast<u32>(size) : 0;
    return gImages ? 0 : -1;
}
s32 CC gbh_FreeImageTable()
{
    for (u32 i = 0; i < gImageCount; ++i)
    {
        if (gImages[i].loaded)
        {
            gbh_FreeTexture(gImages[i].tex);
            free(gImages[i].pixels);
        }
    }
    free(gImages);
    gImages = nullptr;
    gImageCount = 0;
    return 0;
}
#ifdef N64
// N64: the menu screens (16-bit TGA, up to 640x480) become, at load time, a copy at HALF the resolution (2x2 average) in
// RGBA5551, 64-byte aligned, which the renderer draws with the RDP (render_n64.c, kind 3 command). Half resolution because the output is
// 320x240 (menu in 640x480). The 16-bit TGA is stored bottom-up and, in ROM, big-endian (tools/le2be.py).
// The copy only exists while the menu is open (gbh_FreeImageTable frees it).
static u8* ImageHalf16(const u8* src, int w, int h)
{
    const int hw = (w + 1) / 2, hh = (h + 1) / 2;
    const size_t bytes = (static_cast<size_t>(hw) * hh * 2 + 63) & ~static_cast<size_t>(63);
    u16* out = static_cast<u16*>(memalign(64, bytes));
    if (!out)
    {
        return nullptr;
    }
    for (int y = 0; y < hh; ++y)
    {
        const int ya = h - 1 - 2 * y;
        const int yb = ya > 0 ? ya - 1 : ya;
        for (int x = 0; x < hw; ++x)
        {
            const int xa = 2 * x;
            const int xb = xa + 1 < w ? xa + 1 : xa;
            const int ys[2] = {ya, yb};
            const int xs[2] = {xa, xb};
            unsigned r = 0, g = 0, b = 0;
            for (int j = 0; j < 2; ++j)
            {
                for (int i = 0; i < 2; ++i)
                {
                    const u8* q = src + (static_cast<size_t>(ys[j]) * w + xs[i]) * 2;
                    const unsigned v = (static_cast<unsigned>(q[0]) << 8) | q[1];
                    r += (v >> 10) & 31;
                    g += (v >> 5) & 31;
                    b += v & 31;
                }
            }
            out[static_cast<size_t>(y) * hw + x] = static_cast<u16>(((r >> 2) << 11) | ((g >> 2) << 6) | ((b >> 2) << 1) | 1);
        }
    }
    data_cache_hit_writeback_invalidate(out, bytes);
    return reinterpret_cast<u8*>(out);
}
// used by render_n64.c: reduced copy of image 'idx' (width/height already halved), or nullptr
extern "C" const uint16_t* gta2_n64_image_get(int idx, int* w, int* h)
{
    if (idx < 0 || static_cast<u32>(idx) >= gImageCount || !gImages[idx].loaded || !gImages[idx].pixels)
    {
        return nullptr;
    }
    *w = static_cast<int>((gImages[idx].w + 1) / 2);
    *h = static_cast<int>((gImages[idx].h + 1) / 2);
    return reinterpret_cast<const uint16_t*>(gImages[idx].pixels);
}
#endif
#ifdef N64
extern "C" const void* gta2_n64_map_file(const char* path, long offset);
extern "C" void gta2_n64_rom_copy(void* dst, const void* src, unsigned len);
// N64: loads a menu screen (16-bit TGA in ROM, big-endian) DIRECTLY from ROM, without the engine's 600 KB buffer (fopen/malloc/fread/free):
// reads only two rows at a time (PI DMA to a fixed aligned buffer) and immediately writes the half-size copy (ImageHalf16).
// Returns the index in the image table (>= 0) or < 0 on error.
extern "C" s32 gta2_n64_image_from_rom(const char* path)
{
    const u8* rom = static_cast<const u8*>(gta2_n64_map_file(path, 0));
    if (!rom)
    {
        return -3;
    }
    u8 hdr[24] __attribute__((aligned(8)));
    gta2_n64_rom_copy(hdr, rom, 20);
    const int idlen = hdr[0];
    const int w = (hdr[12] << 8) | hdr[13];
    const int h = (hdr[14] << 8) | hdr[15];
    if (hdr[1] != 0 || hdr[2] != 2 || hdr[16] != 16 || w <= 0 || h <= 0 || w > 640 || h > 480)
    {
        return -2;
    }
    for (u32 i = 0; i < gImageCount; ++i)
    {
        if (gImages[i].loaded)
        {
            continue;
        }
        const int hw = (w + 1) / 2, hh = (h + 1) / 2;
        const size_t bytes = (static_cast<size_t>(hw) * hh * 2 + 63) & ~static_cast<size_t>(63);
        u16* out = static_cast<u16*>(memalign(64, bytes));
        if (!out)
        {
            return -4;
        }
        static u8 rowbuf[2 * 640 * 2 + 32] __attribute__((aligned(16)));
        const uint32_t pix0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rom) & 0x1FFFFFFFu) + 18u + static_cast<uint32_t>(idlen);
        for (int y = 0; y < hh; ++y)
        {
            const int ya = h - 1 - 2 * y;          // 16-bit TGA: bottom-up
            const int yb = ya > 0 ? ya - 1 : ya;
            const uint32_t pi = pix0 + static_cast<uint32_t>(yb) * static_cast<uint32_t>(w) * 2u;
            const unsigned len = static_cast<unsigned>(ya - yb + 1) * static_cast<unsigned>(w) * 2u;
            u8* base = rowbuf + (pi & 7u);          // same alignment (mod 8) as the ROM, a dma_read requirement
            data_cache_hit_writeback_invalidate(base, len);
            dma_read(base, pi, len);
            const u8* rowA[2] = {base + static_cast<size_t>(w) * 2u * (ya - yb), base}; // [0] = row ya, [1] = row yb
            for (int x = 0; x < hw; ++x)
            {
                const int xa = 2 * x;
                const int xb = xa + 1 < w ? xa + 1 : xa;
                unsigned r = 0, g = 0, b = 0;
                for (int j = 0; j < 2; ++j)
                {
                    const u8* row = rowA[j];
                    const int xs[2] = {xa, xb};
                    for (int k = 0; k < 2; ++k)
                    {
                        const unsigned v = (static_cast<unsigned>(row[xs[k] * 2]) << 8) | row[xs[k] * 2 + 1];
                        r += (v >> 10) & 31;
                        g += (v >> 5) & 31;
                        b += v & 31;
                    }
                }
                out[static_cast<size_t>(y) * hw + x] = static_cast<u16>(((r >> 2) << 11) | ((g >> 2) << 6) | ((b >> 2) << 1) | 1);
            }
        }
        data_cache_hit_writeback_invalidate(out, bytes);
        STexture* t = NewTexture(w, h, nullptr);
        gImages[i] = {TRUE, static_cast<DWORD>(w), static_cast<DWORD>(h), t, reinterpret_cast<u8*>(out)};
        return static_cast<s32>(i);
    }
    return -1;
}
#endif
// SImage is a TGA header followed by the image id and 16-bit pixels.
s32 CC gbh_LoadImage(SImage* img)
{
    if (!img || img->field_1 || img->field_2 != 2 || img->field_10 != 16)
    {
        return -2;
    }
    for (u32 i = 0; i < gImageCount; ++i)
    {
        if (!gImages[i].loaded)
        {
            STexture* t = NewTexture(img->field_C_width, img->field_E_height, nullptr);
#ifndef N64
            t->field_1C_ptr = reinterpret_cast<void*>(static_cast<intptr_t>(gNextTextureId++)); // N64: field_1C_ptr is the N64Tex descriptor (freed in gbh_FreeTexture)
#endif
            const u8* src = reinterpret_cast<const u8*>(&img->field_12) + img->field_0;
            const size_t bytes = static_cast<size_t>(img->field_C_width) * img->field_E_height * 2;
#ifdef N64
            u8* copy = ImageHalf16(src, img->field_C_width, img->field_E_height); // copy at half resolution (150 KB at most), not the original 600 KB
#else
            u8* copy = static_cast<u8*>(malloc(bytes));
#endif
            if (copy)
            {
                memcpy(copy, src, bytes);
            }
            gImages[i] = {TRUE, img->field_C_width, img->field_E_height, t, copy};
            return static_cast<s32>(i);
        }
    }
    return -1;
}
s32 CC gbh_BlitImage(s32 index, s32 left, s32 top, s32 right, s32 bottom, s32 x, s32 y)
{
    if (index < 0 || static_cast<u32>(index) >= gImageCount || !gImages[index].loaded)
    {
        return -1;
    }
#ifdef N64
    if (!gImages[index].pixels)
    {
        return -1;
    }
    gta2_frame_blit(index, left, top, right, bottom, x, y);
    return 0;
#endif
    Vert v[8] = {};
    const float w = static_cast<float>(right - left), h = static_cast<float>(bottom - top);
    const float xs[4] = {0, w, w, 0}, ys[4] = {0, 0, h, h};
    for (int i = 0; i < 4; ++i)
    {
        v[i].x = x + xs[i];
        v[i].y = y + ys[i];
        v[i].z = v[i].w = 1.0f;
        v[i].u = left + xs[i];
        v[i].v = top + ys[i];
        v[i].diff = 0xFFFFFFFF;
    }
    Draw(kUseVertexDiffuse, gImages[index].tex, v, 4, 0xFF);
    return 0;
}

void CC gbh_DrawQuad(s32 flags, STexture* t, Vert* v, s32 colour)
{
    Draw(static_cast<u32>(flags), t, v, 4, static_cast<u32>(colour));
}
void CC gbh_DrawTriangle(s32 flags, STexture* t, Vert* v, u8 colour)
{
    Draw(static_cast<u32>(flags), t, v, 3, colour);
}
s32 CC gbh_DrawTilePart(u32 flags, STexture* t, Vert* v, s32 colour)
{
    gta2_frame_set_tile(1);
    gTileDraw = 1;
    DrawTile(flags, t, v, static_cast<u32>(colour));
    gTileDraw = 0;
    gta2_frame_set_tile(0);
    return 0;
}
s32 CC gbh_DrawTile(u32 flags, STexture* t, Vert* v, u8 colour)
{
    gta2_frame_set_tile(1);
    gTileDraw = 1;
    DrawTile(flags, t, v, colour);
    gTileDraw = 0;
    gta2_frame_set_tile(0);
    return 0;
}

// Present in the interface but not used by the game in practice.
s32 CC gbh_BlitBuffer(s32, s32, s32, s32, s32, s32)
{
    return 0;
}
void CC gbh_DrawQuadClipped(s32, s32, s32, s32, s32)
{
}
s32 CC gbh_DrawFlatRect(Vert*, s32)
{
    return 0;
}
void CC gbh_Plot(f32, f32, s32, s32)
{
}
s32 CC gbh_PrintBitmap(s32, s32)
{
    return 0;
}
u32 CC gbh_Convert16BitGraphic(s32, u32, WORD*, s32)
{
    return 0;
}
u32 CC gbh_ConvertColour(u8 r, u8 g, u8 b)
{
    return (static_cast<u32>(r) << 16) | (static_cast<u32>(g) << 8) | b;
}

} // extern "C"

