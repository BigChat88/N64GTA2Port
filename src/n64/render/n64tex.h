// Texture descriptor for the N64 renderer: the engine (gbh_n64.cpp) fills it, the renderer (render_n64.c) reads it.
// The pointer to this block lives in STexture::field_1C_ptr (it serves as the texture "id" in draw commands).
#pragma once
#include <stdint.h>
/* converted-texture cache slots (4 KB each); texture tokens = TEX_TOKEN_BASE + slot. Measured peak: ~135 in use. */
#ifndef N64_CACHE_N
#define N64_CACHE_N 192
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct N64Tex
{
    const uint8_t* pix;   // 8-bit pixels (palette), 256-byte rows (.sty pages)
    const uint32_t* pal;  // physical palette: entry i at pal[i*64], 0x00RRGGBB
    uint16_t w, h;
    uint16_t gen;         // bumped on each (re)fill: invalidates the converted copy
    int16_t slot;         // position in the converted-texture cache (-1 = none)
    int16_t palid;        // physical palette id (-1 = none): the renderer resolves the pointer at draw time
    uint8_t key1;         // 1 = palette index 1 is transparent (gang icon: the flat paint would cover the car)
} N64Tex;
#ifdef __cplusplus
}
#endif
