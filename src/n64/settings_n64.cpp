// The settings of the Options page, kept in the FlashRAM through the same layer as the saved games (ramfs_n64.cpp).
// Without a chip, or without a valid file, the game starts with the default values.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>

extern "C" FILE* gta2_ramfs_open(const std::string& key, const char* mode); // ramfs_n64.cpp
extern "C" int gta2_n64_get_volume(int which);                              // audio_n64.c
extern "C" void gta2_n64_set_volume(int which, int v);
extern "C" int gta2_n64_get_lights(void); // gbh_n64.cpp
extern "C" void gta2_n64_set_lights(int v);
extern "C" int gta2_n64_get_mode(void); // Camera.cpp
extern "C" void gta2_n64_set_mode(int v);
extern "C" int gta2_n64_get_fps_limit(void); // main_n64.c
extern "C" void gta2_n64_set_fps_limit(int on);
extern "C" int gta2_n64_get_rumble(void); // main_n64.c
extern "C" void gta2_n64_set_rumble(int on);
extern "C" int gta2_n64_get_population(void); // Char_Pool.cpp
extern "C" void gta2_n64_set_population(int level);
extern "C" int gta2_n64_get_traffic(void); // Char_Pool.cpp
extern "C" void gta2_n64_set_traffic(int level);
extern char gN64Language; // text_0x14.cpp

namespace
{
const char kFile[] = "n64options";
const char kMagic[4] = {'G', '2', 'O', '1'}; // the last character is the version of this layout

struct Settings
{
    char magic[4];
    uint8_t music, effects, lights, mode, fpsLimit;
    char language;
    // last: files saved before these existed are shorter and stay valid
    uint8_t rumble, population, traffic;
};

Settings Current()
{
    Settings s;
    memcpy(s.magic, kMagic, sizeof(kMagic));
    s.music = static_cast<uint8_t>(gta2_n64_get_volume(0));
    s.effects = static_cast<uint8_t>(gta2_n64_get_volume(1));
    s.lights = static_cast<uint8_t>(gta2_n64_get_lights());
    s.mode = static_cast<uint8_t>(gta2_n64_get_mode());
    s.fpsLimit = static_cast<uint8_t>(gta2_n64_get_fps_limit());
    s.language = gN64Language;
    s.rumble = static_cast<uint8_t>(gta2_n64_get_rumble());
    s.population = static_cast<uint8_t>(gta2_n64_get_population());
    s.traffic = static_cast<uint8_t>(gta2_n64_get_traffic());
    return s;
}

bool LanguageInRom(char code)
{
    char path[24];
    snprintf(path, sizeof(path), "rom:/data/%c.gxt", code);
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        return false;
    }
    fclose(f);
    return true;
}

Settings gLastSaved;
} // namespace

// At boot, after the FlashRAM is mounted and before the engine loads its text.
extern "C" void gta2_settings_load(void)
{
    gLastSaved = Current();
    FILE* f = gta2_ramfs_open(kFile, "rb");
    if (!f)
    {
        return;
    }
    Settings s;
    s.rumble = 1;
    s.population = 0;
    s.traffic = 0;
    const size_t got = fread(&s, 1, sizeof(s), f);
    fclose(f);
    if (got + 3 < sizeof(s) || memcmp(s.magic, kMagic, sizeof(kMagic)) != 0)
    {
        return;
    }
    gta2_n64_set_volume(0, s.music);
    gta2_n64_set_volume(1, s.effects);
    gta2_n64_set_lights(s.lights);
    gta2_n64_set_mode(s.mode);
    gta2_n64_set_fps_limit(s.fpsLimit);
    gta2_n64_set_rumble(s.rumble);
    gta2_n64_set_population(s.population);
    gta2_n64_set_traffic(s.traffic);
    if ((s.language == 'e' || s.language == 's') && LanguageInRom(s.language))
    {
        gN64Language = s.language;
    }
    gLastSaved = Current();
}

// On leaving the Options page. The file reaches the chip a moment later (gta2_ramfs_tick).
extern "C" void gta2_settings_save(void)
{
    const Settings s = Current();
    if (memcmp(&s, &gLastSaved, sizeof(s)) == 0)
    {
        return;
    }
    FILE* f = gta2_ramfs_open(kFile, "wb");
    if (!f)
    {
        return;
    }
    const size_t put = fwrite(&s, 1, sizeof(s), f);
    fclose(f);
    if (put == sizeof(s))
    {
        gLastSaved = s;
    }
}
