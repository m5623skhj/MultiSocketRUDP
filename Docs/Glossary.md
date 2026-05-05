# 용어집 (Glossary)

> 프로젝트에서 사용하는 핵심 용어 정리.

---

## 네트워크 용어

| 용어 | 설명 |
|------|------|
| **RUDP** | Reliable UDP. UDP 위에 신뢰성(재전송, 순서 보장)을 구현한 커스텀 프로토콜 |
| **RIO** | Registered I/O. Windows의 고성능 소켓 I/O API. 커널-유저 복사 최소화 |
| **ARQ** | Automatic Repeat reQuest. 패킷 유실 시 자동 재전송 |
| **CWND** | Congestion Window. 한 번에 전송 가능한 미확인 패킷 수 제한 |
| **AAD** | Additional Authenticated Data. 암호화는 하지 않지만 인증 태그 계산에 포함 |
| **GCM** | Galois/Counter Mode. AES 인증 암호화 모드. 기밀성 + 무결성 동시 제공 |
| **Nonce** | Number Once. 암호화 시 한 번만 사용해야 하는 임의 값 |
| **SChannel** | Windows 내장 TLS/SSL 구현 (Secure Channel SSP) |
| **SessionSalt** | 세션별 무작위 값. Nonce 충돌 방지에 사용 |

---

## 프로젝트 용어

| 용어 | 설명 |
|------|------|
| **SessionBroker** | TCP+TLS로 RUDP 세션 정보를 클라이언트에 전달하는 서버 구성요소 |
| **SessionId** | `unsigned short`. 세션 풀 내 인덱스로도 사용 |
| **PacketSequence** | `unsigned long long`. 단조 증가하는 패킷 순번 |
| **PacketId** | `unsigned int`. 콘텐츠 패킷 종류 식별자 (`PACKET_ID` enum) |
| **CorePacket** | CONNECT, DISCONNECT, REPLY, HEARTBEAT 등 프레임워크 내부 패킷. PacketId 필드 없음 |
| **SendPacketInfo** | 재전송 추적 구조체. 버퍼 포인터, 소유 세션, 타임스탬프, 참조 카운트 포함 |
| **PendingQueue** | 흐름 제어로 인해 즉시 전송하지 못한 패킷 대기 큐 |
| **HoldingQueue** | 순서가 맞지 않아 보관 중인 수신 패킷 큐 |
| **TLS 인스턴스** | `thread_local`로 생성된 `CryptoHelper`. 스레드마다 독립적인 BCrypt 핸들 |
| **advertiseWindow** | 수신 측이 현재 받을 수 있는 패킷 수. SEND_REPLY에 포함 |
| **isCorePacket** | PacketId 필드 존재 여부. false이면 일반 데이터 패킷 |

---

## 상태 용어

| 용어 | 설명 |
|------|------|
| **DISCONNECTED** | 세션 풀에 반환, 재사용 대기 |
| **RESERVED** | 세션 발급 완료, 클라이언트 CONNECT 패킷 대기 |
| **CONNECTED** | CONNECT 패킷 수신 완료, 정상 통신 가능 |
| **RELEASING** | 해제 프로세스 진행 중, IO 완료 대기 |
| **IO_SENDING** | RIO Send 작업이 진행 중 (atomic enum) |
| **IO_NONE_SENDING** | Send 작업 없음 (atomic enum) |

---

## 관련 문서
- [[00_Overview]] — 전체 구조 개요
- [[PacketFormat]] — 패킷 구조 상세
- [[CryptoSystem]] — 암호화 용어 상세

---

## BotTester 용어

| 용어 | 설명 |
|------|------|
| **ActionGraph** | 트리거 이벤트가 발생할 때 실행되는 행동 노드 그래프 |
| **ActionNodeBase** | 모든 노드의 공통 베이스. `Name`, `Trigger`, `NextNodes`, `Execute()` |
| **ContextNodeBase** | `ActionNodeBase` 확장. `Execute()` 호출 시 `SetPacket()` 후 `ExecuteImpl()`로 위임 |
| **TriggerType** | 노드 실행 트리거 종류: `OnConnected` / `OnDisconnected` / `OnPacketReceived` / `Manual` |
| **NodeVisual** | 캔버스에 표시되는 노드의 WPF 비주얼 표현 (Border, 포트, 연결 정보 등) |
| **NodeConfiguration** | 노드별 설정값 컨테이너 (`PacketId`, `IntValue`, `StringValue`, `Properties`) |
| **RuntimeContext** | 노드 실행 중 데이터를 공유하는 `ConcurrentDictionary` 기반 컨텍스트 |
| **NodeBuilderRegistry** | `NodeVisual` → `ActionNodeBase` 변환 빌더 모음 |
| **AiTreeService** | Gemini 응답 JSON 파싱·검증 서비스 |
| **AiNodeFactory** | JSON → NodeVisual 트리 변환기 (캔버스 자동 배치 포함) |
| **BotTesterCore** | 봇 세션 생성·관리·종료를 담당하는 싱글톤 |
| **SessionGetter (C#)** | `SslStream`으로 세션 브로커에서 세션 정보를 수신하는 TLS 클라이언트 |
| **BackgroundBrush (카테고리)** | Action=DimGray, Condition=DarkOrange, Loop=DarkMagenta |
| **AssertionFailedException** | `AssertNode`가 `StopOnFailure=true`인 상태에서 조건 실패 시 발생 |
| **isAsyncNode** | 실행 후 NextNodes를 직접 관리하는 비동기 노드 여부 |
| **BotVariables** | `[BotVariable]` 어트리뷰트로 등록된 Getter/Setter 메서드 집합 |
| **__received_{PacketId}** | 패킷 수신 시 RuntimeContext에 자동 저장되는 예약 키 |
---

## 검토 메모

- 용어집은 함수 설명 문서가 아니므로 개별 함수 목록을 추가하지 않고, 현재 코드에서 실제로 쓰는 용어와 enum 이름을 맞추는 데 집중하는 편이 맞다.
