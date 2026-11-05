// C++ -> C bridge: main (main_n64.c, libdragon) calls these functions, which seed the
// registry and start the engine.
#include <string.h>
#include <stdio.h>
#include <windows.h>

#include "winmain.hpp"

int __stdcall WinMain_5E53F0(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd);
extern "C" int Gta2Port_RunLoopSlice();
extern "C" int Gta2Port_PauseState();
extern "C" void Gta2Port_QuitToMenu();
extern "C" int Gta2Port_InDemo();
extern "C" void Gta2Port_StaticInit();
extern "C" void gta2_port_reg_set_dword(const char* path, const char* name, DWORD value);
extern "C" void gta2_port_reg_set_string(const char* path, const char* name, const char* value);
extern "C" void gta2_port_key(int scancode, int vk, int down, int ch);
extern "C" int Gta2Port_InFrontend(); // winmain.cpp
extern "C" void gta2_ramfs_mount(void); // ramfs_n64.cpp
extern "C" void gta2_settings_load(void); // settings_n64.cpp
extern "C" void gta2_ramfs_tick(void);
extern "C" int gta2_n64_boot_map; // main_n64.c: -1 = menu, 0..2 = direct map

namespace
{
constexpr int kRunning = 0x7FFFFFFF;
const char kRegRoot[] = "hklm\\software\\dma design ltd\\gta2";

void SeedRegistry()
{
    char path[96];
    snprintf(path, sizeof(path), "%s\\screen", kRegRoot);
    gta2_port_reg_set_dword(path, "start_mode", 0);
    gta2_port_reg_set_dword(path, "do_play_movie", 0); // no Bink on N64
    // Controls (DirectInput scan codes), in the game's slot order.
    static const int controls[] = {0x11, 0x1f, 0x1e, 0x20, 0x26, 0x1c, 0x39, 0x24, 0x25, 0x17, 0x18, 0x19};
    snprintf(path, sizeof(path), "%s\\control", kRegRoot);
    for (int i = 0; i < 12; ++i)
    {
        char name[8];
        snprintf(name, sizeof(name), "%d", i);
        gta2_port_reg_set_dword(path, name, controls[i]);
    }
    gta2_port_reg_set_dword("hklm\\software\\freeloader.com\\gta2", "cityinstalled", 2);
    snprintf(path, sizeof(path), "%s\\debug", kRegRoot);
    // N64: opens the original GTA 2 menu by default (the engine picks the map from data\\test1.seq: City 1 = wil, 2 = ste, 3 = bil).
    // Debug ROMs: if the player held Z at power-on (main_n64.c), skips the menu and goes straight into the chosen map.
    static const char* const kMaps[3] = {"bil", "ste", "wil"};
    const int boot = gta2_n64_boot_map;
    const char* m = (boot >= 0 && boot < 3) ? kMaps[boot] : "bil";
    char name[16];
    if (boot >= 0 && boot < 3)
    {
        // WARNING: the engine only tests whether the "skip_frontend" key EXISTS (value ignored): to open the menu it must not be created.
        gta2_port_reg_set_dword(path, "skip_frontend", 1);
    }
    snprintf(name, sizeof(name), "%s.gmp", m);
    gta2_port_reg_set_string(path, "mapname", name);
    snprintf(name, sizeof(name), "%s.sty", m);
    gta2_port_reg_set_string(path, "stylename", name);
    snprintf(name, sizeof(name), "%s.scr", m);
    gta2_port_reg_set_string(path, "scriptname", name);
}
bool gStarted = false;
} // namespace

extern "C" void gta2_frame_reserve(void);             // frame_n64.cpp
extern "C" void gta2_n64_palettes_reserve(int n);     // gbh_n64.cpp
extern "C" void gta2_text_reserve(void);              // text_0x14.cpp

// 0 = running; != 0 = the game ended during initialization (exit code).
extern "C" int gta2_game_init(void)
{
    gta2_ramfs_mount();
    gta2_settings_load();
    SeedRegistry();
    Gta2Port_StaticInit(); // what the original CRT start-up did
    // What stays for the whole session, at the bottom of the heap: see gta2_n64_render_reserve
    gta2_frame_reserve();
    gta2_n64_palettes_reserve(512); // the cities have 320
    gta2_text_reserve();
    static char cmd[4] = "";
    const int r = WinMain_5E53F0(reinterpret_cast<HINSTANCE>(0x400000), nullptr, cmd, SW_SHOWNORMAL);
    if (r != kRunning)
    {
        return r ? r : 1;
    }
    gStarted = true;
    return 0;
}

// -1 = continue; any other value = game exit code.
extern "C" int gta2_game_frame(void)
{
    gta2_ramfs_tick();
    return gStarted ? Gta2Port_RunLoopSlice() : 1;
}

extern "C" void gta2_game_key(int scancode, int vk, int down)
{
    gta2_port_key(scancode, vk, down, 0);
}

// 0 = not paused, 1 = paused, 2 = showing the question of quitting the game
extern "C" int gta2_game_pause_state(void)
{
    return gStarted ? Gta2Port_PauseState() : 0;
}

extern "C" void gta2_game_quit_to_menu(void)
{
    if (gStarted)
    {
        Gta2Port_QuitToMenu();
    }
}

extern "C" int gta2_game_in_demo(void)
{
    return gStarted ? Gta2Port_InDemo() : 0;
}

// 1 = the GTA 2 menu is open (main_n64.c changes the button mapping between menu and game)
extern "C" int gta2_game_in_menu(void)
{
    return gStarted ? Gta2Port_InFrontend() : 0;
}

