#!/usr/bin/env python3
"""
pack_rom.py -- turn your own copy of GTA 2 (PC) into a Nintendo 64 ROM,
without the N64 toolchain.

It does everything the Makefile's ROM rule does *except* compile the engine.
The compiled engine (gta2_recomp.elf.stripped + .sym + logo sprites) holds no game data and is
the same for everyone - it is committed in tools/vendor/engine/, or build it
yourself with `make engine` (it lands in build/engine/, which is also searched).

    python tools/pack_rom.py
    python tools/pack_rom.py --gamedata "C:/Games/GTA2"
    python tools/pack_rom.py --gamedata gta2.zip

--gamedata may be a folder (your install folder works as-is) or a .zip; the
game's data folder is the one holding e.gxt, found anywhere inside, in any
letter case.

Requires the libdragon host tools (mkdfs, n64tool, ed64romconfig, audioconv64)
-- found via --tools-dir, $N64_INST/bin, tools/vendor/bin/ or PATH.
See tools/vendor/bin/README.md.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

import stage_data

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

ROM_NAME = "gta2_recomp"
ROM_OUTPUT = "GTA 2 N64.z64"
MAX_ROM_BYTES = 64 * 1024 * 1024
ENGINE_FILES = (f"{ROM_NAME}.elf.stripped", f"{ROM_NAME}.elf.sym")
# Fallback header settings if the engine folder has no rom.cfg (written by
# `make engine`, which takes them from the Makefile).
DEFAULT_ROM_CFG = {"title": "GTA 2 N64", "category": "N", "savetype": "flashram",
                   "expansionpak": "required", "regionfree": "1"}


def project_version() -> str:
    try:
        return (ROOT / "VERSION").read_text(encoding="utf-8").strip() or "0.0.0"
    except OSError:
        return "0.0.0"


def log(msg: str) -> None:
    print(msg, flush=True)


def die(msg: str):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd: list, **kw) -> None:
    log("  $ " + " ".join(str(c) for c in cmd))
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def exe(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def find_tool(name: str, extra_dir: Path | None) -> Path:
    cands: list = []
    if extra_dir:
        cands.append(extra_dir / exe(name))
    n64_inst = os.environ.get("N64_INST")
    if n64_inst:
        cands.append(Path(n64_inst) / "bin" / exe(name))
    cands.append(HERE / "vendor" / "bin" / exe(name))
    which = shutil.which(name)
    if which:
        cands.append(Path(which))
    for c in cands:
        if c.is_file():
            # git on Windows does not record the executable bit of the committed Linux tools
            if os.name != "nt" and not os.access(c, os.X_OK):
                c.chmod(c.stat().st_mode | 0o755)
            return c
    die(
        f"host tool '{name}' not found.\n"
        f"  looked in: {', '.join(str(c) for c in cands)}\n"
        f"  download a Release, build it with tools/vendor/bin/build-tools.sh,\n"
        f"  install the libdragon toolchain and set N64_INST, or pass --tools-dir"
    )


def find_engine(engine_dir: Path | None) -> Path:
    dirs = [engine_dir] if engine_dir else [HERE / "vendor" / "engine", ROOT / "build" / "engine"]
    for d in dirs:
        if all((d / f).is_file() for f in ENGINE_FILES):
            return d
    die(
        f"compiled engine not found ({' + '.join(ENGINE_FILES)}).\n"
        f"  looked in: {', '.join(str(d) for d in dirs)}\n"
        "  download a Release, build it with `make engine`,\n"
        "  or pass --engine-dir. See tools/vendor/engine/README.md"
    )


def read_rom_cfg(engine: Path) -> dict:
    cfg = dict(DEFAULT_ROM_CFG)
    f = engine / "rom.cfg"
    if f.is_file():
        for line in f.read_text(encoding="utf-8").splitlines():
            key, sep, value = line.partition("=")
            if sep:
                cfg[key.strip()] = value.strip()
    return cfg


def find_data_folder(root: Path) -> Path | None:
    """Shallowest folder under root holding the game's marker file, in any letter case."""
    found = []
    for dp, _, names in os.walk(root):
        if any(n.lower() == stage_data.MARKER for n in names):
            found.append(Path(dp))
    return min(found, key=lambda p: len(p.parts)) if found else None


def resolve_gamedata(explicit: Path | None, work: Path) -> Path:
    """Returns the game's data folder, extracting a .zip into the work folder if needed."""
    path = explicit or ROOT / "gamedata"
    if not path.exists():
        die(f"{path}: not a folder or a .zip")
    if path.is_dir():
        folder = find_data_folder(path)
        if folder:
            return folder
        zips = sorted(p for p in path.glob("*") if p.suffix.lower() == ".zip")
        if not explicit and len(zips) == 1:
            path = zips[0]
    if path.is_file() and path.suffix.lower() == ".zip":
        log(f"extracting {path}")
        target = work / "gamedata"
        with zipfile.ZipFile(path) as z:
            z.extractall(target)
        folder = find_data_folder(target)
        if folder:
            return folder
    die(
        f"{path}: GTA 2 data not found (no {stage_data.MARKER}).\n"
        "  copy it from your own copy of the game (see gamedata/README.md),\n"
        "  or point --gamedata at your install folder or a .zip of it"
    )


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Pack your own GTA 2 files + the prebuilt engine into a Nintendo 64 ROM.")
    ap.add_argument("--version", action="version", version=f"N64GTA2Port {project_version()}")
    ap.add_argument("--gamedata", type=Path, default=None,
                    help="folder or .zip with your GTA 2 files (default: gamedata/, "
                         "or the single .zip inside it)")
    ap.add_argument("--staged", type=Path, default=None,
                    help="use an already staged filesystem folder instead of --gamedata")
    ap.add_argument("--out", type=Path, default=None,
                    help=f"output ROM path (default: output/{ROM_OUTPUT})")
    ap.add_argument("--engine-dir", type=Path, default=None,
                    help="folder with the prebuilt engine (default: tools/vendor/engine, "
                         "then build/engine)")
    ap.add_argument("--tools-dir", type=Path, default=None,
                    help="folder with the libdragon host tools "
                         "(default: $N64_INST/bin, tools/vendor/bin, PATH)")
    ap.add_argument("--keep-work", action="store_true", help="keep the temporary work folder")
    stage_data.add_options(ap)
    args = ap.parse_args()

    log(f"N64GTA2Port {project_version()}")

    engine = find_engine(args.engine_dir)
    cfg = read_rom_cfg(engine)
    tool_names = ["mkdfs", "n64tool", "ed64romconfig", "audioconv64"]
    tools = {t: find_tool(t, args.tools_dir) for t in tool_names}
    out = args.out or (ROOT / "output" / ROM_OUTPUT)

    log(f"engine   : {engine}")
    for t, p in tools.items():
        log(f"tool     : {t:14s} {p}")

    work = Path(tempfile.mkdtemp(prefix="gta2pack-"))
    try:
        if args.staged:
            fsroot = args.staged
            log(f"[1/3] using staged filesystem {fsroot}")
        else:
            data = resolve_gamedata(args.gamedata, work)
            log(f"gamedata : {data}")
            # Kept between runs so the (slow) radio conversion is only done once.
            fsroot = ROOT / "build" / "filesystem"
            log(f"[1/3] staging filesystem in {fsroot} (the first run takes several minutes)")
            stage_data.stage(str(data), str(fsroot), str(tools["audioconv64"]), **stage_data.options_kwargs(args))

        logo = engine / "intro"
        if logo.is_dir():
            shutil.copytree(logo, fsroot / "intro", dirs_exist_ok=True)

        log("[2/3] mkdfs")
        dfs = work / f"{ROM_NAME}.dfs"
        run([tools["mkdfs"], dfs, fsroot])

        log("[3/3] n64tool + ed64romconfig")
        rom = work / f"{ROM_NAME}.z64"
        extra = sorted(engine.glob("*.version"))
        run([tools["n64tool"], "--toc", "--title", cfg["title"], "--category", cfg["category"], "--output", rom,
             "--align", "256", engine / ENGINE_FILES[0], engine / ENGINE_FILES[1], dfs, *extra])
        run([tools["ed64romconfig"], "--savetype", cfg["savetype"],
             "--expansionpak", cfg["expansionpak"],
             *(["--regionfree"] if cfg["regionfree"] == "1" else []), rom])

        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(rom), out)
        log(f"\nOK -> {out}  ({out.stat().st_size / 1024 / 1024:.1f} MiB)")
        if out.stat().st_size > MAX_ROM_BYTES:
            log(f"WARNING: larger than {MAX_ROM_BYTES // 1024 // 1024} MiB, the usual cartridge limit: "
                "use --radio-rate 8000, --intro-kbps or --no-intro")
    finally:
        if args.keep_work:
            log(f"work folder kept: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
