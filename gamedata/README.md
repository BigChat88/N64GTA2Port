# gamedata/

Copy your own GTA 2 for PC files here. Nothing here is committed.

Any of these layouts works, in any letter case:

* the contents of the game's `data` folder directly in this folder;
* the `data` folder itself, or your whole install folder;
* a single `.zip` of your install folder.

Or leave this folder empty and point the build elsewhere:
`build.cmd --gamedata "C:\Games\GTA2"`.

## Files the build uses

All of them come from the game's `data` folder:

| Files | What they are |
|---|---|
| `e.gxt`, `s.gxt` | English and Spanish text (`e.gxt` is required) |
| `fstyle.sty` | Menu and shared graphics |
| `bil.gmp` `bil.sty` `bil.scr`, `ste.*`, `wil.*` | The three cities: map, graphics, main script |
| `bil/`, `ste/`, `wil/` | Mission scripts of each city |
| `lorne2{e,m,h}.gmp` + `.scr`, `mike1{e,m,h}.gmp` + `.scr`, `mike2{e,m,h}.gmp` + `.scr` | Bonus levels |
| `nyc.gci` | Car data |
| `test1.seq` | Level sequence |
| `attract/` | Demo replays |
| `keyboard/` | Keyboard layouts |
| `frontend/` | Menu screens |
| `audio/bil.*`, `ste.*`, `wil.*`, `fstyle.*` (`.raw` + `.sdt`) | Sound banks |
| `gtaudio/*.wav` | Radio stations: `1`–`12`, `5a`–`11a` and `101a` |
| `movie/intro.bik` | Intro movie (optional; converting it needs ffmpeg) |

Other files in the folder are ignored.
