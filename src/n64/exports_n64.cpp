// GetProcAddress table: the names the game looks up in d3ddll.dll and
// dmavideo.dll, mapped to the port's implementations.
//
// Every entry is static_cast to the game's own function-pointer typedef: a
// mismatch fails the build.
#include <string.h>

#include "dma_video.hpp"
#include "gbh_graphics.hpp"
#include "platform/port_api.h"

extern "C" {
void CC ConvertColourBank(BYTE*);
s32 CC DrawLine(s32, s32, s32, s32, s32);
void CC SetShadeTableA(s32, s32, s32, s32, s32);
s32* CC MakeScreenTable(s32, s32, u32);
s32 CC gbh_AddLight(SLight*);
void CC gbh_AssignPalette(STexture*, s32);
void CC gbh_BeginLevel();
void CC gbh_BeginScene();
s32 CC gbh_BlitBuffer(s32, s32, s32, s32, s32, s32);
s32 CC gbh_BlitImage(s32, s32, s32, s32, s32, s32, s32);
void CC gbh_CloseDLL();
void CC gbh_CloseScreen(SVideo*);
u32 CC gbh_Convert16BitGraphic(s32, u32, WORD*, s32);
u32 CC gbh_ConvertColour(u8, u8, u8);
s32 CC gbh_DrawFlatRect(Vert*, s32);
void CC gbh_DrawQuad(s32, STexture*, Vert*, s32);
void CC gbh_DrawQuadClipped(s32, s32, s32, s32, s32);
s32 CC gbh_DrawTile(u32, STexture*, Vert*, u8);
s32 CC gbh_DrawTilePart(u32, STexture*, Vert*, s32);
void CC gbh_DrawTriangle(s32, STexture*, Vert*, u8);
void CC gbh_EndLevel();
void CC gbh_EndScene();
s32 CC gbh_FreeImageTable();
void CC gbh_FreePalette(s32);
void CC gbh_FreeTexture(STexture*);
u32* CC gbh_GetGlobals();
s32 CC gbh_GetUsedCache(s32);
s32 CC gbh_Init(s32);
u32 CC gbh_InitDLL(SVideo*);
s32 CC gbh_InitImageTable(s32);
s32 CC gbh_LoadImage(SImage*);
STexture* CC gbh_LockTexture(STexture*);
void CC gbh_Plot(f32, f32, s32, s32);
s32 CC gbh_PrintBitmap(s32, s32);
u32 CC gbh_RegisterPalette(s32, DWORD*);
STexture* CC gbh_RegisterTexture(s32, s32, BYTE*, s32, char_type);
void CC gbh_ResetLights();
void CC gbh_SetAmbient(float);
s32 CC gbh_SetCamera(float, float, float, float);
s32 CC gbh_SetColourDepth();
void CC gbh_SetWindow(float, float, float, float);
STexture* CC gbh_UnlockTexture(STexture*);
}

namespace
{
struct Export
{
    const char* name;
    FARPROC fn;
};

#define GBH(name, type) {#name, reinterpret_cast<FARPROC>(static_cast<type>(&name))}

const Export kExports[] = {
    GBH(ConvertColourBank, T_ConvertColourBank),
    GBH(DrawLine, T_DrawLine),
    GBH(SetShadeTableA, T_SetShadeTableA),
    GBH(MakeScreenTable, T_MakeScreenTable),
    GBH(gbh_AddLight, T_gbh_AddLight),
    GBH(gbh_AssignPalette, T_gbh_AssignPalette),
    GBH(gbh_BeginLevel, T_gbh_BeginLevel),
    GBH(gbh_BeginScene, T_gbh_BeginScene),
    GBH(gbh_BlitBuffer, T_gbh_BlitBuffer),
    GBH(gbh_BlitImage, T_gbh_BlitImage),
    GBH(gbh_CloseDLL, T_gbh_CloseDLL),
    GBH(gbh_CloseScreen, T_gbh_CloseScreen),
    GBH(gbh_Convert16BitGraphic, T_gbh_Convert16BitGraphic),
    GBH(gbh_ConvertColour, T_gbh_ConvertColour),
    GBH(gbh_DrawFlatRect, T_gbh_DrawFlatRect),
    GBH(gbh_DrawQuad, T_gbh_DrawQuad),
    GBH(gbh_DrawQuadClipped, T_gbh_DrawQuadClipped),
    GBH(gbh_DrawTile, T_gbh_DrawTile),
    GBH(gbh_DrawTilePart, T_gbh_DrawTilePart),
    GBH(gbh_DrawTriangle, T_gbh_DrawTriangle),
    GBH(gbh_EndLevel, T_gbh_EndLevel),
    GBH(gbh_EndScene, T_gbh_EndScene),
    GBH(gbh_FreeImageTable, T_gbh_FreeImageTable),
    GBH(gbh_FreePalette, T_gbh_FreePalette),
    GBH(gbh_FreeTexture, T_gbh_FreeTexture),
    GBH(gbh_GetGlobals, T_gbh_GetGlobals),
    GBH(gbh_GetUsedCache, T_gbh_GetUsedCache),
    GBH(gbh_Init, T_gbh_Init),
    GBH(gbh_InitDLL, T_gbh_InitDLL),
    GBH(gbh_InitImageTable, T_gbh_InitImageTable),
    GBH(gbh_LoadImage, T_gbh_LoadImage),
    GBH(gbh_LockTexture, T_gbh_LockTexture),
    GBH(gbh_Plot, T_gbh_Plot),
    GBH(gbh_PrintBitmap, T_gbh_PrintBitmap),
    GBH(gbh_RegisterPalette, T_gbh_RegisterPalette),
    GBH(gbh_RegisterTexture, T_gbh_RegisterTexture),
    GBH(gbh_ResetLights, T_gbh_ResetLights),
    GBH(gbh_SetAmbient, T_gbh_SetAmbient),
    GBH(gbh_SetCamera, T_gbh_SetCamera),
    GBH(gbh_SetColourDepth, T_gbh_SetColourDepth),
    GBH(gbh_SetWindow, T_gbh_SetWindow),
    GBH(gbh_UnlockTexture, T_gbh_UnlockTexture),
    GBH(Vid_GetVersion, T_Vid_GetVersion),
    GBH(Vid_Init_SYS, T_Vid_Init_SYS),
    GBH(Vid_CheckMode, T_Vid_CheckMode),
    GBH(Vid_FindMode, T_Vid_FindMode),
    GBH(Vid_FindFirstMode, T_Vid_FindFirstMode),
    GBH(Vid_FindNextMode, T_Vid_FindNextMode),
    GBH(Vid_CloseScreen, T_Vid_CloseScreen),
    GBH(Vid_SetMode, T_Vid_SetMode),
    GBH(Vid_FlipBuffers, T_Vid_FlipBuffers),
    GBH(Vid_ReleaseSurface, T_Vid_ReleaseSurface),
    GBH(Vid_GrabSurface, T_Vid_GrabSurface),
    GBH(Vid_ShutDown_SYS, T_Vid_ShutDown_SYS),
    GBH(Vid_EnableWrites, T_Vid_EnableWrites),
    GBH(Vid_DisableWrites, T_Vid_DisableWrites),
    GBH(Vid_GetSurface, T_Vid_GetSurface),
    GBH(Vid_FreeSurface, T_Vid_FreeSurface),
    GBH(Vid_ClearScreen, T_Vid_ClearScreen),
    GBH(Vid_InitDLL, T_Vid_InitDLL),
    GBH(Vid_FindDevice, T_Vid_FindDevice),
    GBH(Vid_SetDevice, T_Vid_SetDevice),
    GBH(Vid_WindowProc, T_Vid_WindowProc),
    GBH(Vid_SetGamma, T_Vid_SetGamma),
};
#undef GBH
} // namespace

extern "C" FARPROC gta2_port_find_export(const char* name)
{
    for (const Export& e : kExports)
    {
        if (!strcmp(e.name, name))
        {
            return e.fn;
        }
    }
    return nullptr;
}
