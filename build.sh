#!/usr/bin/env bash
# Compile from source: bash build.sh            -> produces output/GTA 2 N64.z64
#                      bash build.sh engine     -> compiles only the engine (no game data needed)
#                      bash build.sh clean      -> cleans
# Requires libdragon (MIPS toolchain + n64.mk). Default path: /opt/libdragon (change with N64_INST=...).
# To build a ROM without any toolchain, use build.cmd or tools/pack_rom.py from a Release instead.
set -e
cd "$(dirname "$0")"
export N64_INST="${N64_INST:-/opt/libdragon}"
export PATH="$N64_INST/bin:$PATH"
[ -d src/gta2_re/Source ] || bash ./setup_engine.sh
if [ "${1:-}" = "clean" ]; then exec make clean; fi
if [ $# -eq 0 ]; then set -- all; fi
exec make -j"$(nproc)" "$@"
