# tools/vendor/engine

The prebuilt engine `tools/pack_rom.py` packs into the ROM:

| file | what |
|---|---|
| `gta2_recomp.elf.stripped` | the compiled game (compressed ELF) |
| `gta2_recomp.elf.sym` | symbols for crash backtraces |
| `intro/*.sprite` | the dragon logo shown at power-on |
| `libdragon.version` | libdragon version stamp |
| `rom.cfg` | ROM header settings |

They contain no game data and are the same for everyone, so they are built once and reused.

They are committed here, and each Release ships a copy that CI builds from source.

After changing the code, rebuild them with **`make engine`** at the repo root (needs the libdragon
toolchain and `bash setup_engine.sh`) and copy `build/engine/` over this folder. `pack_rom.py` uses
this folder first, then `build/engine/`.
