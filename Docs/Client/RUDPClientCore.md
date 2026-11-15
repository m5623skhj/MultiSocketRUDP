# RUDPClientCore

> C++ 클라이언트 코어다. TLS 세션 발급, UDP 연결, 신뢰성·비신뢰성 송수신, 재전송과 생존 검사를 담당한다.
> 구현은 `MultiSocketRUDP/MultiSocketRUDPClient/RUDPClientCore.*`, `SessionGetter.cpp`, `ClientOptionFile.h`에 있다.

## 목차

1. [시작과 공개 API](#시작과-공개-api)
2. [브로커 연결과 TLS](#브로커-연결과-tls)
3. [송수신 계약](#송수신-계약)
4. [ACK와 재전송](#ack와-재전송)
5. [종료와 재시작](#종료와-재시작)
6. [스레드와 공유 상태](#스레드와-공유-상태)
7. [옵션](#옵션)
8. [패킷 크기와 검증](#패킷-크기와-검증)

## 시작과 공개 API

`Start()`와 `Stop()`은 `protected`다. 상속한 클라이언트에서 사용하거나 공개 래퍼를 제공한다.

```cpp
virtual bool Start(const std::wstring& clientCoreOptionFile,
    const std::wstring& sessionGetterOptionFilePath, bool printLogToConsole);
bool Start(const std::wstring& clientCoreOptionFile,
    const std::wstring& sessionGetterOptionFilePath,
    bool printLogToConsole, const std::stop_token& inStopToken);
virtual void Stop();
```

| 파라미터 | 설명 |
|---|---|
| `clientCoreOptionFile` | `:CORE { ... }` 형식의 코어 옵션 파일 |
| `sessionGetterOptionFilePath` | 브로커 주소·포트·헤더 코드 옵션 파일 |
| `printLogToConsole` | 파일 로그와 함께 콘솔 출력 요청 |
| `inStopToken` | 브로커 연결 준비를 취소하는 토큰. 실행 중인 UDP 연결의 종료는 별도 API로 요청한다. |

```text
startStopLock 획득 → 중지 상태·취소 확인 → 이전 정리 스레드 join
  → 연결·시퀀스·윈도우 초기화 → Logger 시작
  → 옵션 검증 → 프로세스 Winsock 참조 확보
  → TLS 브로커에서 세션 발급 → UDP 소켓 생성
  → 수신·송신·재전송 스레드 및 종료 담당 스레드 생성
  → CONNECT 송신 → true 반환
```

`true`는 초기화와 연결 요청 송신 경로가 준비됐다는 뜻이다. CONNECT ACK까지 기다리는 API가 아니므로 실제 연결은 `IsConnected()`로 확인한다. 초기화 실패는 `false`를 반환하며 시작 소유자가 공통 종료 경로로 부분 자원을 정리한다. 일부 하위 연산의 예외까지 모두 `false`로 변환하는 계약은 아니다.

```cpp
bool IsStopped() const;
bool IsConnected() const;
unsigned int GetRemainPacketSize();
NetBuffer* GetReceivedPacket();
NetBuffer* GetReceivedUnreliablePacket();
void SendPacket(OUT IPacket& packet);
bool SendUnreliablePacket(IPacket& packet);
void Disconnect();
```

`GetRemainPacketSize()`는 신뢰성 보류 큐의 항목 수다. 시퀀스 누락으로 아직 꺼낼 수 없는 항목도 포함하며 비신뢰성 큐는 포함하지 않는다.

기본 연결·응답 훅과 테스트에서의 재정의는 [[Client/RUDPClientCoreHooks]]를 참고한다.

## 브로커 연결과 TLS

기본 빌드는 `USE_IOCP_SESSION_GETTER=0`이다. `SessionGetter.cpp`의 직접 TCP/TLS 경로를 사용한다. IOCP 분기의 콜백은 구현이 비어 있는 골격이므로 동일 기능을 제공하는 완성된 대체 경로로 취급하지 않는다.

1. `sessionBrokerIP`의 DNS 이름 또는 IPv4 문자열을 `TLSHelperClient::SetServerName()`에 전달하고 credential을 초기화한다.
2. IPv4 문자열이 아니면 비동기 `GetAddrInfoExW`로 IPv4 주소를 해석한다. DNS와 TCP 연결은 각각 6초 기준 제한과 시작 취소를 관찰한다. 취소한 DNS 작업은 운영체제가 결과 저장소 사용을 끝낼 때까지 기다린다.
3. 비블로킹 TCP 연결을 100ms 간격으로 확인한다. 현재 구현은 DNS 결과의 첫 IPv4 주소를 사용한다.
4. 브로커 소켓 송수신 제한을 각각 6초로 설정하고 TLS 1.2 핸드셰이크를 수행한다. 클라이언트 핸드셰이크 수신 대기에도 6초 기준 제한과 취소를 적용한다.
5. Windows 신뢰 체인, 유효 기간, 서버 인증 용도, 요청한 DNS/IP 이름 검증을 통과해야 한다.
6. 버전 `2`를 4바이트 big-endian 값으로 TLS 전송한다.
7. `TakePendingHandshakeData()`의 잔여 TLS 데이터를 먼저 복호화하고 이후 소켓 데이터를 누적한다. 여러 TLS 레코드에 나뉜 세션 헤더도 누적한다.
8. 전체 페이로드와 `close_notify`를 확인하면 수신 루프를 끝낸다. 현재 코드는 6초 기준 제한에 도달해도 전체 페이로드가 있으면 파싱을 진행하므로 `close_notify`가 항상 필수인 것은 아니다.
9. 응답 버전·결과 코드를 확인하고 UDP 목적지·포트·세션 ID·키·솔트를 설정한다.

```text
브로커 응답:
헤더 5B
  + 버전 4B (little-endian)
  + 결과 코드 1B
  + 서버 IP 문자열 (길이 2B + 문자열)
  + 서버 포트 2B + 세션 ID 2B
  + 키 16B + 솔트 16B
```

구체적인 인증 정책과 진단 한계는 [[TLSHelper]], 응답 형식은 [[Client/RUDPSessionBroker]]를 참고한다.

### 호스트 로컬 접속

`SESSION_BROKER.HOST_LOOPBACK=1`은 실제 TCP peer가 IPv4 127/8일 때만 허용한다. 인증·버전 검증은 유지하고 응답의 UDP 목적지 IP만 `127.0.0.1`로 바꾼다. 포트·세션 ID·키·솔트는 그대로다. 원격 브로커 peer면 시작에 실패한다. 생략 또는 0이면 브로커가 알린 IP를 사용하며 매 시작 시 기본값으로 초기화한다.

## 송수신 계약

### 신뢰성 송신

`SendPacket(OUT IPacket&)`은 콘텐츠를 내부 버퍼로 직렬화한다. 송신 전 연결 세대를 기록하고, 직렬화 후 `lifecycleLock`에서 연결·중지·세대를 재검증한 뒤 시퀀스를 발급하고 암호화한다. 중지 중이거나 크기가 초과하면 로그 또는 조기 반환으로 거절한다. 반환형이 `void`이므로 전달 성공 여부를 제공하지 않는다.

콘텐츠는 모두 `pendingPacketQueue`에 시퀀스 순서로 넣고 즉시 `TryFlushPendingQueue()`를 호출한다. 해당 함수는 pending 잠금을 유지한 채 ACK 대기 맵의 항목 수와 `remoteAdvertisedWindow`를 비교하고 허용량만 등록한다. 큐 삽입 직전에 마지막 ACK가 도착했어도 송신을 재시도한다. CONNECT 같은 코어 패킷은 이 흐름 제어 큐를 우회한다.

### 비신뢰성 송신

`SendUnreliablePacket()`은 독립 64비트 번호와 `CLIENT_TO_SERVER_UNREL` nonce 방향을 사용한다. ACK 맵·재전송·원격 광고 윈도우를 사용하지 않는다.

| 반환값 | 의미 |
|---|---|
| `true` | 로컬 송신 큐가 수용함. 가득 차면 가장 오래된 미송신 항목을 교체함 |
| `false` | 중지·미연결, 직렬화 실패, 크기 초과, 연결 세대 변경 또는 암호화 실패 |

`true`는 원격 전달 보장이 아니다. 송신 스레드는 두 큐에 데이터가 있으면 번갈아 선택한다.

### 수신과 버퍼 소유권

`OnRecvStream()`이 헤더 코드·길이·프레임 한도를 검사해 데이터그램의 각 프레임을 분리한다. `ProcessRecvPacket()`은 인증·타입 분기를 수행하고 필요한 큐가 버퍼 참조를 추가한다. 호출자는 자신의 원래 참조를 처리 후 해제한다.

신뢰성 수신은 최소 시퀀스 우선 큐에서 `nextRecvPacketSequence`와 일치하는 콘텐츠만 반환한다. `DrainReceivedControlPackets()`는 중복과 순서가 맞는 하트비트를 내부 해제하며 콘텐츠와 누락 번호를 건너뛰지 않는다. 비신뢰성 수신은 최초 인증 번호를 수용한 뒤 더 큰 번호만 받으며 별도 큐에서 반환한다.

두 수신 API 모두 읽기 위치가 `PacketId` 앞이고 인증 태그는 읽기 범위에서 제외된다. 반환 버퍼는 호출자가 `NetBuffer::Free()`해야 한다. 큐가 비거나 신뢰성 시퀀스가 누락됐으면 `nullptr`이다. 비신뢰성 수신 큐가 가득 차면 가장 오래된 미소비 항목을 해제한다.

## ACK와 재전송

`OnSendReply()`는 발급한 번호보다 큰 ACK를 무시한다. 추적 맵에 항목이 있을 때만 `SendPacketInfo::completion`을 `Pending → Acknowledged`로 CAS하고 맵 참조를 제거한다. CAS 성공 후 원격 윈도우·ACK 번호를 반영한다. 최초 시퀀스 0 ACK는 `lifecycleLock`에서 연결을 확정하고 생존 검사를 시작한다. 중복 ACK나 종료 중 ACK는 연결을 다시 시작하지 않는다.

클라이언트 재전송 스레드는 맵을 잠가 만료 후보의 참조를 확보한 뒤 잠금을 해제하고 처리한다. 한도 초과는 `Pending → RetryLimit` CAS가 성공한 경우에만 `RequestStop()`을 호출한다. ACK가 먼저 확정되면 실패 종료를 건너뛴다. 종료 요청은 맵 잠금 밖에서 수행한다.

| 구분 | 서버 | C++ 클라이언트 |
|---|---|---|
| 만료 관리 | 스레드별 힙·타이머 | 맵 순회·주기 대기 |
| 시간 설정 | 세션 RTT 기반 동적 RTO | `RETRANSMISSION_MS`를 대기·재전송 기준으로 사용 |
| 한도 초과 | 세션 `BY_RETRANSMISSION` 해제 요청 | 코어 `RequestStop()` |
| 수명 | 맵·큐·힙 참조 계수 | 맵·후보 객체 참조 및 버퍼 참조 계수 |

## 종료와 재시작

```text
작업 스레드·감시기 → RequestStop()
  → lifecycleLock에서 송신 세대 증가·연결 해제·ACK 수락 차단
  → 브로커/UDP 소켓 닫기·송신 종료 이벤트 설정·정리 스레드 통지

shutdownThread → CleanupStoppedSession()
  → 재전송·송신·수신 스레드 join
  → 생존 검사기 중단·join → Logger 사용 참조 반환
  → 송수신·pending·추적 큐 정리
  → 이벤트·Winsock 참조·키 핸들·키 버퍼 해제
  → 시퀀스·윈도우 초기화 → isStopped=true

외부 Stop() → startStopLock → RequestStop() → shutdownThread join
```

UDP 소켓 송수신 제한은 200ms다. 종료를 소켓 닫기의 특정 오류 코드 하나에 의존해 설명하지 않는다. `Stop()`은 정리 완료를 기다리며, 작업 스레드는 자신을 join하지 않도록 `RequestStop()`만 호출한다. `Disconnect()`도 연결 해제 패킷 송신 경로와 종료 요청을 수행하므로 객체 파괴·재사용 전에 외부 `Stop()`으로 완료를 기다린다.

`Start()`·`Stop()`은 `startStopLock`으로 직렬화된다. 시작 중 취소에는 토큰을 사용한다. `Stop()` 호출만으로 다른 스레드가 이미 소유한 시작 잠금을 즉시 취소하지 않는다. 직렬화 중이던 이전 송신은 세대 재검증으로 재시작한 연결에 등록되지 않는다.

## 스레드와 공유 상태

| 상태 | 보호 수단 |
|---|---|
| 외부 시작·종료와 정리 스레드 소유권 | `startStopLock` |
| 송신 수락·세대·종료 요청·CONNECT ACK | `lifecycleLock` |
| 실제 송신 큐와 비신뢰성 송신 큐 | `sendBufferQueueLock` |
| ACK 대기 맵 | `sendPacketInfoMapLock` |
| 신뢰성 pending 큐와 등록 순서 | `pendingPacketQueueLock` |
| 두 수신 큐·기대 번호·최신 번호 | `recvPacketHoldingQueueLock` |
| 프로세스 Winsock 참조 | `clientCountInThisProcessLock` |
| 연결·중지·인증 수신 횟수·UDP 핸들 | atomic |

송신 스레드는 세마포어와 종료 이벤트를 기다리고 깨어나 큐를 비운다. 큐가 작업의 기준이며 세마포어 신호 개수와 실제 큐 길이를 항상 같다고 가정하지 않는다. 콜백·입력 패킷 객체의 수명과 외부 동시 변경은 호출자가 별도로 관리한다.

## 옵션

`ClientOptionFile`은 기존 섹션 형식을 읽고 파일 길이 2,047자, 주석·따옴표·숫자 범위를 검사한다. INI의 `[SECTION]` 문법이 아니다.

```text
:CORE
{
    UNRELIABLE_QUEUE_CAPACITY = 64
    MAX_PACKET_RETRANSMISSION_COUNT = 16
    RETRANSMISSION_MS = 50
    SERVER_ALIVE_CHECK_MS = 15000
}
```

```text
:SESSION_BROKER
{
    IP = "127.0.0.1"
    PORT = 11011
}
:SERIALIZEBUF
{
    PACKET_CODE = 119
    PACKET_KEY = 50
}
```

`IP`는 최대 253자의 ASCII 영문자·숫자·점·하이픈을 허용한다. IPv6 문자열은 지원하지 않는다. `PORT`는 1~65,535, 코드·키는 0~255다. 큐 용량은 생략 시 64, 허용 범위는 1~65,535이며 송수신 deque 각각에 적용한다. 옵션 로더가 음수·오버플로·읽기 실패를 거부한다.

## 패킷 크기와 검증

C++ 최종 프레임은 `MAX_WIRE_PACKET_SIZE=1024`바이트다. 헤더 5 + 타입 1 + 번호 8 + ID 4 + 태그 16을 제외한 본문은 최대 990바이트다. 문자열·컨테이너 길이 필드도 본문에 포함한다. 송신은 번호 발급 전에 검사하며 자동 분할하지 않는다. 저장 버퍼의 최대 16KiB와 전송 한도는 다르다.

수신 초과·잘린 프레임은 인증·ACK 전에 거절하고 크기 진단은 누적 1·2·4·8…건에서만 남긴다. 회귀 검증은 `IntegrationClientHarness/UnreliableClientChecks.cpp`, `IntegrationTest.cpp`의 재시작·CONNECT 재전송 종료 시나리오와 관련 CoreTest에서 확인한다.

> 현재 일반 패킷의 `packetId`는 평문이며 AAD에도 포함되지 않는다. 암호화·인증 범위의 보안 한계는 [[PacketCryptoHelper]]와 [[CryptoSystem]]을 함께 확인한다.

## 관련 문서

- [[ServerAliveChecker]] — 검사기 종료와 수명
- [[Client/RUDPClientCoreHooks]] — 기본 훅과 테스트 재정의
- [[TLSHelper]] — TLS 인증과 누적 스트림
- [[Client/RUDPSessionBroker]] — 브로커 응답
- [[Common/PacketFormat]] — 공통 프레임
- [[FlowController]] — 흐름 제어
- [[UnreliableChannel]] — 큐·순서 보장 범위
- [[Testing]] — 검증 절차
- [[Troubleshooting]] — 장애 조사
