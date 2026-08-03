# RIOManager

> **Windows RIO(Registered I/O) API를 추상화하는 관리자.**  
> 완료 큐(CQ) 생성, 함수 테이블 로드, 세션별 RIO 초기화, 완료 결과 디큐를 담당한다.  
> 일반 IOCP 기반 소켓보다 커널 → 사용자 공간 전환 비용이 적고 처리량이 높다.

---

## 목차

1. [RIO 개요 — 왜 RIO를 사용하는가](#1-rio-개요--왜-rio를-사용하는가)
2. [내부 구조](#2-내부-구조)
3. [초기화 — Initialize](#3-초기화--initialize)
4. [RIO 함수 테이블 로드](#4-rio-함수-테이블-로드)
5. [세션 RIO 초기화 — InitializeSessionRIO](#5-세션-rio-초기화--initializesessionrio)
6. [완료 큐 디큐 — DequeueCompletions](#6-완료-큐-디큐--dequeuecompletions)
7. [버퍼 등록/해제](#7-버퍼-등록해제)
8. [완료 큐 크기 계산](#8-완료-큐-크기-계산)
9. [RIO vs IOCP 비교](#9-rio-vs-iocp-비교)

---

## 1. RIO 개요 — 왜 RIO를 사용하는가

**Registered I/O (RIO)**는 Windows 8 / Server 2012부터 지원하는 고성능 소켓 I/O 인터페이스.

**일반 IOCP 소켓과의 차이:**

| 특성 | 일반 IOCP | RIO |
|------|-----------|-----|
| 버퍼 등록 | 매 호출마다 | **1회 등록 후 재사용** |
| 완료 알림 | `GetQueuedCompletionStatus` (커널→유저 전환) | 폴링 (`RIODequeueCompletion`) |
| 커널 전환 | 매 I/O마다 | 완료 확인 시 최소화 |
| 메모리 복사 | 2회 (커널 버퍼→사용자 버퍼) | 1회 (등록된 버퍼에 직접 기록) |
| 추천 용도 | 범용 | **고빈도 소형 패킷, 게임 서버** |

**RIO의 핵심 절감:**
1. `WSALock` / `WSAUnlock` 없음 → 락 오버헤드 제거
2. 사전 등록된 버퍼(page-locked) 사용 → DMA 직접 전달, 복사 1회
3. 폴링 기반 완료 확인 → 커널 진입 없이 사용자 공간에서 처리

---

## 2. 내부 구조

```cpp
class RIOManager {
    RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable;    // RIO API 함수 포인터
    std::vector<RIO_CQ> rioCompletionQueues;          // 스레드당 완료 큐 (N개)
    ISessionDelegate& sessionDelegate;
};
```

**완료 큐 분리 설계:**

```
Session 0,N,2N,...  → rioCompletionQueues[0]  → IO Worker Thread 0
Session 1,N+1,2N+1,...→ rioCompletionQueues[1]  → IO Worker Thread 1
...
Session N-1,2N-1,...→ rioCompletionQueues[N-1] → IO Worker Thread N-1
```

같은 완료 큐는 항상 같은 IO Worker Thread만 접근 → **락 없이** `RIODequeueCompletion` 호출 가능.

---

## 3. 초기화 — `Initialize`

```cpp
bool RIOManager::Initialize(
    size_t numOfSockets,          // 전체 세션 수
    size_t numOfWorkerThreads    // IO Worker Thread 수 (= N)
)
```

```cpp
{
    // ① RIO 함수 테이블 로드
    if (!LoadRIOFunctionTable()) return false;

    // ② 스레드당 완료 큐 생성
    const size_t sessionsPerWorker =
        numOfSockets / numOfWorkerThreads
        + (numOfSockets % numOfWorkerThreads != 0);
    const size_t queueSize =
        sessionsPerWorker * (RECV_OUTSTANDING_COUNT + 1);
    for (size_t i = 0; i < numOfWorkerThreads; ++i) {

        RIO_CQ cq = rioFunctionTable.RIOCreateCompletionQueue(
            static_cast<ULONG>(queueSize), nullptr);
        if (cq == RIO_INVALID_CQ) {
            LOG_ERROR(std::format("RIOCreateCompletionQueue failed for thread {}", i));
            return false;
        }

        rioCompletionQueues.push_back(cq);
    }

    return true;
}
```

---

## 4. RIO 함수 테이블 로드

```cpp
bool RIOManager::LoadRIOFunctionTable()
{
    // ① 임시 소켓 생성 (함수 테이블 로드 목적으로만 사용)
    SOCKET tempSocket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
                                  nullptr, 0, WSA_FLAG_REGISTERED_IO);
    if (tempSocket == INVALID_SOCKET) {
        LOG_ERROR("WSASocket for RIO table failed");
        return false;
    }

    // ② WSAID_MULTIPLE_RIO로 함수 포인터 요청
    GUID extensionFunctionId = WSAID_MULTIPLE_RIO;
    DWORD dwBytes = 0;

    int result = WSAIoctl(
        tempSocket,
        SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
        &extensionFunctionId,
        sizeof(extensionFunctionId),
        &rioFunctionTable,
        sizeof(rioFunctionTable),
        &dwBytes,
        nullptr, nullptr
    );

    closesocket(tempSocket);

    if (result == SOCKET_ERROR) {
        LOG_ERROR("WSAIoctl SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER failed");
        return false;
    }

    return true;
}
```

**`RIO_EXTENSION_FUNCTION_TABLE`에 로드되는 함수 목록:**

```cpp
struct RIO_EXTENSION_FUNCTION_TABLE {
    DWORD cbSize;
    LPFN_RIORECEIVE          RIOReceive;
    LPFN_RIORECEIVEEX        RIOReceiveEx;       // 주소 버퍼 포함
    LPFN_RIOSEND             RIOSend;
    LPFN_RIOSENDEX           RIOSendEx;          // 현재 송신 경로에서 사용
    LPFN_RIOCLOSECOMPLETIONQUEUE RIOCloseCompletionQueue;
    LPFN_RIOCREATECOMPLETIONQUEUE RIOCreateCompletionQueue;
    LPFN_RIOCREATEREQUESTQUEUE    RIOCreateRequestQueue;
    LPFN_RIODEQUEUECOMPLETION     RIODequeueCompletion;
    LPFN_RIODEREGISTERBUFFER      RIODeregisterBuffer;
    LPFN_RIONOTIFY               RIONotify;
    LPFN_RIOREGISTERBUFFER        RIORegisterBuffer;
    LPFN_RIORESIZECOMPLETIONQUEUE RIOResizeCompletionQueue;
    LPFN_RIORESIZEREQUESTQUEUE    RIOResizeRequestQueue;
};
```

임시 소켓이 필요한 이유: RIO 함수 포인터는 소켓 핸들을 통해 동적으로 로드해야 하며,  
`GetProcAddress` 방식으로는 접근 불가능하다. 로드 후 소켓은 즉시 닫는다.

---

## 5. 세션 RIO 초기화 — `InitializeSessionRIO`

```cpp
bool RIOManager::InitializeSessionRIO(
    RUDPSession& session,
    unsigned char threadId
)
```

```cpp
{
    RIO_CQ cq = rioCompletionQueues[threadId];  // 이 세션이 사용할 완료 큐

    // sessionDelegate를 통해 세션 내부의 RIO 컨텍스트 초기화
    return sessionDelegate.InitializeSessionRIO(session, rioFunctionTable, cq, cq);
    // recv CQ = send CQ = 같은 큐 (IO Worker Thread 하나가 양방향 처리)
}
```

**세션 내부 `InitializeRIO` 흐름:**

```cpp
// SessionRIOContext::Initialize (sessionDelegate를 통해 호출)
bool Initialize(
    const RIO_EXTENSION_FUNCTION_TABLE& rioFunc,
    RIO_CQ recvCQ,
    RIO_CQ sendCQ,
    SOCKET socket,
    SessionIdType sessionId,
    RUDPSession* ownerSession,
    unsigned short pendingQueueCapacity)
{
    cachedSessionId = sessionId;

    // ① RecvContext 초기화: 현재 8개 slot 각각에 data/local/remote buffer 등록
    recvContext.Initialize(rioFunc, sessionId, ownerSession);

    // ② SendContext 초기화
    sendContext.Initialize(rioFunc, pendingQueueCapacity);
    // → RIORegisterBuffer(rioSendBuffer, 32KB)

    // ③ RIO Request Queue 생성
    rioRQ = rioFunc.RIOCreateRequestQueue(
        socket,     // 세션 소켓
        RECV_OUTSTANDING_COUNT, // MaxOutstandingReceive (현재 8)
        1,          // MaxReceiveDataBuffers
        1,          // MaxOutstandingSend
        1,          // MaxSendDataBuffers
        recvCQ,     // 수신 완료 큐
        sendCQ,     // 송신 완료 큐
        &cachedSessionId
        // ↑ SocketContext: 완료 시 session id를 읽는 세션 소유 메모리
    );

    // 실패하면 recv/send 등록을 Cleanup한다.
    return rioRQ != RIO_INVALID_RQ;
}
```

`RECV_OUTSTANDING_COUNT`는 세션 요청 큐가 동시에 유지할 수 있는 수신 작업 수다. 현재 값은 8이다. 송신은 32KB `MAX_SEND_BUFFER_SIZE` 버퍼에 여러 패킷을 묶더라도 하나의 `RIOSendEx` 작업으로 등록하므로 `MaxOutstandingSend`는 1이다. 버퍼의 바이트 크기와 outstanding 작업 개수는 서로 다른 단위다.

---

## 6. 완료 큐 디큐 — `DequeueCompletions`

```cpp
ULONG RIOManager::DequeueCompletions(
    unsigned char threadId,
    OUT RIORESULT* results,
    ULONG maxResults
) const
```

```cpp
{
    return rioFunctionTable.RIODequeueCompletion(
        rioCompletionQueues[threadId],
        results,
        maxResults
    );
    // 비블로킹: 완료된 작업 없으면 0 반환
    // 최대 maxResults(1024)개까지 한 번에 디큐
}
```

**`RIORESULT` 구조:**

```cpp
struct RIORESULT {
    LONG     Status;          // 0=성공, 그 외=작업별 오류 코드
    ULONG    BytesTransferred; // 전송된 바이트 수
    ULONGLONG SocketContext;   // RIOCreateRequestQueue의 RequestContext
                               //  = sessionId (uintptr_t 캐스팅)
    ULONGLONG RequestContext;  // RIOReceiveEx/RIOSend의 RequestContext
                               //  = IOContext* 포인터
};
```

**IO Worker Thread에서의 처리:**

```cpp
RIORESULT rioResults[1024];
ULONG count = rioManager->DequeueCompletions(threadId, rioResults, 1024);

for (ULONG i = 0; i < count; ++i) {
    // RequestContext에서 IOContext 복원
    IOContext* context = reinterpret_cast<IOContext*>(rioResults[i].RequestContext);

    ioHandler->IOCompleted(context,
                           rioResults[i].BytesTransferred,
                           threadId,
                           rioResults[i].Status);
}
```

`IOCompleted`는 컨텍스트에 저장된 session 포인터·ID·generation을 검증한다. 오류나
취소 완료도 같은 함수로 전달되어 recv slot/카운터 또는 send context를 반드시 정리한다.

---

## 7. 버퍼 등록/해제

**`RIORegisterBuffer`** (SessionRecvContext, SessionSendContext에서 호출):

```cpp
// SessionRecvContext::Initialize(): 현재 8개 slot에 반복
for (auto& slot : recvBuffer.slots) {
    auto& context = slot.recvContext;
    context->BufferId = rioFunc.RIORegisterBuffer(
        slot.buffer, RECV_BUFFER_SIZE);
    context->clientAddrRIOBuffer.BufferId = rioFunc.RIORegisterBuffer(
        context->clientAddrBuffer, sizeof(SOCKADDR_INET));
    context->localAddrRIOBuffer.BufferId = rioFunc.RIORegisterBuffer(
        context->localAddrBuffer, sizeof(SOCKADDR_INET));
}

// 세션 send 버퍼 (32KB)
RIO_BUFFERID sendBufferId = rioFunc.RIORegisterBuffer(
    rioSendBuffer,
    MAX_SEND_BUFFER_SIZE
);
```

**`RIODeregisterBuffer`** (세션 해제 시):

```cpp
// SessionRecvContext::Cleanup()
for (const auto& slot : recvBuffer.slots) {
    rioFunc.RIODeregisterBuffer(slot.recvContext->BufferId);
    rioFunc.RIODeregisterBuffer(slot.recvContext->clientAddrRIOBuffer.BufferId);
    rioFunc.RIODeregisterBuffer(slot.recvContext->localAddrRIOBuffer.BufferId);
}

// SessionSendContext::Cleanup()
rioFunc.RIODeregisterBuffer(sendBufferId);
```

> 버퍼 등록은 해당 메모리 영역을 page-lock한다.  
> 세션 해제 시 반드시 `RIODeregisterBuffer`를 호출해 page-lock을 해제해야 한다.  
> 해제 없이 세션 객체가 소멸되면 page-lock이 남아 메모리 누수 + 성능 저하 발생.

---

## 8. 완료 큐 크기 계산

```
스레드당 세션 수 = ceil(numOfSockets / numOfWorkerThreads)
세션당 최대 완료 수 = RECV_OUTSTANDING_COUNT + 최대 동시 Send(1)
CQ 크기 = 스레드당 세션 수 × 세션당 최대 완료 수

현재 샘플: numOfSockets=500, numOfWorkerThreads=4, RECV_OUTSTANDING_COUNT=8
  → ceil(500/4) × (8+1) = 1125

나머지 세션이 특정 worker에 배정되는 경우까지 포함하도록 올림 계산한다.
```

**CQ 크기가 너무 작으면:**  
`RIODequeueCompletion`이 완료 결과를 잃을 수 있다.  
서버 시작 시 `RIOCreateCompletionQueue` 성공 여부를 확인하고,
`RIODequeueCompletion`의 `RIO_CORRUPT_CQ` 반환값을 결과 개수로 사용하지 않아야 한다.
CQ 손상은 안전하게 복구할 수 없는 치명적 상태이므로 오류를 기록하고 해당 IO Worker를
종료하며, `ServerFatalError` callback을 통해 상위 레이어에 전달한다. 실제 프로세스 재시작 요청과 운영 처리 절차는 [[FatalErrorHandling|치명 오류 통지와 프로세스 재시작]]을 따른다.

---

## 9. RIO vs IOCP 비교

```
IOCP 기반 UDP 서버 흐름:
  1. WSARecvFrom() → OVERLAPPED 등록
  2. 패킷 도착 → 커널이 사용자 버퍼로 복사
  3. GetQueuedCompletionStatus() → 커널→유저 전환
  4. 데이터 처리
  5. WSARecvFrom() 다시 등록

RIO 기반 UDP 서버 흐름:
  1. RIOReceiveEx() → 등록된 버퍼에 직접 수신 예약
  2. 패킷 도착 → 등록된 page-locked 버퍼에 DMA 기록
  3. RIODequeueCompletion() → 유저 공간 폴링 (커널 전환 없음)
  4. 데이터 처리 (memcpy → NetBuffer)
  5. RIOReceiveEx() 다시 등록

성능 차이 발생 지점:
  ▼ IOCP: 완료마다 커널→유저 컨텍스트 스위칭
  ▼ IOCP: 내부 버퍼 → 사용자 버퍼 복사 (2회)
  ✅ RIO: page-locked 버퍼 → DMA (1회만)
  ✅ RIO: 폴링 → 컨텍스트 스위칭 없음 (CPU는 더 쓰지만 레이턴시 낮음)
```

---

## 관련 문서
- [[MultiSocketRUDPCore]] — RIOManager 생성 및 Initialize 호출
- [[RUDPIOHandler]] — RIOReceiveEx / RIOSend 호출
- [[ThreadModel]] — IO Worker Thread의 DequeueCompletions 루프
- [[SessionComponents]] — SessionRecvContext/SendContext RIO 버퍼 초기화
- [[PerformanceTuning]] — 완료 큐 크기 및 스레드 설정
- [[FatalErrorHandling]] — CQ 손상 통지와 프로세스 재시작 정책
