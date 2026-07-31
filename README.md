# MultiSocketRUDP

## 제작 기간 : 2024.10.20 ~ 진행중

1. 개요
2. 구성
3. Tools
4. 문서
5. 테스트
6. GitHub Actions 자동화
7. 측정

---

1. 개요

[이전에 개발하던 RUDP 프로젝트](https://github.com/m5623skhj/RUDPServer)에서 부족한 점을 보완하기 위해 생성한 프로젝트입니다.

이전에 개발중이던 RUDP에서는 하나의 소켓이 여러 클라이언트에 대한 수신을, SendWorkerThread가 소유한 소켓에 대해서 클라이언트에 대한 송신을 담당하였는데,

이 경우 Send를 할 때, 하나의 소켓만 사용하게 되어 스레드를 효율적으로 사용하지 못하게 되고, 때문에 현재와 같이 클라이언트와 소켓이 1:1 대응이 되도록 수정하였습니다.

아래는 약식으로 그린 Connect, Recv, Send의 흐름도 입니다.

<img width="1063" height="541" alt="image" src="https://github.com/user-attachments/assets/b1f2dc47-24a7-4221-bb8a-8d871cc74003" />

<img width="964" height="331" alt="image" src="https://github.com/user-attachments/assets/3985f92b-019a-4b63-982d-ccd704f6fe9a" />

<img width="814" height="422" alt="image" src="https://github.com/user-attachments/assets/9697a196-4806-466e-abf5-dd0443bdab4e" />

---

2. 구성

솔루션에 포함된 주요 프로젝트는 아래와 같습니다.

| 프로젝트 | 설명 |
| :--- | :--- |
| `MultiSocketRUDPServer` | RUDP 서버 코어 라이브러리입니다. 세션 관리, UDP/RIO 기반 송수신, 재전송, 하트비트 처리를 담당합니다. |
| `MultiSocketRUDPClient` | RUDP 클라이언트 코어 라이브러리입니다. 서버 연결, 패킷 송수신, 응답 대기 및 재전송 처리를 담당합니다. |
| `ContentsServer` | 서버 코어를 사용하는 예제 서버 프로젝트입니다. |
| `ContentsClient` | 클라이언트 코어를 사용하는 예제 클라이언트 프로젝트입니다. |
| `Logger` | 서버, 클라이언트, 테스트 프로젝트에서 사용하는 공통 로그 출력 기능을 제공합니다. |
| `CoreTest` | 서버/클라이언트 코어의 주요 로직의 계약을 검증하는 테스트 프로젝트입니다. |
| `IntegrationTest` | 실제 서버-클라이언트 흐름을 기준으로 연결, 요청/응답, 재전송 동작을 검증하는 통합 테스트 프로젝트입니다. |
| `IntegrationClientHarness` | 통합 테스트에서 클라이언트 동작을 별도 실행 대상으로 분리하기 위한 테스트 하네스입니다. |
| `MultiSocketRUDPBotTester` | WPF 기반 행동 그래프 편집기와 C# RUDP 부하 테스트 클라이언트입니다. |
| `MultiSocketRUDPBotTester.UnitTests` | BotTester 저장소, 암호화, 그래프, 실행 정책을 검증하는 xUnit 프로젝트입니다. |
| `ProtocolInteropTest` | C++/C# AES-GCM 패킷 형식이 공용 vector와 일치하는지 검증하는 실행형 테스트입니다. |

---

2.1 서버

* `MultiSocketRUDPCore`
  * UDP와 RIO를 사용하는 서버 코어입니다.
  * 스레드는 아래와 같이 구성됩니다.
    * `IOWorkerThread` : IO 처리를 담당합니다.
    * `RecvLogicThread` : 클라이언트에게 받은 패킷을 바탕으로 연결, 연결 해제, 패킷 핸들러 호출 등을 담당합니다.
    * `SessionBrokerThread` : 클라이언트가 어떤 소켓과 통신할 것인지 알 수 있도록 지원하는 스레드입니다.
    * `RetransmissionThread` : 패킷 유실 등으로 인한 타임 아웃이 발생했을 때, 해당 패킷을 재전송해주는 스레드입니다.
      * 일정 횟수 재전송을 해보고, 응답이 오지 않을 경우, 클라이언트가 끊겼다고 판단하고, 해당 세션을 ReleaseThread에서 정리할 수 있도록 전달합니다.
    * `ReleaseThread` : 세션 정리를 전담하는 스레드입니다.
      * 단일 스레드입니다.
    * `HeartbeatThread` : 각 세션의 통신 상태를 확인하기 위하여 일정 시간마다 하트비트 패킷을 보내는 스레드입니다.
      * 단일 스레드입니다.
  
* `RUDPSession`
  * Core에서 관리되고 있는 각 세션의 정보를 담고 있는 객체입니다.
  * 하나의 UDP 소켓을 가지고 있으며, 1개의 클라이언트를 연결하는 것 같이 표현합니다.
  * 서버가 초기화 될 때, 유저가 지정한 개수로 생성됩니다.
  * 컨텐츠에서는 이 클래스를 상속 받아서 유저를 구현합니다.
  
* `RUDPSessionBroker`
  * 최초로 클라이언트와 연결하여, 유저에게 실제로 연결될 세션을 알려주는 역할을 담당합니다.
  * 세션 브로커의 경우, 클라이언트와 TCP로 연결됩니다.
  * 유저가 접속하면, 아래의 행동을 진행합니다.
  	* 1. 클라이언트와 TLS로 통신을 하기 위하여 핸드 셰이크를 진행
  	* 2. 세션 브로커는 연결할 세션 정보와 세션 키, 세션 솔트를 클라이언트에게 발급
    * 3. 대상이 된 세션을 예약 처리한 후 연결을 종료
    * 4. 2에서 얻은 정보로 클라이언트가 패킷을 송신하면, 서버에서는 예약된 클라이언트인지를 확인하고 RUDPSession을 해당 클라이언트의 주소에 귀속

* `Ticker`와 `TimerEvent`
  * 일정 시간 마다 등록한 이벤트를 호출하는 객체입니다.
  * Ticker는 싱글턴 객체이며, Ticker에 TimerEvent를 등록하여 사용합니다.
  * Ticker는 스레드를 하나 생성하고, 이 스레드를 사용하여 TimerEvent의 시간을 확인하고 함수를 호출합니다.
  * TimerEvent를 사용할 경우, 반드시 TimerEventCreator::Create()를 통해 생성해야 하며, Ticker::RegisterTimerEvent()를 통해 Ticker에 등록해야 정상 동작합니다.

---

2.2 클라이언트

* `RUDPClientCore`
	* UDP를 이용한 클라이언트 코어입니다.
	* 스레드는 아래와 같이 구성됩니다.
		* `RecvThread` : 서버로 부터 수신 받은 패킷을 처리하는 단일 스레드입니다.
			* 수신 받은 패킷의 타입에 따라 분기하며, 일반 수신 패킷의 경우, 큐에 받아온 패킷들을 홀딩하고 있게 됩니다.
			* 클라이언트에서는 해당 큐에 쌓여있는 패킷들을 순서대로 꺼내어 처리해야 합니다.
		* `SendThread` : 서버로의 패킷 송신을 담당하는 단일 스레드입니다. 
		* `RetransmissionThread` : 재전송 제어를 담당하는 단일 스레드입니다.
			* 일정 횟수 이상 재전송 해보고, 서버로 부터의 응답이 여전히 없을 경우, 서버와의 연결이 끊어졌다고 판단합니다.
     			* 위의 상황 등으로 인하여 연결이 끊어졌다고 판단될 경우, 클라이언트의 모든 스레드가 정리되며, Stop 상태가 됩니다.

* `SessionGetter`
	* 클라이언트를 시작하면, 옵션 파일에 지정된 주소에 요청하여 연결할 주소 및 세션 정보를 얻어오는 클래스 입니다.
		* 위의 정보는 서버의 세션 브로커에게 얻어오며, 서버에서 얻지 못할 경우, 클라이언트가 정상적으로 가동하지 않습니다.

---

2.3 공통

* `Logger`
	* 로그 출력을 위한 오브젝트입니다.
		* 기본적으로 지정된 폴더에 로그 파일을 생성하며, 유저의 지정에 따라 콘솔에도 로그를 출력합니다.
		* 샘플로 들어 있는 서버나 클라이언트에 있는 LogExtension.h 파일을 참고하여 LogBase 클래스를 상속 받고 로그 클래스를 작성해주시면 됩니다.
			* 로그는 json 형식으로 출력됩니다.

* 패킷 구성
  * 위에서 부터 아래로 순차적으로 패킷 배열에 해당 내용이 들어가 있습니다.
    * `Packet header` 5byte
    * `Packet type` 1byte
    * `Packet sequence` 8byte
    * `Packet id` 4byte
    * `Packet body` nbyte
    * `Auth tag` 16byte

---

3. Tools
   1. PacketGenerator
      * PacketGenerate.bat 파일을 실행하면 PacketDefine.yml 파일을 참조하여 아래 파일들을 생성 혹은 수정합니다.
        * PlayerPacketHandler.cpp
        * Player.h의 패킷 핸들러 부문
        * PlayerPacketHandlerRegister.cpp
        * PlayerPacketHandlerRegister.h
        * PacketHandler.h
        * Protocol.cpp
        * Protocol.h
        * PacketIdType.h
      * 각 파일 생성 경로를 Tool/PacketGenerator/PacketItemsFilePath.py에 정의하면 해당 경로에 생성됩니다.
      * PacketDefine.yml 파일의 내용이 비어있을 경우, 파일의 삭제 및 생성을 시도하지 않습니다.
      * 해당하는 파일이 없을 경우 새로 생성합니다.
      * 각 파일들의 diff를 확인해 보고, 패킷 제네레이트의 결과물 파일이 이전 각 파일들의 원본과 비교하여 수정 사항이 없을 경우, 파일을 수정하지 않습니다.
        * 필요 없는 빌드 횟수를 줄이기 위하여 위와 같은 동작을 채택함
   2. RunDebug
      * 간단하게 디버그 모드의 Contents Client와 Contents Server(테스트 용 프로젝트)를 구동하기 위해 제공되는 배치 파일입니다.
   3. 개발용 인증서
      * 테스트용 인증서를 생성 및 삭제합니다.
      * CreateDevTLSCert.bat
        * 테스트용 TLS 인증서를 제작하는 배치파일입니다.
        * `DevServerCert` 라는 이름의 인증서를 제작합니다.
      * RemoveDevTLSCert.bat
        * `DevServerCert` 라는 이름을 가진 인증서를 제거합니다.
      * CreateDevTLSPfx.bat
        * 통합 테스트용 self-signed 인증서를 `IntegrationTest/TestCert.pfx`로 생성하고 인증서 저장소의 임시 원본을 제거합니다.
   4. 테스트 실행
      * `BuildCoreTestAndRunUnitTest.bat` : CoreTest를 Debug x64로 빌드하고 실행합니다.
      * `BuildIntegrationTestAndRun.bat` : 필요한 PFX를 생성한 뒤 IntegrationTest를 빌드하고 실행합니다.
   5. 패킷 정의 업로드
      * `PacketUploader.bat` : `PacketDefine.yml`을 설정된 Google Sheets worksheet에 업로드합니다.
      * `PacketGenerateAndUploader.bat` : 패킷 코드를 생성한 뒤 업로드를 연속 실행합니다.
      * 자세한 주의점은 [PacketUploader](./Docs/Tools/PacketUploader.md)를 참고합니다.
   6. 로그 압축
      * `LogCompress.bat` : 스크립트 위치 아래 `Log Folder`의 `.txt` 로그를 검증된 `.tar.gz`로 압축합니다.
      * 자세한 경로와 보존 정책은 [개발·테스트 보조 스크립트](./Docs/Tools/DevelopmentScripts.md)를 참고합니다.

---

4. [문서](https://github.com/m5623skhj/MultiSocketRUDP/tree/main/Docs)

---

5. 테스트

* `CoreTest`는 GoogleTest 기반 유닛 테스트입니다.
* `IntegrationTest`는 실제 서버/클라이언트, TLS, UDP 흐름을 사용하는 통합 테스트입니다.
* `MultiSocketRUDPBotTester.UnitTests`는 xUnit 기반 BotTester 유닛 테스트입니다.
* `ProtocolInteropTest`는 8개의 공용 vector로 C++/C# 패킷 암호화 호환성을 검증합니다.
* PR CI는 변경 경로에 따라 Native GTest와 BotTester xUnit/프로토콜 테스트를 선택적으로 실행하고, `build-and-test` 체크로 결과를 집계합니다.
* 자세한 실행 방법과 CI 주의점은 [Testing](./Docs/Testing.md)을 참고합니다.

---

6. GitHub Actions 자동화

GitHub Actions는 PR 병합을 검증하는 CI와 코드 리뷰, 문서 유지보수, 정적 분석을 지원하는 자동화로 구성됩니다.

| 구분 | Action | 실행 조건 | 역할 |
| :--- | :--- | :--- | :--- |
| PR CI | [PR CI](./.github/workflows/CI.yml) | PR 생성, 갱신, 재오픈 | 변경 경로를 분류하고 필요한 테스트를 호출한 뒤 `build-and-test` 필수 체크로 결과를 집계합니다. |
| PR CI | [Native GTest](./.github/workflows/GoogleTest.yml) | PR CI에서 C++ 관련 변경 시 호출 | C++ Debug x64 빌드, GoogleTest 유닛·통합 테스트, 실패 테스트 재시도와 커버리지 측정을 수행합니다. |
| PR CI | [BotTester Protocol Interop](./.github/workflows/BotTester.yml) | PR CI에서 BotTester 관련 변경 시 호출 | .NET 9 빌드, xUnit 테스트와 C++/C# 프로토콜 상호운용 테스트를 수행합니다. |
| 리뷰 보조 | [Gemini PR Comment Bot](./.github/workflows/GeminiPRCommoentBot.yml) | PR 생성, 갱신, 재오픈 | 코드 diff를 분석해 AI 리뷰 주석을 남깁니다. 병합 필수 체크로 사용하지 않습니다. |
| 문서 자동화 | [docs-bot](./.github/workflows/docs-bot.yml) | 매일, `docs-review` 라벨, 수동 실행 | 병합된 코드의 인터페이스 변경을 분석해 문서 최신화 PR을 제안합니다. |
| 품질 분석 | [Daily Static Analysis](./.github/workflows/StaticAnalysis.yml) | 매일 09:30 KST, 수동 실행 | C++ MSVC Native Analysis와 .NET Roslyn Analysis를 수행하고 분석 로그를 artifact로 보관합니다. |

PR CI의 변경 경로 분류, 테스트 과정과 필수 체크 구성은 [Testing](./Docs/Testing.md), 문서 자동화의 상세 동작은 [docs-bot](./Scripts/DocsBot/README.md)을 참고합니다.

---

7. 측정

[RTT 측정](https://github.com/m5623skhj/MultiSocketRUDP/issues/185#issuecomment-4670917398)

---
