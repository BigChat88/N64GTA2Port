# N64GTA2Port

**Grand Theft Auto 2 (PC) running on the Nintendo 64.** The game logic from the
[`gta2_re`](https://github.com/CriminalRETeam/gta2_re) decompilation runs on the console on top of a custom
RDP renderer and audio, input and file layers built with [libdragon](https://github.com/n64brew/libdragon).
The code is based on [GTA-2-N64-Recomp](https://github.com/Sunshinin11/GTA-2-N64-Recomp).

**Requires the Expansion Pak (8 MB RAM).** A full ROM is about 62 MiB.

> **The game is not included.** This repository has no GTA 2 data, ROMs or original game binaries, and it does not
> redistribute the `gta2_re` sources. You supply your own copy of the PC game. The ROM you build contains that
> data, so it is for your personal use only: do not share it. Requests for game files or ROMs will be closed.
>
> Not affiliated with or endorsed by Rockstar Games or Take-Two Interactive.

## Build a ROM

No Docker and no N64 toolchain needed, only [Python 3](https://www.python.org/).

1. Clone this repository, or download and extract the latest **Release** zip (`N64GTA2Port-<version>.zip`). Both include the prebuilt engine and host tools for Windows and Linux x86-64 in `tools/vendor/`.
2. Copy the `data` folder of your GTA 2 for PC into `gamedata/`, including `data/movie/intro.bik` if you want the intro movie. See [Game files you need](#game-files-you-need) for the list, and [gamedata/README.md](gamedata/README.md) for the accepted layouts, which include a `.zip` of your install folder.
3. Run `build.cmd` on Windows, or `python3 tools/pack_rom.py` elsewhere.
4. Your ROM is at `output/GTA 2 N64.z64`.

The first run takes several minutes because it converts the radio stations and the intro. Later runs reuse them. Converting the intro and limiting the peaks of the radios needs [ffmpeg](https://ffmpeg.org/) on `PATH`; without it the ROM is built with no intro.

To use data stored elsewhere: `build.cmd --gamedata "C:\Games\GTA2"`.

### Game files you need

All of these come from the `data` folder of your GTA 2 for PC install. Copying the whole folder is the simplest way: other files in it are ignored.

| Files | What they are | Needed |
|---|---|---|
| `e.gxt`, `s.gxt` | English and Spanish text | `e.gxt` is required. Without `s.gxt` there is no Spanish |
| `fstyle.sty` | Menu and shared graphics | Yes |
| `bil.gmp` `bil.sty` `bil.scr`, `ste.*`, `wil.*` | The three cities: map, graphics, main script | Yes |
| `bil/`, `ste/`, `wil/` | Mission scripts of each city | Yes |
| `lorne2{e,m,h}.gmp` + `.scr`, `mike1{e,m,h}.gmp` + `.scr`, `mike2{e,m,h}.gmp` + `.scr` | Bonus levels | Yes |
| `nyc.gci` | Car data | Yes |
| `test1.seq` | Level sequence | Yes |
| `attract/` | Demo replays | Yes |
| `keyboard/` | Keyboard layouts | Yes |
| `frontend/` | Menu screens | Yes, unless built with `--no-frontend` |
| `audio/bil.*`, `ste.*`, `wil.*`, `fstyle.*` (`.raw` + `.sdt`) | Sound banks | Yes |
| `gtaudio/*.wav` | Radio stations: `1`–`12`, `5a`–`11a` and `101a` | Yes, unless built with fewer radios |
| `movie/intro.bik` | **Intro movie (FMV).** Converted at build time; needs [ffmpeg](https://ffmpeg.org/) on `PATH` | Optional: without it, or without ffmpeg, the ROM has no intro |

A missing file only prints a warning during the build, but the game may not work without it. `p.gxt` (Brazilian Portuguese text) is not part of the game: the build generates it from `e.gxt` and `assets/lang/pt_br.tsv`.

Run the ROM on a real N64 with an Expansion Pak and a compatible flashcart, or in an accurate emulator such as
[ares](https://ares-emu.net/) with the Expansion Pak enabled.

### Controls

| N64 | In game | In menus |
|---|---|---|
| D-pad | Move / turn | Navigate |
| Analog | Turn only | Navigate |
| A | Accelerate | Confirm |
| B | Reverse. While paused: asks whether to quit the game (then A quits, B goes back) | Back |
| Start | Pause | Confirm |
| Z / L | Attack | |
| R | Brake / jump | |
| C-left | Next weapon | |
| C-up | Enter / exit car. While paused: C-up + C-right + d-pad left together opens the cheat menu | |
| C-right | Next radio station | |
| C-down | Special (horn, etc.) | |

Hold **Z** at power-on to skip the menu: `Z` = Industrial (bil), `Z + C-down` = Residential (ste), `Z + C-left` = Downtown (wil).

## Compile from source

Needs [libdragon](https://github.com/n64brew/libdragon) (preview branch; CI uses the commit in
[tools/libdragon.pin](tools/libdragon.pin)) installed at `/opt/libdragon` or in `N64_INST`, plus `git`, `patch`,
`make` and `python3`. On Windows, use WSL.

```bash
bash setup_engine.sh   # once: downloads gta2_re at the pinned commit and applies the patch
bash build.sh          # compiles and writes output/GTA 2 N64.z64
bash build.sh engine   # or: only the engine, in build/engine/ (no game data needed)
bash build.sh clean
```

The decompiled sources use mixed-case `#include`s. On a case-sensitive file system, run
`python3 tools/ci_links.py` after `setup_engine.sh`. In WSL, a Windows drive path (`/mnt/c/...`) also works.

`bash setup_engine.sh --force` redoes the engine checkout. **Do not edit `src/gta2_re/` for changes you want to
keep**: it is generated. Port changes to the engine live in `assets/patches/gta2_re_n64.patch`.

After changing the code, run `bash build.sh engine` and copy `build/engine/` over `tools/vendor/engine/`: that prebuilt
copy is what `build.cmd` and `tools/pack_rom.py` pack into the ROM.


## Build options

Pass these to `make` or `build.sh`. Most stage options also exist on `tools/pack_rom.py` (see `--help`).

| Option | Meaning |
|---|---|
| `GAME_DATA=/path/to/data` | Where your GTA 2 data is (default `gamedata`) |
| `MAPS="bil ste wil"` | Cities to include (fewer = smaller ROM). `pack_rom.py --maps` |
| `INCLUDE_FRONTEND=0` | Leave out the menu screens (saves about 9.6 MB; hold Z at boot to enter a map). `pack_rom.py --no-frontend` |
| `INCLUDE_INTRO=0` | Leave out the intro movie (saves about 9.5 MB). `pack_rom.py --no-intro`, or `--intro-kbps` to change its quality |
| `RADIO_RATE=12000` | Radio sample rate in Hz (`8000` = smaller). `pack_rom.py --radio-rate` |
| `N64_INST=/path` | libdragon install location |
| `OVERSCAN_X=0` `OVERSCAN_Y=0` | Safe-area borders. The default `0` is the full image; on a TV that crops the edges, try `36` and `28` |

Compile-time tuning, passed as `EXTRA_CFLAGS="-DNAME=value ..."`. These require compiling from source:

| Define | Default | Meaning |
|---|---|---|
| `N64_CAM_ZOOM_QUALITY_PCT` | `116` | Camera height in quality mode. Higher = closer to the ground and faster |
| `N64_CAM_ZOOM_PERF_PCT` | `126` | Camera height in performance mode |
| `N64_POP_PCT` | `40` | Cars, as a percentage of the PC amount. Pedestrians are chosen on the Options page: 35 / 40 / 50%, and `N64_KF_PED_PCT` (`100`) during a kill frenzy |
| `N64_DEBUG_OVERLAY` | `0` | `1` shows FPS, free RAM, the save system, the pedestrian counts and the sound counters on screen |
| `N64_HUD_SCALE` | `1.25f` | Size of the HUD markers: bars, hearts, money, wanted level (`1.0f` = as on PC) |
| `N64_PAGER_SCALE` | `1.5f` | Size of the pagers: mission clock and counter (`1.0f` = as on PC) |
| `N64_HUD_INSET_LEFT` / `_RIGHT` / `_TOP` | `8.0f` / `16.0f` / `6.0f` | Distance of the HUD markers from the screen edges |
| `N64_HUD_SHARP` | `1` | HUD and in-game text use full-resolution textures and whole-pixel positions. `0` = like the world |
| `N64_GAME_320` | `1` | Gameplay at 320x240. `0` = gameplay at 640x240 |
| `N64_MENU_320` | `1` | With `N64_GAME_320=0`: menus at 320x240. `0` = menus at 640x240 too |
| `N64_LIGHTS` | `0` | Initial lighting: `0` off, `1` flat, `2` smooth. The Options menu time of day overrides it |
| `N64_LIGHT_GAIN` | `0.6` | Strength of coloured lights (`1.0` = PC) |
| `N64_AMBIENT_FLOOR` | `100` | Minimum ambient light, 0–255 (`0` = as dark as PC) |
| `N64_CACHE_N` | `192` | Converted-texture cache slots (4 KB each) |
| `N64_HEAP_RESERVE_KB` | `320` | RAM the renderer caches must leave free for the engine |

## Releases and versioning

The version lives in [VERSION](VERSION) and follows `MAJOR.MINOR.PATCH`. To publish a new version, change that file
and push to `main`.

[.github/workflows/release.yml](.github/workflows/release.yml) compiles the engine and the host tools on every pull
request as a check. On a push to `main` it also publishes a prerelease tagged `v<VERSION>`, with
`N64GTA2Port-<VERSION>.zip` attached. That zip holds the project's tracked files, the prebuilt engine, and the
Windows and Linux host tools. It holds no game data. Pushing again without changing `VERSION` updates the same release.

## Credits and licensing

- **DMA Design / Rockstar Games:** the original GTA 2. GTA 2 is property of Rockstar Games / Take-Two Interactive.
- **CriminalRETeam / `gta2_re`:** the decompilation this port runs on.
- **Sunshinin11 / [`GTA-2-N64-Recomp`](https://github.com/Sunshinin11/GTA-2-N64-Recomp):** the N64 port this code is based on.
- **M1sterMo / `gta2-browser`:** the browser port whose platform layer and compatibility headers parts of this work derive from (MIT).
- **libdragon:** N64 SDK and RDP, RSP and audio libraries.
- **N64brew / n64brew2025:** the libdragon logo animation (MIT).

This project's own work is under the MIT license: see [LICENSE](LICENSE) for what it covers and what it does not. See also [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
