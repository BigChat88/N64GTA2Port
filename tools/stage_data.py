#!/usr/bin/env python3
"""stage_data.py - stages your GTA 2 data for the ROM's DragonFS, with lowercase names.

.gxt/.sty/.gmp/.tga files are converted from little-endian (PC) to big-endian (N64) by tools/le2be.py,
ALWAYS from the original (never in place -> no risk of a double swap). Used by the Makefile and by
tools/pack_rom.py; only needs Python and audioconv64.

usage: stage_data.py --gamedata <data_folder> --out <fs_folder> --audioconv <audioconv64>
                     [--maps "bil ste wil"] [--bonus "..."] [--radios "..."] [--sfx-maps "bil ste wil"]
                     [--no-frontend] [--radio-rate 12000] [--radio-quality 35]
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
LANG = os.path.join(os.path.dirname(HERE), "assets", "lang")

# Maps (cities): bil = City 3 (Industrial), ste = City 2 (Residential), wil = City 1 (Downtown).
MAPS = "bil ste wil"
# Bonus levels of each city (listed in test1.seq: lorne2 = wil, mike1 = ste, mike2 = bil): .gmp + .scr
BONUS = "lorne2e lorne2m lorne2h mike1e mike1m mike1h mike2e mike2m mike2h"
# Radios (music): all numbered stations. A.wav and D.wav (the game's CD check) stay out until it is known where they are used.
RADIOS = "1 2 3 4 5 5a 6 6a 7 7a 8 8a 9 9a 10 10a 11 11a 12 101a"
# Effect banks merged into a single audio/sfx.raw (tools/sfx_share.py), instead of 6 MB per map.
SFX_MAPS = "bil ste wil"
# ULC quality of the intro's audio: speech and music, better than the radios (35) as it is short.
INTRO_AUDIO_QUALITY = 70
# Peak bitrate of the intro's video: its average is set by --intro-kbps. The peak is what the N64 has to keep up with while decoding.
INTRO_MAX_KBPS = 1200
# Ceiling for the radios and the intro. The mixer's resampler overshoots up to 25% and saturates before any volume (see
# BANK_GAIN in audio_le2be.py), and these do go through it: the real DAC rate is not exactly the file's (22048 Hz for 22050),
# so anything above 1/1.25 = 0.8 can clip on playback. Below that because alimiter lets peaks land ~3% over its limit and
# ULC does not reproduce them exactly.
STREAM_LIMIT = 0.72
# 12.wav is the city ambience heard on foot. Its right channel repeats part of the left one 95 ms later, so a mono downmix is a comb
# filter that sounds metallic. One channel is used instead, scaled to the loudness of the downmix (rms 1955 from 3151).
SINGLE_CHANNEL = {"12.wav": "pan=mono|c0=0.62*c1"}
# Lets a folder or zip be recognized as the game's data folder.
MARKER = "e.gxt"


def py(script, *args):
    subprocess.run([sys.executable, os.path.join(HERE, script), *args], check=True)


def entries(maps, bonus, radios, frontend):
    out = ["fstyle.sty", "e.gxt", "s.gxt"]
    for m in maps:
        out += [m + ".gmp", m + ".sty", m + ".scr", m]
    for b in bonus:
        out += [b + ".gmp", b + ".scr"]
    out += ["nyc.gci", "test1.seq", "attract", "keyboard"]
    if frontend:
        out.append("frontend")
    out += ["audio/bil.raw", "audio/bil.sdt", "audio/fstyle.raw", "audio/fstyle.sdt"]
    out += ["gtaudio/%s.wav" % r for r in radios]
    return out


def find_ci(folder, rel):
    """Case-insensitive lookup of a relative path; None if missing."""
    cur = folder
    for part in rel.split("/"):
        if not os.path.isdir(cur):
            return None
        match = next((n for n in os.listdir(cur) if n.lower() == part.lower()), None)
        if match is None:
            return None
        cur = os.path.join(cur, match)
    return cur


def walk_files(path):
    if os.path.isfile(path):
        return [path]
    found = []
    for dp, _, names in os.walk(path):
        found += [os.path.join(dp, n) for n in names]
    return sorted(found)


def is_junk(low):
    base = low.rsplit("/", 1)[-1]
    return base == ".ds_store" or low.startswith("old ") or "/old " in low


def master_wav(ffmpeg, src, dst, rate):
    """Writes src as a mono wav under STREAM_LIMIT. Everything happens in float: the intro's source goes over full scale."""
    # rematrix_maxval: the stereo downmix must not add gain (float samples are not normalized by default)
    af = "aresample=out_sample_rate=%d:out_chlayout=mono:out_sample_fmt=flt:rematrix_maxval=1,alimiter=limit=%s:level=disabled" % (rate, STREAM_LIMIT)
    channel = SINGLE_CHANNEL.get(os.path.basename(src).lower())
    if channel:
        af = channel + "," + af
    # bitexact, no metadata: audioconv64's wav reader only expects the fmt and data chunks
    subprocess.run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", src, "-vn", "-af", af, "-c:a", "pcm_s16le",
                    "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact", dst], check=True)


def convert_radio(src, dst, out_dir, audioconv, ffmpeg, rate, quality):
    print("    [RADIO] %s (ULC mono %s Hz)" % (os.path.basename(dst), rate), flush=True)
    compress = [audioconv, "--wav-compress", "ulc,quality=%s" % quality]
    # Run from the file's folder with a bare name: audioconv64 only splits paths on '/',
    # so a Windows path would end up in the output file name.
    if ffmpeg:
        with tempfile.TemporaryDirectory(prefix="gta2radio-") as tmp:
            master_wav(ffmpeg, src, os.path.join(tmp, os.path.basename(src)), rate)
            subprocess.run(compress + ["-o", os.path.abspath(out_dir), os.path.basename(src)],
                           cwd=tmp, stdout=subprocess.DEVNULL, check=True)
    else:
        subprocess.run(compress + ["--wav-mono", "--wav-resample", str(rate), "-o", os.path.abspath(out_dir), os.path.basename(src)],
                       cwd=os.path.dirname(src), stdout=subprocess.DEVNULL, check=True)
    # audioconv64 uses the original name (may have uppercase)
    produced = os.path.join(out_dir, os.path.splitext(os.path.basename(src))[0] + ".wav64")
    if os.path.basename(produced) != os.path.basename(dst):
        os.replace(produced, dst)


def stage_intro(src, data, out_base, audioconv, ffmpeg, kbps, quality):
    """Converts movie/intro.bik for the N64. Returns the files produced (none if the movie or ffmpeg is missing)."""
    bik = find_ci(src, "movie/intro.bik")
    video, audio = os.path.join(data, "intro.m1v"), os.path.join(data, "intro.wav64")
    if not bik:
        print("    [INTRO] no movie/intro.bik in the game data: the ROM will have no intro", flush=True)
        return []
    stamp = out_base + ".intro_cfg"
    st = os.stat(bik)
    cfg = "%d-%d-%d-%d-%s-deint" % (st.st_size, int(st.st_mtime), kbps, quality, STREAM_LIMIT)
    if os.path.isfile(video) and os.path.isfile(audio) and os.path.isfile(stamp) and open(stamp, encoding="utf-8").read().strip() == cfg:
        return [video, audio]
    ffmpeg = ffmpeg or shutil.which("ffmpeg")
    if not ffmpeg:
        print("    [INTRO] ffmpeg not found: the ROM will have no intro (install it to include the movie)", flush=True)
        return []
    print("    [INTRO] converting movie/intro.bik (%d kbit/s)" % kbps, flush=True)
    quiet = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", bik]
    # Part of the movie is interlaced: halving the height blends its two fields, moments apart, into a double image.
    # bwdif deinterlaces only the frames it detects as combed (bottom field first, the order measured with idet).
    # 29.97 fps: exactly twice the movie's 14.985 fps. MPEG-1 has no 15 fps, and libdragon's decoder only knows the standard rates.
    encode = quiet + ["-an", "-vf", "bwdif=mode=send_frame:parity=bff:deint=interlaced,scale=320:240:flags=lanczos", "-r", "30000/1001",
                      "-c:v", "mpeg1video", "-b:v", "%dk" % kbps, "-maxrate", "%dk" % INTRO_MAX_KBPS, "-bufsize", "400k", "-g", "30",
                      "-mbd", "rd", "-trellis", "2", "-cmp", "2", "-subcmp", "2", "-mpv_flags", "+mv0", "-last_pred", "2"]
    with tempfile.TemporaryDirectory(prefix="gta2intro-") as tmp:
        passlog = os.path.join(tmp, "pass")
        subprocess.run(encode + ["-pass", "1", "-passlogfile", passlog, "-f", "null", "-"], check=True)
        subprocess.run(encode + ["-pass", "2", "-passlogfile", passlog, "-f", "mpeg1video", video], check=True)
        master_wav(ffmpeg, bik, os.path.join(tmp, "intro.wav"), 22050)
        # bare name, as in convert_radio()
        subprocess.run([audioconv, "--wav-compress", "ulc,quality=%d" % quality, "-o", os.path.abspath(data), "intro.wav"],
                       cwd=tmp, stdout=subprocess.DEVNULL, check=True)
    with open(stamp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(cfg + "\n")
    return [video, audio]


def stage(src, out, audioconv, maps=MAPS.split(), bonus=BONUS.split(), radios=RADIOS.split(),
          sfx_maps=SFX_MAPS.split(), frontend=True, radio_rate=12000, radio_quality=35,
          intro=True, intro_kbps=640, ffmpeg=None):
    data = os.path.join(out, "data")
    os.makedirs(data, exist_ok=True)
    out_base = out.rstrip("/" + os.sep)

    ffmpeg = ffmpeg or shutil.which("ffmpeg")
    if not ffmpeg:
        print("WARNING: ffmpeg not found: the radios keep their full-scale peaks, which can clip in the mixer", flush=True)

    # radios: rate, quality and ceiling. If it changes (different stamp file), ALL radios are reconverted.
    radio_cfg = "ulc%s-mono-%s-%s-%s" % (radio_quality, radio_rate, STREAM_LIMIT if ffmpeg else "nolimit", SINGLE_CHANNEL if ffmpeg else "downmix")
    stamp = out_base + ".radio_cfg"
    radio_force = True
    if os.path.isfile(stamp):
        radio_force = open(stamp, encoding="utf-8").read().strip() != radio_cfg

    keep = set()
    if sfx_maps:
        py("sfx_share.py", src, data, *sfx_maps)
        keep.add(os.path.join(data, "audio", "sfx.raw"))
        for b in sfx_maps:
            for ext in (".sdt", ".raw"):
                p = os.path.join(data, "audio", b + ext)
                if os.path.isfile(p):
                    keep.add(p)

    if intro:
        keep.update(stage_intro(src, data, out_base, audioconv, ffmpeg, intro_kbps, INTRO_AUDIO_QUALITY))

    with tempfile.TemporaryDirectory(prefix="gta2stage-") as tmp:
        # Every language gets the N64 menu text (assets/lang/menu.tsv).
        generated = {}
        for code in "es":
            base = find_ci(src, code + ".gxt")
            if not base:
                continue
            out_gxt = os.path.join(tmp, code + ".gxt")
            try:
                py("lang_gxt.py", base, out_gxt, code, os.path.join(LANG, "menu.tsv"))
                generated[code + ".gxt"] = out_gxt
            except subprocess.CalledProcessError:
                print("WARNING: could not prepare %s.gxt" % code, flush=True)

        for entry in entries(maps, bonus, radios, frontend):
            path = generated.get(entry) or find_ci(src, entry)
            if not path:
                print("WARNING: '%s' does not exist in %s" % (entry, src), flush=True)
                continue
            for f in walk_files(path):
                if entry in generated:
                    rel = entry
                else:
                    rel = os.path.relpath(f, src).replace(os.sep, "/")
                low = rel.lower()
                if is_junk(low):
                    continue
                dst = os.path.join(data, *low.split("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                ext = low.rsplit(".", 1)[-1]
                is_bank = low.startswith("audio/") and ext in ("raw", "sdt")
                is_radio = low.startswith("gtaudio/") and ext == "wav"
                if is_bank:
                    bank = os.path.basename(low).rsplit(".", 1)[0]
                    if bank in sfx_maps:
                        continue  # already produced by sfx_share.py
                    py("audio_le2be.py", f, dst)
                elif is_radio:
                    dst = dst[:-4] + ".wav64"
                    if radio_force or not os.path.isfile(dst) or os.path.getmtime(f) > os.path.getmtime(dst):
                        convert_radio(f, dst, os.path.dirname(dst), audioconv, ffmpeg, radio_rate, radio_quality)
                elif ext in ("gxt", "sty", "gmp", "tga"):
                    py("le2be.py", f, dst)
                    if ext == "sty":
                        py("fix_zone_font.py", dst)
                elif ext == "scr":
                    # mission scripts: field-by-field conversion (tools/scr_le2be.py + scr_table.json)
                    py("scr_le2be.py", f, dst)
                elif ext == "rep":
                    py("rep_le2be.py", f, dst)
                elif not os.path.isfile(dst) or os.path.getmtime(f) > os.path.getmtime(dst):
                    # not copy2: game files may be read-only and the next run must be able to replace them
                    shutil.copyfile(f, dst)
                keep.add(dst)

    # prune: removes from the DragonFS (build folder, regenerable) whatever was NOT staged in this run (e.g.: frontend/*.tga from old builds)
    keep = {os.path.normcase(os.path.normpath(p)) for p in keep}
    for f in walk_files(data):
        if os.path.normcase(os.path.normpath(f)) not in keep:
            os.remove(f)
            print("    [PRUNE] %s" % os.path.relpath(f, data), flush=True)
    for dp, dirs, files in os.walk(data, topdown=False):
        if dp != data and not dirs and not files:
            os.rmdir(dp)
    with open(stamp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(radio_cfg + "\n")


def add_options(ap):
    ap.add_argument("--maps", default=MAPS, help='cities to include (default: "%(default)s"; fewer = smaller ROM)')
    ap.add_argument("--bonus", default=BONUS, help="bonus levels to include")
    ap.add_argument("--radios", default=RADIOS, help="radio stations to include")
    ap.add_argument("--sfx-maps", default=SFX_MAPS, help="sound banks merged into audio/sfx.raw")
    ap.add_argument("--no-frontend", action="store_true", help="leave out the menu screens (debug ROMs only: a standard ROM cannot start a game without them)")
    ap.add_argument("--radio-rate", type=int, default=12000, help="radio sample rate in Hz (default: %(default)s; 8000 = smaller)")
    ap.add_argument("--radio-quality", type=int, default=35, help="radio ULC quality (default: %(default)s)")
    ap.add_argument("--no-intro", action="store_true", help="leave out the intro movie (about 9.5 MB)")
    ap.add_argument("--intro-kbps", type=int, default=640, help="intro video bitrate (default: %(default)s)")
    ap.add_argument("--ffmpeg", default=None, help="path to ffmpeg, needed to convert the intro and limit the radios (default: PATH)")


def options_kwargs(a):
    return dict(maps=a.maps.split(), bonus=a.bonus.split(), radios=a.radios.split(), sfx_maps=a.sfx_maps.split(),
                frontend=not a.no_frontend, radio_rate=a.radio_rate, radio_quality=a.radio_quality,
                intro=not a.no_intro, intro_kbps=a.intro_kbps, ffmpeg=a.ffmpeg)


def main():
    ap = argparse.ArgumentParser(description="Stage GTA 2 data for the ROM's DragonFS.")
    ap.add_argument("--gamedata", required=True, help="the game's data folder")
    ap.add_argument("--out", required=True, help="output filesystem folder")
    ap.add_argument("--audioconv", required=True, help="path to audioconv64")
    add_options(ap)
    a = ap.parse_args()
    src = find_ci(a.gamedata, MARKER) and a.gamedata or find_ci(a.gamedata, "data")
    if not src or not find_ci(src, MARKER):
        sys.exit("error: %s: GTA 2 data not found (no %s); see gamedata/README.md" % (a.gamedata, MARKER))
    stage(src, a.out, a.audioconv, **options_kwargs(a))


if __name__ == "__main__":
    main()
