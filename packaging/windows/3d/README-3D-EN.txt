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
   This 3D add-on is made for the original v0.3.9. If the version you got is newer than 0.3.9, get
   v0.3.9 instead: https://github.com/Supermedo/uncharted_pc/releases/tag/v0.3.9
2. On the same page, under Releases on the right, download uncharted-3d-windows.zip (this package).
3. Unpack both zips into the same folder, the 3D zip second, overwriting every file of the same
   name: Uncharted.exe and the _internal\ folder (the launcher with the 3D page), the bin\ folder
   (the 3D build bb-probe.exe with the DLLs it uses and the SR weaver bridge ps2sr.dll) and the two
   README files.
4. Run uncharted-windows\Uncharted.exe.
   - On the Game page choose the game folder.
   - On the 3D page pick your display in the 3D list:
       SR monitor : for SR monitors (Acer SpatialLabs, Leia SR panels). The SR runtime weaves the
                    picture for the panel with eye tracking.
       SBS        : for ordinary 3D TVs and 3D monitors that take a side-by-side picture (set the
                    display to its side-by-side 3D mode). Also for weavers that read the window
                    (SRCapture3D, ReShade Game Bridge).
       Off        : no 3D.
   - Choosing 3D sets the Display page to the settings 3D was tested with (Fullscreen, Mailbox,
     60 FPS, 1920x1080, Anti-aliasing off, Frame generation off). Keep them and press PLAY.

Requirements
------------
- NVIDIA or Intel GPU (Vulkan multiview, 144-byte push constants). On AMD the picture stays flat.
- SR monitor needs the Acer SpatialLabs / Leia SR runtime. Without it the picture is side by side.
- SBS: set the display to its side-by-side 3D input mode.

Keys in the game
----------------
Ctrl+F4       automatic convergence on / off (also a checkbox on the launcher's 3D page): the
              nearest thing in the picture is kept at screen depth, so nothing comes out in front
              of the screen, and the convergence follows the scene as the camera moves
Ctrl+F5 / F6  depth (eye separation) - / +
Ctrl+F7 / F8  convergence - / + (the distance that sits at screen depth, 3 m by default); with
              the automatic convergence, its bias: above 1 the nearest thing comes out a little,
              below 1 everything sits behind the screen
Ctrl+F9       swap the eyes
Insert        settings menu, "3D (stereo)"
Values are saved at once and come back to the launcher's 3D page when the game closes. Whether 3D
is on is chosen in the launcher before the start; switching it on with Ctrl+F3 during the game
gives a flat picture.

Updates
-------
This launcher looks for updates among the Releases of the 3D source repository above. Installing
an update it offers keeps 3D. Unpacking a newer original PC port over this folder removes 3D; if
that happened, unpack the 3D zip again.

Notes
-----
- DLSS, FSR, XeSS, TAA and frame generation glitch between the eyes. The 3D build switches the
  upscaler off at start. Keep the Display settings 3D was tested with; the launcher warns when
  they differ.
- When you get hit, the game itself turns the picture gray. That is not a 3D fault.
- Measured (2026-10-25, RTX 4080, U1 "Ambushed"): 60 FPS at 60 Hz, about 10 px of parallax per half
  for the far background, HUD at screen depth. U2 and U3 are not yet checked.
- Problems with the PC port itself (install, saves, controls): see the original author's
  repository. 3D problems: the Issues of the 3D source repository above.
