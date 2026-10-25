@echo off
rem Uncharted: The Nathan Drake Collection - stereo 3D for SR MONITORS (Acer SpatialLabs, Leia SR
rem panels): both eyes are drawn in one pass (multiview) and woven for the panel by the SR runtime
rem through bin\ps2sr.dll. For an ordinary 3D TV or 3D monitor (side-by-side input) use
rem Uncharted-3D-SBS.bat instead.
rem Put this file next to Uncharted.exe of the original PC port (uncharted-windows.zip: "Download
rem the latest version" at https://github.com/nautymac/uncharted_pc), with bin\bb-probe.exe and
rem bin\ps2sr.dll from the same 3D package (they replace / join the original bin\ files).
setlocal
cd /d "%~dp0"
if not exist "Uncharted.exe" (
  echo Uncharted.exe was not found next to this file.
  echo Unpack the original PC port here first: "Download the latest version" at
  echo https://github.com/nautymac/uncharted_pc ^(uncharted-windows.zip^), then unpack the 3D package over it.
  pause
  exit /b 1
)
if not exist "bin\ps2sr.dll" (
  echo bin\ps2sr.dll is missing: the picture will be side by side instead of woven for the panel.
)
rem One pass: both eyes from the same frame (set at start; the upscaler is switched off).
set BB_STEREO=geo
rem sr: woven for the SR panel (this file); sbs: side by side in the window (Uncharted-3D-SBS.bat).
set BB_STEREO_OUTPUT=sr
rem Eye separation (% of the width per eye at infinity) and the distance at screen depth (m).
rem In the game: Ctrl+F5/F6 depth, Ctrl+F7/F8 convergence, Ctrl+F9 swap eyes, Insert menu.
set BB_STEREO_DEPTH=1.0
set BB_STEREO_CONVERGENCE=3.0
rem First run: the launcher opens. Choose the game folder (Game page) and on the Display page
rem set Fullscreen, Mailbox, 60 FPS, 1920x1080, Anti-aliasing off, Frame generation off, then
rem press PLAY. Later runs start the game directly with the saved settings.
start "" "Uncharted.exe" --play
