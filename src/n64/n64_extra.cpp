// Small pieces of the N64 platform layer.
#include <stdio.h>
#include <windows.h>
#include <string.h>
#include "ImGuiDebug.hpp"
#include "Network_20324.hpp"
#include "platform/port_api.h"

// ImGuiDebug.cpp is not in the build (depends on ImGui); winmain.cpp references the callback.
void CC ImGuiDebugDraw()
{
}

// Present hook: the Tiny3D renderer (render_t3d.c) overrides this weak symbol.
extern "C" __attribute__((weak)) void gta2_n64_present(const void* cmds, int cmdCount, const void* verts, int vertCount, int refWidth)
{
    (void)cmds; (void)cmdCount; (void)verts; (void)vertCount; (void)refWidth;
}

// DirectPlay lobby: no network on N64.
s32 Network_20324::cb_FillSessionList_519D30(Network_20324*, Network_NameList*)
{
    return 0;
}

extern "C" {
// Remaining Win32
HRESULT CoCreateInstance(REFCLSID, void*, DWORD, REFIID, LPVOID*)
{
    return -1; // no DirectPlay/DirectMusic
}
UINT GetDriveTypeA(LPCSTR)
{
    return 3; // DRIVE_FIXED // no CD: the audio is on the cartridge
}
int WideCharToMultiByte(UINT, DWORD, LPCWSTR w, int wl, LPSTR out, int ol, LPCSTR, LPBOOL)
{
    int n = 0;
    for (; (wl < 0 || n < wl) && w[n]; ++n)
    {
        if (out && n < ol)
            out[n] = w[n] < 0x100 ? (char)w[n] : '?';
    }
    if (wl < 0)
    {
        if (out && n < ol)
            out[n] = 0;
        ++n;
    }
    return n;
}
LPSTR lstrcatA(LPSTR a, LPCSTR b)
{
    return strcat(a, b);
}
}

// getcwd: the N64 has no working directory; the engine only uses it at start-up.
extern "C" char* getcwd(char* buf, size_t size)
{
    if (!buf || size < 6)
        return 0;
    strcpy(buf, "rom:/");
    return buf;
}
