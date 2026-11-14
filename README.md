# wowvision-korean

WowVision 한국어판에만 들어가는 기능을 모아 두는 저장소다. 원본(wow-vision)에 보내지 않는 코드가 원본 복제 저장소의 파일 사이에 섞이지 않도록 따로 둔다.

wowvision-installer `ko` 브랜치의 `korean/` 경로에 서브모듈로 붙어 있다. 설치 프로그램은 `installer/*.cs`를 함께 컴파일한다(`*Test.cs`는 자체 검사 `WowVisionSelfTest`에만 들어간다).

## 구성

| 경로 | 내용 |
|------|------|
| `sense-reader-bridge/` | 센스 리더 프록시 `nvdaControllerClient.dll`(C, x64) 소스와 테스트 |
| `installer/KoreanSenseReader.cs` | 설치 프로그램 단계: 프록시를 SAPI2SR 폴더에 넣고 서명한다 |
| `installer/KoreanSenseReaderTest.cs` | 자체 검사 `kosensereader` |

## 센스 리더 연결

WowVision은 SAPI2SR 브리지 음성(엔진 `sapi2sr_engine.dll`)으로 화면 낭독기에 말한다. 엔진은 같은 폴더의 `nvdaControllerClient.dll`을 불러 말할 내용을 넘긴다. 원본 설치 프로그램이 넣는 그 DLL은 Sku의 말한 내용 기록 프록시이고, 그 프록시가 NV Access의 `nvdaControllerClient.nvaccess.dll`로 넘긴다.

한국어판 설치 프로그램은 브리지 설치 직후 그 DLL을 `nvdaControllerClient.next.dll`로 옮기고 이 저장소의 프록시를 그 자리에 넣는다.

    SAPI2SR 엔진 -> 센스 리더 프록시 -> next(Sku 기록 프록시) -> NV Access 클라이언트 -> NVDA
                                \-> (NVDA가 꺼져 있고 센스 리더가 켜져 있을 때) SenseReader.Application

- 모든 호출은 먼저 next로 간다. NVDA가 실행 중이면 지금과 같다.
- next가 오류를 돌려주고(NVDA가 꺼져 있음) 센스 리더가 실행 중이면, 센스 리더의 자동화 객체 `SenseReader.Application`(xvsrd.exe가 등록한 COM 서버)으로 보낸다. `speakText`·`speakSsml`은 `Speak`(대기열에 이어 말함), `cancelSpeech`는 `StopSpeaking`(대기열까지 비움)이다. 그래서 엔진의 `skuint` 끊기와 볼륨 0 건너뛰기가 센스 리더에서도 그대로 동작한다.
- 센스 리더 실행 여부는 `XVSRD` 창과 그 창의 주인 `xvsrd.exe`로 판단한다. 센스 리더가 꺼져 있을 때 COM 객체를 만들면 센스 리더가 새로 실행되므로, 확인 없이 만들지 않는다.
- 점자(`brailleMessage`)는 센스 리더 API가 없어 NVDA로만 간다. 센스 리더로 말한 내용은 Sku 기록 프록시를 거치지 않으므로 말한 내용 기록에 남지 않는다.
- WoW는 서명되지 않은 엔진 DLL을 불러오지 않는다. 브리지의 서명 스크립트(`sku-nvda-voice-sign.ps1`)는 고정 해시가 맞는 파일만 서명하므로, payload를 만들 때 프록시의 해시를 더한 사본(`sign.ps1`)을 함께 묶는다.
- Sku 설치 프로그램이나 원본 설치 프로그램을 나중에 실행하면 프록시가 Sku 프록시로 다시 덮인다. 그때는 한국어판 설치 프로그램을 다시 실행한다.

### 빌드와 테스트

Visual Studio 2022 Build Tools(C++)가 필요하다.

    sense-reader-bridge\build.bat test

`out\nvdaControllerClient.dll`을 만들고 오프라인 라우팅 테스트(`tests\test_bridge.c`, 가짜 NVDA DLL 사용)를 돌린다. 센스 리더를 띄운 상태에서는 `out\test_bridge.exe live-running`, `live-speak <문장>`, `live-stop`으로 실제 센스 리더 경로를 확인할 수 있다.

설치 프로그램 payload(`WowVisionInstaller\payload\koSenseReader.zip`)는 wowwork의 `uv run python -m tools.installer_loc payload`가 이 빌드와 테스트를 거쳐 만든다.
