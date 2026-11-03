// The DMA video driver interface (dmavideo.dll) for the N64.
//
// One device and one 640x480 mode: the game renders in its reference space
// and render_n64.c scales the result to the framebuffer. Presenting a frame
// hands the draw list to the renderer (frame_n64.cpp).
#include <stdlib.h>
#include <string.h>

#include "3rdParty/GTA2Hax/DmaVideo/DmaVideo.hpp"
#include "dma_video.hpp"
#include "render/frame.h"
#include "Game_0x40.hpp"
#include "winmain.hpp"

namespace
{
SVideo* gVideo = nullptr;
SVideoFunctions* gFuncs = nullptr;
SDisplayMode gMode = {};
SDevice gDevice = {};
s32 gEnumIdx = 0;
SVidVersion gVersion = {0x3FE7AE14, "GTA2 N64 port video driver"};
constexpr s32 kModeId = 1;
s32 gViewWidth = 640;
} // namespace

extern "C" {

SVidVersion* CC Vid_GetVersion()
{
    return &gVersion;
}

s32 CC Vid_InitDLL(HINSTANCE, SVideoFunctions* funcs)
{
    gFuncs = funcs;
    return 0;
}

SVideo* CC Vid_Init_SYS(HINSTANCE param1, u16 flags)
{
    gVideo = static_cast<SVideo*>(calloc(1, sizeof(SVideo)));
    gVideo->field_0 = 1;
    gVideo->field_4_flags = flags | 0x200;
    gVideo->field_14_display_mode_count_2_q = 1;
    gVideo->field_18_num_guids = 2;
    gVideo->field_78 = static_cast<u32>(reinterpret_cast<uintptr_t>(param1));
    gVideo->field_84_from_initDLL = gFuncs;
    gMode.field_0_display_mode_idx = kModeId;
    gMode.field_8_width = 640;
    gMode.field_C_height = 480;
    gMode.field_10_pitch = 640 * 2;
    gMode.field_14_rgb_bit_count = 16;
    gDevice.field_0_id = 1;
    gDevice.field_4_flags = 1; // keeps the game from minimising on focus loss
    gVideo->field_1C_num_display_modes = 1;
    return gVideo;
}

void CC Vid_ShutDown_SYS(SVideo* v)
{
    free(v);
    if (v == gVideo)
    {
        gVideo = nullptr;
    }
}

s32 CC Vid_CheckMode(SVideo* v, s32 width, s32 height, s32)
{
    return v && width == 640 && height == 480 ? kModeId : 0;
}

SDevice* CC Vid_FindDevice(SVideo*, s32)
{
    return &gDevice;
}

s32 CC Vid_SetDevice(SVideo*, s32)
{
    return 0;
}

SDisplayMode* CC Vid_FindMode(SVideo* v, s32 modeId)
{
    return v && modeId == kModeId ? &gMode : nullptr;
}

s32 CC Vid_FindFirstMode(SVideo* v, s32)
{
    gEnumIdx = 0;
    return Vid_FindNextMode(v);
}

s32 CC Vid_FindNextMode(SVideo* v)
{
    if (!v || gEnumIdx >= 1)
    {
        return 0;
    }
    return ++gEnumIdx;
}

s32 CC Vid_SetMode(SVideo* v, HWND hwnd, s32)
{
    if (!v)
    {
        return 1;
    }
    v->field_8_found_width = 640;
    v->field_C_found_height = 480;
    v->field_10_found_rgb_bit_count = 16;
    v->field_48_rect_right = gViewWidth;
    v->field_4C_rect_bottom = 480;
    v->field_5C = 5; // RGB 5:6:5 component widths, read by the Bink path
    v->field_60_g = 6;
    v->field_64_r = 5;
    v->field_68_b = 5;
    v->field_6C = 5;
    v->field_4C0_hwnd = hwnd;
    return 0;
}

void CC Vid_CloseScreen(SVideo*)
{
}
void CC Vid_GrabSurface(SVideo*)
{
}
void CC Vid_ReleaseSurface(SVideo*)
{
}
s32 CC Vid_EnableWrites(SVideo*)
{
    return 0;
}
s32 CC Vid_DisableWrites(SVideo*)
{
    return 0;
}
s32 CC Vid_GetSurface(SVideo*)
{
    return 0;
}
s32 CC Vid_FreeSurface(SVideo*)
{
    return 0;
}

void CC Vid_FlipBuffers(SVideo*)
{
    gta2_frame_present();
}

s32 CC Vid_ClearScreen(SVideo*, u8 r, u8 g, u8 b, s32 left, s32 top, s32 right, s32 bottom)
{
    gta2_frame_clear(r, g, b, left, top, right, bottom);
    return 0;
}

s32 CC Vid_SetGamma(SVideo*, f32, f32, f32)
{
    return 0;
}

s32 CC Vid_WindowProc(SVideo*, HWND, u32, WPARAM, LPARAM)
{
    return 0;
}

} // extern "C"
