Uncharted: The Nathan Drake Collection — 입체 3D 추가 패키지 / Stereo 3D add-on
==========================================================================

이 패키지는 PC 포트가 아닙니다. 원작자의 PC 포트 위에 입체 3D(한 패스 양눈, 멀티뷰)를 더하는
파일만 들어 있습니다. 게임 파일도 들어 있지 않습니다(본인 소유 게임의 복호화 덤프가 필요하며,
그 준비는 원작자 안내를 따릅니다).
This is not the PC port. It only adds stereo 3D (both eyes drawn in one pass, Vulkan multiview)
to the original author's PC port. No game files are included (you need your own decrypted dump;
follow the original author's instructions for it).

원작 PC 포트 / Original PC port: https://github.com/Supermedo/uncharted_pc/releases
입체 3D 소스 / 3D source (branch stereo-3d): https://github.com/nautymac/uncharted_pc

설치 / Install
--------------
1. 원작 PC 포트의 uncharted-windows.zip을 받아 한 폴더에 풉니다. (Unpack the original
   uncharted-windows.zip into a folder.)
2. 이 패키지의 zip을 같은 폴더에 풉니다. 같은 이름의 파일은 덮어씁니다:
   bin\ 폴더(3D 빌드 bb-probe.exe와 그것이 쓰는 DLL, SR 패널 위빙 ps2sr.dll), Uncharted-3D.bat,
   Uncharted-3D-SBS.bat, 이 파일. 원작의 bin\ DLL과 섞이지만 3D 빌드는 자기 DLL을 씁니다.
   (Unpack this zip over it, overwriting bin\: the 3D build needs its own DLLs.)
3. Uncharted-3D.bat을 실행합니다. 처음에는 런처가 열립니다: Game 페이지에서 게임 폴더를 고르고,
   Display 페이지를 전체 화면 / Mailbox / 60 FPS / 1920x1080 / 안티앨리어싱 끔 / 프레임 생성 끔으로
   두고 PLAY. 다음부터는 바로 게임이 뜹니다. (Run Uncharted-3D.bat. The first time the launcher
   opens: choose the game folder, set Display to Fullscreen, Mailbox, 60 FPS, 1920x1080,
   Anti-aliasing off, Frame generation off, press PLAY. Later runs start the game directly.)

필요한 것 / Requirements
-----------------------
- NVIDIA 또는 Intel GPU (Vulkan multiview, 푸시 상수 144바이트). AMD에서는 3D가 평면으로 나옵니다.
  (NVIDIA or Intel GPU; on AMD the picture stays flat.)
- SR 패널 위빙: Acer SpatialLabs / Leia SR 런타임이 설치되어 있어야 합니다. 없으면 좌우(SBS)로
  나옵니다. (SR weaving needs the SR runtime; without it the picture is side by side.)
- 3D 디스플레이나 다른 위버(SRCapture3D, ReShade Game Bridge): Uncharted-3D-SBS.bat.

게임 안의 키 / Keys in the game
-----------------------------
Ctrl+F5 / F6  깊이(눈 간격) - / +      depth (eye separation)
Ctrl+F7 / F8  수렴 거리 - / +           convergence (the distance at screen depth, 3 m)
Ctrl+F9       좌우 눈 바꾸기             swap the eyes
Insert        설정 메뉴 "3D (stereo)"    settings menu
값은 bbport.ini에 바로 저장됩니다. 3D 모드 자체(geo)는 시작할 때 정해지며, 게임 중에 Ctrl+F3으로
바꾸면 평면이 됩니다. (The mode is fixed at start; Ctrl+F3 in the game gives a flat picture.)

주의 / Notes
-----------
- DLSS, FSR, XeSS, TAA와 프레임 생성은 두 눈 사이에서 깨집니다. 3D 빌드는 시작 때 업스케일러를
  끕니다. (Temporal anti-aliasing and frame generation glitch between the eyes; the 3D build
  switches the upscaler off at start.)
- 피격되면 게임이 화면을 회색조로 바꿉니다. 3D 오류가 아닙니다.
- 실측(2026-10-25, RTX 4080, U1 Ambushed): 60 Hz에서 60 FPS, 먼 배경 시차 약 10 px(반폭 기준),
  HUD는 화면면. U2·U3는 아직 확인하지 않았습니다. (U2 and U3 not yet checked.)
- PC 포트 자체의 문제(설치, 세이브, 조작)는 원작자 쪽을 참고하세요. 3D 관련 문제는 위 3D 소스
  저장소의 Issues에 적어 주세요.
