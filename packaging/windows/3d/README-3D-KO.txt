Uncharted: The Nathan Drake Collection — 입체 3D 추가 패키지
=============================================================
(English: README-3D-EN.txt)

이 패키지는 PC 포트가 아닙니다. 원작자의 PC 포트 위에 입체 3D(한 패스 양눈, Vulkan 멀티뷰)를 더하는
파일만 들어 있습니다. 게임 파일도 들어 있지 않습니다. 본인 소유 게임의 복호화 덤프가 필요하며, 그 준비는
원작자 안내를 따릅니다.

원작 PC 포트: https://github.com/Supermedo/uncharted_pc/releases
입체 3D 소스(브랜치 stereo-3d): https://github.com/nautymac/uncharted_pc

설치
----
두 zip 모두 안에 uncharted-windows\ 폴더가 들어 있어서, 같은 곳에 풀면 그 한 폴더로 합쳐집니다.

1. https://github.com/nautymac/uncharted_pc 를 열고 맨 위의 "Download the latest version"을 누릅니다.
   원작 PC 포트 uncharted-windows.zip이 받아집니다.
   이 3D 패키지는 원작 v0.3.9용입니다. 받은 버전이 0.3.9보다 새것이면 v0.3.9를 받으세요:
   https://github.com/Supermedo/uncharted_pc/releases/tag/v0.3.9
2. 같은 페이지 오른쪽의 Releases에서 uncharted-3d-windows.zip(이 패키지)을 받습니다.
3. 두 zip을 같은 폴더에 풉니다. 3D zip을 나중에 풀고, 같은 이름의 파일은 모두 덮어씁니다:
   Uncharted.exe와 _internal\ 폴더(3D 페이지가 있는 런처), bin\ 폴더(3D 빌드 bb-probe.exe와 그것이
   쓰는 DLL, SR 패널 위빙 ps2sr.dll), README 두 개.
4. uncharted-windows\Uncharted.exe를 실행합니다.
   - Game 페이지에서 게임 폴더를 고릅니다.
   - 3D 페이지의 3D 목록에서 디스플레이에 맞는 것을 고릅니다.
       SR monitor : SR 모니터용 (Acer SpatialLabs, Leia SR 패널). SR 런타임이 눈 추적으로 패널에 맞게
                    위빙합니다.
       SBS        : 일반 3D TV·3D 모니터용 (좌우 분할 Side-by-Side 입력; 디스플레이를 좌우 3D 모드로
                    둡니다). 창을 읽는 다른 위버(SRCapture3D, ReShade Game Bridge)에도 씁니다.
       Off        : 3D 끔.
   - 3D를 고르면 Display 페이지가 3D 실측 설정(전체 화면, Mailbox, 60 FPS, 1920x1080, 안티앨리어싱 끔,
     프레임 생성 끔)으로 바뀝니다. 그대로 두고 PLAY를 누릅니다.

필요한 것
---------
- NVIDIA 또는 Intel GPU (Vulkan multiview, 푸시 상수 144바이트). AMD에서는 3D가 평면으로 나옵니다.
- SR monitor: Acer SpatialLabs / Leia SR 런타임이 설치되어 있어야 합니다. 없으면 좌우(SBS)로 나옵니다.
- SBS: 디스플레이를 좌우(Side-by-Side) 3D 입력 모드로 둡니다.

게임 안의 키
-----------
Ctrl+F5 / F6  깊이(눈 간격) - / +
Ctrl+F7 / F8  수렴 거리 - / + (화면면에 놓이는 거리, 기본 3 m)
Ctrl+F9       좌우 눈 바꾸기
Insert        설정 메뉴 "3D (stereo)"
값은 바로 저장되고, 게임을 닫으면 런처 3D 페이지에도 돌아옵니다. 3D를 켜고 끄는 것은 시작 전에
런처에서 정합니다. 게임 중에 Ctrl+F3으로 켜면 평면이 됩니다.

업데이트
--------
이 런처는 업데이트를 위 3D 소스 저장소의 Releases에서 찾습니다. 런처가 알려 주는 업데이트를 설치하면
3D가 유지됩니다. 원작 PC 포트의 새 버전을 이 폴더에 덮어쓰면 3D가 지워집니다. 그랬다면 3D zip을 다시
풀어 주세요.

주의
----
- DLSS, FSR, XeSS, TAA와 프레임 생성은 두 눈 사이에서 깨집니다. 3D 빌드는 시작 때 업스케일러를 끕니다.
  Display 설정을 3D 실측값 그대로 두세요. 바꾸면 런처가 경고합니다.
- 피격되면 게임이 화면을 회색조로 바꿉니다. 3D 오류가 아닙니다.
- 실측(2026-10-25, RTX 4080, U1 Ambushed): 60 Hz에서 60 FPS, 먼 배경 시차 약 10 px(반폭 기준),
  HUD는 화면면. U2·U3는 아직 확인하지 않았습니다.
- PC 포트 자체의 문제(설치, 세이브, 조작)는 원작자 쪽을 참고하세요. 3D 관련 문제는 위 3D 소스 저장소의
  Issues에 적어 주세요.
