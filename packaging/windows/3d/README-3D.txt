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
두 zip 모두 안에 uncharted-windows\ 폴더가 들어 있어서, 같은 곳에 풀면 그 한 폴더로 합쳐집니다.
(Both zips contain an uncharted-windows\ folder, so unpacking them to the same place merges them.)

1. https://github.com/nautymac/uncharted_pc 를 열고 맨 위의 "Download the latest version"을 누릅니다.
   원작 PC 포트 uncharted-windows.zip이 받아집니다. (Open https://github.com/nautymac/uncharted_pc
   and click "Download the latest version" at the top: that is the original PC port,
   uncharted-windows.zip.)
2. 같은 페이지 오른쪽의 Releases에서 uncharted-3d-windows.zip(이 패키지)을 받습니다.
   (On the same page, under Releases on the right, download uncharted-3d-windows.zip, this package.)
3. 두 zip을 같은 폴더에 풉니다. 3D zip을 나중에 풀고, 같은 이름의 파일은 덮어씁니다:
   bin\ 폴더(3D 빌드 bb-probe.exe와 그것이 쓰는 DLL, SR 패널 위빙 ps2sr.dll), Uncharted-3D-SR.bat,
   Uncharted-3D-SBS.bat, 이 파일. 원작의 bin\ DLL과 섞이지만 3D 빌드는 자기 DLL을 씁니다.
   (Unpack both zips into the same folder, the 3D zip second, overwriting: the 3D build needs
   its own DLLs.)
4. uncharted-windows\ 안에서 디스플레이에 맞는 bat을 실행합니다. (In uncharted-windows\ run the
   batch file for your display.)
   - Uncharted-3D-SR.bat  : SR 모니터용 (Acer SpatialLabs, Leia SR 패널). SR 런타임이 눈 추적으로
     패널에 맞게 위빙합니다. (For SR monitors: Acer SpatialLabs / Leia SR panels, woven by the SR
     runtime.)
   - Uncharted-3D-SBS.bat : 일반 3D TV·3D 모니터용 (좌우 분할 Side-by-Side 입력을 받는 디스플레이;
     디스플레이를 좌우 3D 모드로 둡니다). 창을 읽는 다른 위버(SRCapture3D, ReShade Game Bridge)에도.
     (For ordinary 3D TVs and 3D monitors that take a side-by-side picture, set the display to its
     side-by-side 3D mode; also for weavers that read the window.)
   처음에는 런처가 열립니다: Game 페이지에서 게임 폴더를 고르고, Display 페이지를 전체 화면 / Mailbox /
   60 FPS / 1920x1080 / 안티앨리어싱 끔 / 프레임 생성 끔으로 두고 PLAY. 다음부터는 바로 게임이 뜹니다.
   (The first time the launcher opens: choose the game folder, set Display to Fullscreen, Mailbox,
   60 FPS, 1920x1080, Anti-aliasing off, Frame generation off, press PLAY. Later runs start the
   game directly.)

필요한 것 / Requirements
-----------------------
- NVIDIA 또는 Intel GPU (Vulkan multiview, 푸시 상수 144바이트). AMD에서는 3D가 평면으로 나옵니다.
  (NVIDIA or Intel GPU; on AMD the picture stays flat.)
- Uncharted-3D-SR.bat: Acer SpatialLabs / Leia SR 런타임이 설치되어 있어야 합니다. 없으면 좌우(SBS)로
  나옵니다. (Needs the SR runtime; without it the picture is side by side.)
- Uncharted-3D-SBS.bat: 디스플레이를 좌우(Side-by-Side) 3D 입력 모드로 둡니다. (Put the display into
  its side-by-side 3D mode.)

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
