@echo off
rem  build.cmd -- one-click GTA 2 N64 ROM build.
rem
rem  1. copy the "data" folder of your GTA 2 for PC into  gamedata\
rem     (or a .zip of your install folder)
rem  2. run this file
rem  3. the ROM appears in  output\GTA 2 N64.z64
rem
rem  Extra options are passed straight through, e.g.:
rem     build.cmd --gamedata "C:\Games\GTA2"
setlocal
cd /d "%~dp0"

set "PY=python"
where python >nul 2>&1 || set "PY=py -3"

%PY% tools\pack_rom.py %*
set "RC=%ERRORLEVEL%"

echo.
if not "%RC%"=="0" (
    echo Build FAILED ^(exit %RC%^).
) else (
    echo Done. Check output\ for your ROM.
)
pause
exit /b %RC%
