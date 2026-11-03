#!/usr/bin/env bash
# Prepares the engine: downloads gta2_re (the GTA 2 decompilation) at the pinned commit, applies the N64 patch
# and generates the upper/lowercase compatibility headers. Run ONCE after cloning.
#   bash setup_engine.sh            (use --force to redo from scratch)
set -euo pipefail
cd "$(dirname "$0")"

UPSTREAM="https://github.com/CriminalRETeam/gta2_re.git"
PIN="02de8beeb766644417f195d360eba33af0ee491f"
DEST="src/gta2_re"

if [ -e "$DEST/Source" ]; then
  if [ "${1:-}" = "--force" ]; then
    echo "Redoing $DEST ..."; rm -rf "$DEST"
  else
    echo "$DEST already exists. To redo it: bash setup_engine.sh --force"; exit 0
  fi
fi

for t in git patch; do command -v "$t" >/dev/null || { echo "ERROR: install '$t' (e.g.: sudo apt install git patch)"; exit 1; }; done

mkdir -p "$DEST"
git -C "$DEST" init -q
git -C "$DEST" remote add origin "$UPSTREAM"
echo "Downloading gta2_re ($PIN) ..."
git -C "$DEST" fetch -q --depth 1 origin "$PIN"
git -C "$DEST" checkout -q FETCH_HEAD

echo "Applying assets/patches/gta2_re_n64.patch ..."
patch -p1 -d "$DEST" --dry-run -s < assets/patches/gta2_re_n64.patch >/dev/null || { echo "ERROR: the patch does not apply to this commit."; exit 1; }
patch -p1 -d "$DEST" -s < assets/patches/gta2_re_n64.patch

# Copies of the 2 headers whose names differ in case (for case-sensitive file systems)
mkdir -p src/compat_ci
cp "$DEST/Source/fix16.hpp"       src/compat_ci/Fix16.hpp
cp "$DEST/Source/miss2_0x11C.hpp" src/compat_ci/miss2_0x11c.hpp

echo "Engine ready. Next step: see README.md (Compile from source)."
