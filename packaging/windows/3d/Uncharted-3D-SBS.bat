@echo off
rem Uncharted: The Nathan Drake Collection - stereo 3D for ORDINARY 3D TVs AND 3D MONITORS: both
rem eyes are drawn in one pass (multiview) and shown side by side in the window (left eye left,
rem right eye right, each half width). Put the display into its side-by-side 3D mode. Also for
rem weavers that read the window (SRCapture3D --mode sbs, ReShade Game Bridge, ...).
rem For an SR monitor (Acer SpatialLabs, Leia SR) use Uncharted-3D-SR.bat instead.
rem Same requirements as Uncharted-3D-SR.bat (the original PC port unpacked here, bin\bb-probe.exe).
setlocal
cd /d "%~dp0"
if not exist "Uncharted.exe" (
  echo Uncharted.exe was not found next to this file.
  echo Unpack the original PC port here first: "Download the latest version" at
  echo https://github.com/nautymac/uncharted_pc ^(uncharted-windows.zip^), then unpack the 3D package over it.
  pause
  exit /b 1
)
set BB_STEREO=geo
set BB_STEREO_OUTPUT=sbs
set BB_STEREO_DEPTH=1.0
set BB_STEREO_CONVERGENCE=3.0
rem First run: the launcher opens. Choose the game folder (Game page) and on the Display page
rem set Fullscreen, Mailbox, 60 FPS, 1920x1080, Anti-aliasing off, Frame generation off, then
rem press PLAY. Later runs start the game directly with the saved settings.
start "" "Uncharted.exe" --play
