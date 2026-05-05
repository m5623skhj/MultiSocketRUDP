# MultiSocketRUDPCore

> **서버의 최상위 오케스트레이터.**  
> WSAStartup부터 세션 풀 구성, RIO 초기화, 스레드 시작, 세션 브로커 실행까지  
> 서버 전체 생명주기를 단일 클래스에서 관리한다.  
> 콘텐츠 개발자는 이 클래스의 API를 통해 세션 조회, 연결 수 확인, TPS 측정 등을 수행한다.

---

## 목차

1. [서버 시작 — StartServer](#1-서버-시작--startserver)
2. [서버 종료 — StopServer](#2-서버-종료--stopserver)
3. [함수 설명](#3-함수-설명)
4. [콘텐츠 서버 API](#4-콘텐츠-서버-api)
5. [내부 동작 — 패킷 전송 경로](#5-내부-동작--패킷-전송-경로)
6. [내부 동작 — 세션 해제 경로](#6-내부-동작--세션-해제-경로)
7. [내부 동작 — InitReserveSession](#7-내부-동작--initreservesession)
8. [옵션 파일 설정값 전체](#8-옵션-파일-설정값-전체)
9. [멀티소켓 구조의 의미](#9-멀티소켓-구조의-의미)
10. [의존 컴포넌트](#10-의존-컴포넌트)

---

## 1. 서버 시작 — `StartServer`

```cpp
bool StartServer(
    const std::wstring& coreOptionFilePath,
    const std::wstring& sessionBrokerOptionFilePath,
    SessionFactoryFunc&& factoryFunc,
    bool printLogToConsole
);
```

### 파라미터 상세

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `coreOptionFilePath` | `const wstring&` | 서버 코어 설정 INI 파일 (스레드 수, 소켓 수, 재전송 등) |
| `sessionBrokerOptionFilePath` | `const wstring&` | 세션 브로커 설정 INI 파일 (포트, 서버 IP) |
| `factoryFunc` | `SessionFactoryFunc&&` | `RUDPSession*` 을 반환하는 팩토리 람다. **콘텐츠 개발자가 구현** |
| `printLogToConsole` | `bool` | `true`이면 Logger가 파일과 콘솔 동시 출력 |

**`SessionFactoryFunc` 타입:**
```cpp
using SessionFactoryFunc = std::function<RUDPSession*(MultiSocketRUDPCore&)>;
```

### 반환값

| 반환값 | 의미 |
|--------|------|
| `true` | 모든 초기화 완료, 클라이언트 수락 시작 |
| `false` | 옵션 파일 읽기 실패, WSAStartup 실패, RIO 초기화 실패, SessionBroker 시작 실패 등 |

> **false 반환 시 정리:** 부분 초기화된 자원이 있을 수 있으므로, `false` 반환 후에도  
> `StopServer()`를 호출해야 완전히 정리된다.

### 내부 실행 순서 (전체)

```
1. Logger::GetInstance().RunLoggerThread(printLogToConsole)

2. ReadOptionFile(coreOptionFilePath, sessionBrokerOptionFilePath)
   └─ g_Paser.GetValue_*() 로 INI 파싱 → 멤버 변수 설정

3. MultiSocketRUDPCoreFunctionDelegate::Instance().Init(*this)
   └─ 전역 함수 위임자 초기화 (RUDPSession → Core 호출 브릿지)

4. InitNetwork()
   ├─ WSAStartup(MAKEWORD(2,2))
   └─ RUDPSession::SetMaximumPacketHoldingQueueSize(maxHoldingPacketQueueSize)

5. sessionManager = make_unique<RUDPSessionManager>(numOfSockets, *this, sessionDelegate)
6. sessionManager->Initialize(numOfWorkerThread, move(factoryFunc))
   └─ for i in 0..numOfSockets:
        session = factoryFunc(*this)         ← 콘텐츠 팩토리 호출
        SetSessionId(session, i)
        SetThreadId(session, i % N)          ← RIO 완료 큐 분산
        sessionList.push_back(session)
        unusedSessionIdList.push_back(i)

7. InitRIO()
   ├─ rioManager = make_unique<RIOManager>(sessionDelegate)
   ├─ ioHandler = make_unique<RUDPIOHandler>(...)
   └─ rioManager->Initialize(numOfSockets, numOfWorkerThread)
        └─ LoadRIOFunctionTable() (WSAIoctl WSAID_MULTIPLE_RIO)
        └─ for i in 0..N: RIOCreateCompletionQueue(numOfSockets/N * MAX_SEND_BUFFER_SIZE)

8. RunAllThreads()
   ├─ recvLogicThreadEventStopHandle = CreateEvent(manual, FALSE)
   ├─ sessionReleaseStopEventHandle  = CreateEvent(manual, FALSE)
   ├─ sessionReleaseEventHandle      = CreateEvent(auto,   FALSE)
   │
   ├─ Ticker::GetInstance().Start(timerTickMs)
   │
   ├─ for id in 0..N:
   │    recvIOCompletedContexts.emplace_back()
   │    recvLogicThreadEventHandles.push_back(CreateSemaphore(0, LONG_MAX))
   │    sendPacketInfoList.emplace_back()
   │    sendPacketInfoListLock.push_back(make_unique<mutex>())
   │
   ├─ SESSION_RELEASE_THREAD × 1 시작
   ├─ HEARTBEAT_THREAD × 1 시작
   ├─ IO_WORKER_THREAD × N 시작
   ├─ RECV_LOGIC_WORKER_THREAD × N 시작
   ├─ RETRANSMISSION_THREAD × N 시작
   │
   ├─ Sleep(1000)    ← 모든 스레드 안정화 대기
   │
   └─ sessionBroker = make_unique<RUDPSessionBroker>(...)
      sessionBroker->Start(sessionBrokerPort, coreServerIp)
      └─ TCP 소켓 bind + listen + accept 스레드 시작
         TLS 워커 스레드 4개 시작
```

> **Sleep(1000)의 이유**: IO Worker와 RecvLogic Worker가 완전히 준비되기 전에  
> SessionBroker가 클라이언트를 수락하면, 세션에 DoRecv()가 등록되기 전에  
> 패킷이 도착해 유실될 수 있다. 1초 대기로 이를 방지한다.

### 사용 예시 (전체)

```cpp
#include "MultiSocketRUDPCore.h"
#include "Player.h"
#include "Protocol.h"
#include "PlayerPacketHandlerRegister.h"

int main() {
    // 인증서 저장소 이름, 인증서 Subject Name
    MultiSocketRUDPCore core(L"MY", L"DevServerCert");

    // PacketManager에 패킷 팩토리 등록 (StartServer 이전에 반드시 호출)
    ContentsPacketRegister::Init();

    bool ok = core.StartServer(
        L"ServerOptionFile/CoreOption.txt",
        L"ServerOptionFile/SessionBrokerOption.txt",
        [](MultiSocketRUDPCore& c) -> RUDPSession* {
            return new Player(c);
        },
        true   // 콘솔 출력
    );

    if (!ok) {
        std::cerr << "Server start failed\n";
        core.StopServer();
        return -1;
    }

    std::cout << "Server started. Press Enter to stop.\n";
    std::cin.get();

    core.StopServer();
    return 0;
}
```

---

## 2. 서버 종료 — `StopServer`

```cpp
void StopServer();
```

모든 자원을 순서대로 안전하게 해제한다.

```
1. sessionBroker->Stop()
   ├─ closesocket(sessionBrokerListenSocket)   ← accept() 에러 발생 → accept 루프 종료
   ├─ sessionBrokerThread.request_stop() + join()
   └─ 워커 스레드 4개 request_stop() + join() (condition_variable 신호)

2. CloseAllSessions()
   └─ for each session: sessionDelegate.CloseSocket(*session)
      → closesocket() 호출 → 진행 중인 RIO 작업에 에러 상태 유발
      → IO Worker가 에러 상태를 감지하고 정리

3. SetEvent(recvLogicThreadEventStopHandle)   ← Logic Worker 종료 신호
4. SetEvent(sessionReleaseStopEventHandle)    ← Release Thread 종료 신호

5. StopAllThreads()
   └─ 각 THREAD_GROUP별 stop_token 신호
   └─ jthread 소멸 → join() 자동 호출 (블로킹)

6. for each handle in recvLogicThreadEventHandles: CloseHandle
7. CloseHandle(recvLogicThreadEventStopHandle)
8. CloseHandle(sessionReleaseEventHandle)
9. CloseHandle(sessionReleaseStopEventHandle)

10. Ticker::GetInstance().Stop()
    └─ tickerThread.join()

11. ClearAllSession()
    ├─ unusedSessionIdList.clear()
    ├─ for each session: delete session   ← 메모리 해제
    └─ sessionList.clear()

12. Logger::GetInstance().StopLoggerThread()
    └─ SetEvent(stopHandle) → Worker 종료 → 잔여 로그 기록 후 join()

13. WSACleanup()
14. isServerStopped = true
```

**종료 순서가 중요한 이유:**

| 순서 위반 예시 | 발생 문제 |
|----------------|-----------|
| 스레드 종료 전 `ClearAllSession` | 스레드가 delete된 세션 포인터에 접근 → crash |
| Ticker 종료 전 Logger 종료 | Ticker의 TimerEvent가 Logger에 로그를 쓰려다 crash |
| `StopAllThreads` 전 소켓 닫기 건너뜀 | IO Worker가 영원히 RIO 완료 대기 |

---

## 3. 함수 설명

### 공개 함수

#### `MultiSocketRUDPCore(std::wstring&& inSessionBrokerCertStoreName, std::wstring&& inSessionBrokerCertSubjectName)`
- SessionBroker가 사용할 인증서 저장소 이름과 Subject Name을 보관한다.
- 인증서 정보가 없으면 브로커 초기화가 성립하지 않으므로 필수 인자가 있는 생성자로 취급한다.

#### `bool StartServer(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath, SessionFactoryFunc&& factoryFunc, bool printLogToConsole = false)`
- 서버 전체 초기화 진입점이다.
- 옵션 파일 로드, 네트워크/RIO 초기화, 세션 풀 생성, 워커 스레드 기동, SessionBroker 시작까지 담당한다.

#### `void StopServer()`
- 서버 전체 종료 진입점이다.
- 브로커 중단, 세션 소켓 정리, 워커 스레드 정지, 세션 메모리 반환, Logger 종료, `WSACleanup()`를 순서대로 수행한다.

#### `bool IsServerStopped() const`
- 서버가 완전히 종료되었는지 반환한다.

#### `unsigned short GetNowSessionCount() const`
- 현재 CONNECTED 상태 세션 수를 반환한다.

#### `unsigned int GetAllConnectedCount() const`
- 서버 시작 이후 누적 연결 성공 횟수를 반환한다.

#### `unsigned int GetAllDisconnectedCount() const`
- 서버 시작 이후 누적 연결 해제 횟수를 반환한다.

#### `unsigned int GetAllDisconnectedByRetransmissionCount() const`
- 재전송 한도 초과로 종료된 누적 세션 수를 반환한다.

#### `bool SendPacket(SendPacketInfo* sendPacketInfo) const`
- 세션이 조립한 `SendPacketInfo`를 IO 계층으로 전달해 실제 송신을 시작한다.

#### `void EraseSendPacketInfo(SendPacketInfo* eraseTarget, ThreadIdType threadId)`
- 스레드별 전송 추적 목록에서 특정 `SendPacketInfo`를 제거한다.
- ACK 수신 후 정리나 송신 실패 정리 흐름에서 호출된다.

#### `RIO_EXTENSION_FUNCTION_TABLE GetRIOFunctionTable() const`
- 초기화된 RIO 함수 테이블을 반환한다.
- 세션 RIO 초기화에서 사용된다.

#### `static WORD GetPayloadLength(const NetBuffer& buffer)`
- `NetBuffer` 헤더에서 payload 길이를 추출한다.
- 패킷 형식 검증이나 디버깅 시 유용한 정적 유틸이다.

#### `int32_t GetTPS() const`
- 현재 TPS 카운터 값을 반환한다.

#### `void ResetTPS() const`
- TPS 카운터를 0으로 초기화한다.

### 비공개 함수

#### `void DisconnectSession(SessionIdType disconnectTargetSessionId) const`
- 완전히 해제된 세션을 세션 풀로 반환한다.
- 사용자 코드가 직접 호출하는 API가 아니라 Session Release 흐름 내부 함수다.

#### `void PushToDisconnectTargetSession(RUDPSession& session)`
- RELEASE 대상 세션을 release queue에 넣고 Session Release Thread를 깨운다.

#### `RUDPSession* AcquireSession() const`
- 세션 매니저에서 재사용 가능한 세션을 하나 확보한다.
- SessionBroker가 새 연결을 발급할 때 간접 호출된다.

#### `RUDPSession* GetUsingSession(SessionIdType sessionId) const`
- RESERVED 또는 CONNECTED 상태 세션을 조회한다.
- 반환 직후 상태가 바뀔 수 있으므로 즉시 검증 후 사용해야 한다.

#### `RUDPSession* GetReleasingSession(SessionIdType sessionId) const`
- RELEASING 상태 세션을 조회한다.
- Session Release Thread 내부 로직에서 사용된다.

#### `CONNECT_RESULT_CODE InitReserveSession(RUDPSession& session) const`
- 세션 소켓 생성, 세션 RIO 초기화, 첫 `DoRecv()` 등록, RESERVED 상태 전이를 수행한다.
- SessionBroker가 새 세션을 발급할 때 호출된다.

---

## 4. 콘텐츠 서버 API

### 서버 상태 조회

```cpp
// 서버 종료 완료 여부
bool IsServerStopped() const;
// → isServerStopped atomic 값 반환

// 현재 연결된 세션 수 (CONNECTED 상태)
unsigned short GetNowSessionCount() const;
// → sessionManager->GetConnectedCount() → connectedUserCount.load()

// 서버 시작 이후 누적 연결 수
unsigned int GetAllConnectedCount() const;

// 서버 시작 이후 누적 연결 해제 수
unsigned int GetAllDisconnectedCount() const;

// 재전송 한도 초과로 종료된 누적 세션 수
unsigned int GetAllDisconnectedByRetransmissionCount() const;
```

### TPS (초당 처리 패킷 수) 모니터링

```cpp
int32_t GetTPS() const;
// → packetProcessor->GetTPS()
// → SEND_TYPE 패킷 성공 처리 시마다 atomic++ 됨

void ResetTPS() const;
// → tps.store(0, relaxed)
// → 주기적으로 읽고 초기화하는 패턴으로 사용
```

**사용 예시 (모니터링 스레드):**

```cpp
// 서버 모니터링 예시
std::thread monitorThread([&]() {
    while (!core.IsServerStopped()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        int32_t tps = core.GetTPS();
        core.ResetTPS();
        int sessions = core.GetNowSessionCount();
        LOG_DEBUG(std::format("TPS: {}, Sessions: {}", tps, sessions));
    }
});
```

### 세션 접근

```cpp
// sessionId로 CONNECTED 또는 RESERVED 세션 접근
RUDPSession* GetUsingSession(SessionIdType sessionId) const;

// sessionId로 RELEASING 세션 접근 (세션 해제 스레드 내부에서 사용)
RUDPSession* GetReleasingSession(SessionIdType sessionId) const;
```

**GetUsingSession 사용 패턴:**

```cpp
// 다른 세션에게 패킷 전송
void GameRoom::NotifyPlayerLeft(SessionIdType leaveId) {
    auto* leavePlayer = static_cast<Player*>(
        core.GetUsingSession(leaveId));

    for (auto id : memberIds) {
        auto* member = static_cast<Player*>(core.GetUsingSession(id));
        if (member && member->IsConnected()) {
            PlayerLeftPacket pkt;
            pkt.sessionId = leaveId;
            member->SendPacket(pkt);
        }
    }
}
```

> **주의:** `GetUsingSession()`이 nullptr을 반환하지 않더라도, 그 직후 세션이  
> `DoDisconnect()`를 호출할 수 있다. `IsConnected()`로 상태를 재확인하거나,  
> `SendPacket()`의 반환값으로 처리 성공 여부를 확인해야 한다.

### 특정 세션 강제 종료 (내부 API — `private`)

```cpp
// private: 외부에서 직접 호출 불가
// session->DoDisconnect(reason) 을 호출하면 Session Release Thread가 자동 처리
void DisconnectSession(SessionIdType disconnectTargetSessionId) const;
```

```cpp
// 내부 구현
void MultiSocketRUDPCore::DisconnectSession(SessionIdType id) const {
    if (!sessionManager->ReleaseSession(id)) return;
    // → 세션이 RELEASING 상태인지 확인
    // → unusedSessionIdList에 반환

    LOG_INFO(std::format("Session {} disconnected", id));
}
```

> **외부에서 직접 호출할 수 없다.** `DisconnectSession`은 `private` 멤버이며,  
> `session->DoDisconnect(reason)`을 호출하면 Session Release Thread가  
> 내부적으로 이 함수를 자동 호출한다.

### RIO 함수 테이블 접근

```cpp
RIO_EXTENSION_FUNCTION_TABLE GetRIOFunctionTable() const;
// → rioManager->GetRIOFunctionTable()
// → 세션 내 RIO 컨텍스트 초기화 시 사용
```

---

## 5. 내부 동작 — 패킷 전송 경로

`session->SendPacket(packet)` 호출 이후의 전체 경로:

```
[콘텐츠 레이어] RUDPSession::SendPacket(IPacket&)
    ↓ 시퀀스 증가, 직렬화
[흐름 제어] CanSend() → PendingQueue 또는 →
    ↓
[내부] RUDPSession::SendPacketImmediate()
    ↓ AES-GCM EncodePacket
[코어] MultiSocketRUDPCore::SendPacket(SendPacketInfo*)
    ↓ refCount++, SendContext 큐에 삽입
[IO 핸들러] RUDPIOHandler::DoSend(session, threadId)
    ↓ IO_SENDING CAS 획득
[스트림] RUDPIOHandler::MakeSendStream(session, threadId)
    ↓ 복수 패킷을 32KB 버퍼에 배치
[RIO] rioManager.RIOSendEx(rioRQ, context, ...)
    ↓ 완료 큐에 등록
[IO Worker] RIODequeueCompletion → IOCompleted → SendIOCompleted
    ↓ IO_NONE_SENDING 복원
[재시도] DoSend() 재호출 → 큐에 남은 패킷 처리
```

---

## 6. 내부 동작 — 세션 해제 경로

```
[어디서든] session->DoDisconnect()
    ↓ TryTransitionToReleasing() CAS
    ↓ OnDisconnected() 콘텐츠 훅
    ↓ PushToDisconnectTargetSession(session)
         ↓ nowInReleaseThread = true
         ↓ releaseSessionIdList.push_back(sessionId)
         ↓ SetEvent(sessionReleaseEventHandle)

[Session Release Thread] WaitForMultipleObjects
    ↓ GetReleasingSession(id) → 세션 획득
    ↓
    ├─ IO_SENDING 중? → remainList 보관, SetEvent 재시도
    ├─ nowInProcessingRecvPacket? → remainList 보관, SetEvent 재시도
    └─ 안전 확인 완료 → session->Disconnect()
            ↓ CloseSocket() (unique_lock)
            ↓ ForEachAndClearSendPacketInfoMap → EraseSendPacketInfo
            ↓ OnReleased() 콘텐츠 훅
            ↓ InitializeSession() 상태 초기화
            ↓ stateMachine.SetDisconnected()
            ↓ DisconnectSession(id)
                 ↓ RUDPSessionManager::ReleaseSession(id)
                      ↓ unusedSessionIdList.push_back(id)
                      ↓ connectedUserCount--
```

---

## 7. 내부 동작 — `InitReserveSession`

`RUDPSessionBroker`가 새 클라이언트를 위해 호출:

```cpp
CONNECT_RESULT_CODE MultiSocketRUDPCore::InitReserveSession(OUT RUDPSession& session) const
```

```
1. CreateRUDPSocket()
   ├─ WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, WSA_FLAG_REGISTERED_IO)
   ├─ bind(INADDR_ANY, port=0)     ← OS가 자동으로 포트 할당
   └─ getsockname() → session.socketContext.SetServerPort(ntohs(addr.sin_port))

   실패 → CREATE_SOCKET_FAILED 반환

2. RAII 가드 설정
   └─ ScopeExit: 실패 시 session.rioContext.Cleanup() + CloseSocket() 자동 호출

3. rioManager->InitializeSessionRIO(session, session.GetThreadId())
   ├─ recvCQ = rioCompletionQueues[threadId]
   ├─ sendCQ = rioCompletionQueues[threadId]   (동일 큐 사용)
   └─ sessionDelegate.InitializeSessionRIO(session, rioFunctionTable, recvCQ, sendCQ)
        └─ session.InitializeRIO(...)
             ├─ SessionRecvContext::Initialize()
             │    ├─ RIORegisterBuffer(recvBuffer, 16KB)
             │    ├─ RIORegisterBuffer(clientAddrBuffer, sizeof SOCKADDR_INET)
             │    └─ RIORegisterBuffer(localAddrBuffer, sizeof SOCKADDR_INET)
             ├─ SessionSendContext::Initialize()
             │    └─ RIORegisterBuffer(rioSendBuffer, 32KB)
             └─ RIOCreateRequestQueue(sock, 1, 1, 1, 1, recvCQ, sendCQ, &sessionId)

   실패 → RIO_INIT_FAILED 반환

4. ioHandler->DoRecv(session)
   └─ RIOReceiveEx(recvRIORQ, context, localAddrBuf, clientAddrBuf, ...)
   실패 → DO_RECV_FAILED 반환

5. session.stateMachine.SetReserved()

6. raii.Dismiss()    ← 성공, RAII 가드 해제
   return SUCCESS
```

**`RIOCreateRequestQueue` 파라미터:**

```cpp
RIOCreateRequestQueue(
    sock,          // 세션 소켓
    1,             // MaxOutstandingReceive (한 번에 등록 가능한 recv 수)
    1,             // MaxReceiveDataBuffers
    1,             // MaxOutstandingSend
    1,             // MaxSendDataBuffers
    recvCQ,        // 수신 완료 큐
    sendCQ,        // 송신 완료 큐
    &sessionId     // RequestContext (완료 시 반환됨)
);
```

> **MaxOutstandingReceive = 1인 이유:**  
> 하나의 UDP 소켓에서는 한 번에 하나의 데이터그램만 수신 대기하면 충분하다.  
> UDP는 한 번의 recvfrom이 하나의 완전한 데이터그램을 반환하므로  
> TCP처럼 여러 버퍼를 미리 등록할 필요가 없다.

---

## 8. 옵션 파일 설정값 전체

### CoreOption.ini

```ini
[CORE]
THREAD_COUNT=4                    ; IO/Logic/Retransmission 워커 스레드 수 (N)
NUM_OF_SOCKET=1000                ; 세션 풀 크기 = 최대 동시 접속 수
MAX_PACKET_RETRANSMISSION_COUNT=15 ; 재전송 횟수 초과 시 강제 종료
WORKER_THREAD_ONE_FRAME_MS=1      ; IO Worker 프레임 슬립 (0이면 비활성)
RETRANSMISSION_MS=200             ; 패킷 미ACK 시 재전송 간격 (ms)
RETRANSMISSION_THREAD_SLEEP_MS=50 ; 재전송 스레드 슬립 간격 (ms)
HEARTBEAT_THREAD_SLEEP_MS=3000    ; 하트비트 전송 주기 (ms)
TIMER_TICK_MS=16                  ; Ticker 틱 간격 (~60fps)
MAX_HOLDING_PACKET_QUEUE_SIZE=32  ; 순서 보장용 홀딩 큐 크기

[SERIALIZEBUF]
PACKET_CODE=0x89    ; 헤더 코드 (클라이언트와 반드시 동일)
PACKET_KEY=0x99     ; XOR 키
```

### SessionBrokerOption.ini

```ini
[SESSION_BROKER]
CORE_IP=192.168.1.100    ; 클라이언트에게 알릴 UDP 서버 IP
SESSION_BROKER_PORT=10001 ; 세션 브로커 리스닝 포트 (TCP)
```

### 설정값 선택 가이드

| 시나리오 | 권장 설정 |
|----------|-----------|
| 저레이턴시 우선 (FPS 게임) | `WORKER_THREAD_ONE_FRAME_MS=0`, `RETRANSMISSION_MS=50` |
| CPU 절약 우선 (MMO) | `WORKER_THREAD_ONE_FRAME_MS=1`, `RETRANSMISSION_MS=200` |
| 세션 수 많음 (1000+) | `THREAD_COUNT` ≥ 4, `NUM_OF_SOCKET` 적절히 |
| 불안정 네트워크 | `MAX_PACKET_RETRANSMISSION_COUNT` 증가, `RETRANSMISSION_MS` 증가 |
| 고빈도 하트비트 필요 | `HEARTBEAT_THREAD_SLEEP_MS` 감소 |

---

## 9. 멀티소켓 구조의 의미

기존 IOCP 서버가 **하나의 서버 소켓**에서 모든 클라이언트 패킷을 처리하는 것과 달리,  
MultiSocketRUDP는 **세션마다 독립된 UDP 소켓을 가진다.**

```
전통적 UDP 서버:
  하나의 소켓 → recvfrom → 클라이언트 주소로 분기

MultiSocketRUDP:
  세션 A → 자체 UDP 소켓 (Port 50001) → RIO 완료 큐 [0]
  세션 B → 자체 UDP 소켓 (Port 50002) → RIO 완료 큐 [0]
  세션 C → 자체 UDP 소켓 (Port 50003) → RIO 완료 큐 [1]
  세션 D → 자체 UDP 소켓 (Port 50004) → RIO 완료 큐 [1]
```

**장점:**
- **RIO 완료 큐 분리**: 세션이 스레드에 고정 배정되어 완료 큐 경쟁 없음
- **주소 검증 간소화**: 소켓마다 알려진 클라이언트만 수신하므로 IP+Port 검증 용이
- **암호화 컨텍스트 분리**: 세션 키가 소켓 단위로 독립 → 키 공유 위험 없음

**단점:**
- OS 소켓 자원 소비 증가 (파일 디스크립터 1개/세션)
- 포트 자원 소비 (OS가 임시 포트 할당)

> **실제 포트 범위**: `bind(port=0)`으로 OS에게 포트 할당 위임.  
> Linux 기본 임시 포트 범위: 32768~60999.  
> Windows: `netsh int ipv4 show dynamicport udp`으로 확인 가능.

---

## 10. 의존 컴포넌트

```
MultiSocketRUDPCore
 ├── unique_ptr<RUDPSessionManager>       ← 세션 풀
 ├── unique_ptr<RIOManager>               ← RIO 완료 큐 / 버퍼 등록
 ├── unique_ptr<RUDPIOHandler>            ← DoRecv / DoSend / IOCompleted
 ├── unique_ptr<RUDPPacketProcessor>      ← PacketType 분기, DecodePacket
 ├── unique_ptr<RUDPSessionBroker>        ← TLS 세션 발급
 ├── unique_ptr<RUDPThreadManager>        ← jthread 그룹 관리
 │
 ├── RUDPSessionFunctionDelegate          ← ISessionDelegate 구현체
 │    └─ RUDPSession private 멤버에 접근하는 브릿지
 │
 ├── MultiSocketRUDPCoreFunctionDelegate  ← 싱글톤 함수 위임자
 │    └─ RUDPSession → Core 역방향 호출 (friend 없이)
 │
 ├── CTLSMemoryPool<IOContext>            ← Send IOContext TLS 풀
 ├── CTLSMemoryPool<RecvIOCompletedContext> ← Recv 완료 컨텍스트 TLS 풀
 │
 ├── vector<CListBaseQueue<RecvIOCompletedContext*>> recvIOCompletedContexts[N]
 ├── vector<HANDLE> recvLogicThreadEventHandles[N]  ← Semaphore
 ├── vector<list<SendPacketInfo*>> sendPacketInfoList[N]
 └── vector<unique_ptr<mutex>> sendPacketInfoListLock[N]
```

### `MultiSocketRUDPCoreFunctionDelegate` 의 역할

`RUDPSession`이 `MultiSocketRUDPCore`의 메서드를 호출해야 하는 경우,  
순환 의존성 없이 friend를 남용하지 않으면서 호출하기 위한 싱글톤 브릿지:

```cpp
// RUDPSession.cpp 내부
void RUDPSession::DoDisconnect() {
    // ...
    MultiSocketRUDPCoreFunctionDelegate::PushToDisconnectTargetSession(*this);
    //  → Instance().core->PushToDisconnectTargetSession(*this)
}
```

```cpp
// RUDPSessionBroker.cpp 내부
RUDPSession* RUDPSessionBroker::ReserveSession(...) {
    return MultiSocketRUDPCoreFunctionDelegate::AcquireSession();
    //  → Instance().core->AcquireSession()
}
```

---

## 관련 문서
- [[RUDPSession]] — 세션 상속 및 API 사용법
- [[RUDPSessionBroker]] — TLS 세션 발급 상세
- [[ThreadModel]] — 스레드 그룹 상세 동작
- [[PacketProcessing]] — 수신 파이프라인
- [[RUDPSessionManager]] — 세션 풀 관리
- [[GettingStarted]] — 처음부터 서버 구축하기
- [[Troubleshooting]] — 자주 발생하는 문제와 해결
