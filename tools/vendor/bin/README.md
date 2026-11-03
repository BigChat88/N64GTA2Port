# tools/vendor/bin

The libdragon host tools `tools/pack_rom.py` runs:

| tool | job |
|---|---|
| `mkdfs` | pack the game files into the DragonFS filesystem image |
| `n64tool` | assemble engine + symbols + filesystem into the `.z64` |
| `ed64romconfig` | write the save type / Expansion Pak flags into the ROM header |
| `audioconv64` | convert the radio stations to `.wav64` |

`pack_rom.py` looks for them in `--tools-dir`, `$N64_INST/bin`, this folder and then `PATH`.

## Getting them

* **They are already here**: this folder holds Windows `.exe`s and static Linux x86-64 binaries,
  built from the same libdragon as the prebuilt engine (see `../engine/README.md`).
* **Build them yourself** (for example on another platform) with a native C and C++ compiler:

  ```sh
  bash tools/fetch_libdragon.sh
  ./build-tools.sh                                                        # gcc/g++ on PATH
  CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ ./build-tools.sh   # cross to Windows
  ```
