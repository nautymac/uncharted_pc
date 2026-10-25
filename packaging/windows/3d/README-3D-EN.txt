Uncharted: The Nathan Drake Collection — stereo 3D add-on
==========================================================
(한국어: README-3D-KO.txt)

This is not the PC port. It only adds stereo 3D (both eyes drawn in one pass, Vulkan multiview) to
the original author's PC port. No game files are included: you need your own decrypted dump of the
game you own, prepared as the original author's instructions describe.

Original PC port: https://github.com/Supermedo/uncharted_pc/releases
3D source (branch stereo-3d): https://github.com/nautymac/uncharted_pc

Install
-------
Both zips contain an uncharted-windows\ folder, so unpacking them to the same place merges them into
that one folder.

1. Open https://github.com/nautymac/uncharted_pc and click "Download the latest version" at the top.
   That is the original PC port, uncharted-windows.zip.
2. On the same page, under Releases on the right, download uncharted-3d-windows.zip (this package).
3. Unpack both zips into the same folder, the 3D zip second, overwriting files of the same name:
   the bin\ folder (the 3D build bb-probe.exe with the DLLs it uses and the SR weaver bridge
   ps2sr.dll), Uncharted-3D-SR.bat, Uncharted-3D-SBS.bat and the two README files. The original's
   bin\ DLLs stay mixed in, but the 3D build uses its own.
4. In uncharted-windows\ run the batch file for your display.
   - Uncharted-3D-SR.bat  : for SR monitors (Acer SpatialLabs, Leia SR panels). The SR runtime
     weaves the picture for the panel with eye tracking.
   - Uncharted-3D-SBS.bat : for ordinary 3D TVs and 3D monitors that take a side-by-side picture
     (set the display to its side-by-side 3D mode). Also for weavers that read the window
     (SRCapture3D, ReShade Game Bridge).
   The first time the launcher opens. On the Game page choose the game folder, on the Display page
   set Fullscreen, Mailbox, 60 FPS, 1920x1080, Anti-aliasing off, Frame generation off, then press
   PLAY. Later runs start the game directly.

Requirements
------------
- NVIDIA or Intel GPU (Vulkan multiview, 144-byte push constants). On AMD the picture stays flat.
- Uncharted-3D-SR.bat needs the Acer SpatialLabs / Leia SR runtime. Without it the picture is side
  by side.
- Uncharted-3D-SBS.bat: set the display to its side-by-side 3D input mode.

Keys in the game
----------------
Ctrl+F5 / F6  depth (eye separation) - / +
Ctrl+F7 / F8  convergence - / + (the distance that sits at screen depth, 3 m by default)
Ctrl+F9       swap the eyes
Insert        settings menu, "3D (stereo)"
Values are saved to bbport.ini at once. The 3D mode itself (geo) is fixed when the game starts;
switching it with Ctrl+F3 during the game gives a flat picture.

Notes
-----
- DLSS, FSR, XeSS, TAA and frame generation glitch between the eyes. The 3D build switches the
  upscaler off at start. Keep the Display settings above.
- When you get hit, the game itself turns the picture gray. That is not a 3D fault.
- Measured (2026-10-25, RTX 4080, U1 "Ambushed"): 60 FPS at 60 Hz, about 10 px of parallax per half
  for the far background, HUD at screen depth. U2 and U3 are not yet checked.
- Problems with the PC port itself (install, saves, controls): see the original author's
  repository. 3D problems: the Issues of the 3D source repository above.
