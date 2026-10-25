@echo off
rem Uncharted: The Nathan Drake Collection - stereo 3D in one pass (multiview), shown side by
rem side in the window (left eye left, right eye right, each half width): for 3D displays and
rem weavers that read the window (SRCapture3D --mode sbs, ReShade Game Bridge, ...).
rem Same requirements as Uncharted-3D.bat (the original PC port unpacked here, bin\bb-probe.exe).
setlocal
cd /d "%~dp0"
if not exist "Uncharted.exe" (
  echo Uncharted.exe was not found next to this file.
  echo Unpack the original PC port here first: https://github.com/Supermedo/uncharted_pc/releases
  echo ^(uncharted-windows.zip^), then unpack the 3D package over it.
  pause
  exit /b 1
)
set BB_STEREO=geo
set BB_STEREO_OUTPUT=sbs
set BB_STEREO_DEPTH=1.0
set BB_STEREO_CONVERGENCE=3.0
start "" "Uncharted.exe" --play
