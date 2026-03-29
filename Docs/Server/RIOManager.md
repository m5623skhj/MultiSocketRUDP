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
    unsigned short numOfSockets,    // 전체 세션 수
    unsigned char numOfWorkerThread // IO Worker Thread 수 (= N)
)
```

```cpp
{
    // ① RIO 함수 테이블 로드
    if (!LoadRIOFunctionTable()) return false;

    // ② 스레드당 완료 큐 생성
    for (unsigned char i = 0; i < numOfWorkerThread; ++i) {
        // 각 큐의 크기 = (세션 수 / 스레드 수) × 최대 Send 버퍼 배수
        DWORD cqSize = static_cast<DWORD>(
            std::ceil(static_cast<double>(numOfSockets) / numOfWorkerThread))
            * MAX_SEND_BUFFER_SIZE;
        // MAX_SEND_BUFFER_SIZE = 32 (한 번의 배치 전송에 포함되는 최대 패킷 수)

        RIO_CQ cq = rioFunctionTable.RIOCreateCompletionQueue(cqSize, nullptr);
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
    LPFN_RIOSENDEX           RIOSendEx;          // 현재 미사용 (RIOSend 사용)
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
// RUDPSession::InitializeRIO (sessionDelegate를 통해 호출)
bool InitializeRIO(
    const RIO_EXTENSION_FUNCTION_TABLE& rioFunc,
    RIO_CQ recvCQ,
    RIO_CQ sendCQ)
{
    // ① RecvContext 초기화
    rioContext.GetRecvBuffer().Initialize(rioFunc, sessionId, this);
    // → RIORegisterBuffer(recvBuffer.buffer, 16KB)
    // → RIORegisterBuffer(clientAddrBuffer, sizeof(SOCKADDR_INET))
    // → RIORegisterBuffer(localAddrBuffer, sizeof(SOCKADDR_INET))

    // ② SendContext 초기화
    rioContext.GetSendBuffer().Initialize(rioFunc, sessionId);
    // → RIORegisterBuffer(rioSendBuffer, 32KB)

    // ③ RIO Request Queue 생성
    rioRQ = rioFunc.RIOCreateRequestQueue(
        socket,     // 세션 소켓
        1,          // MaxOutstandingReceive (한 번에 1개 recv만 등록)
        1,          // MaxReceiveDataBuffers
        MAX_SEND_BUFFER_SIZE,  // MaxOutstandingSend (32개까지 배치 전송)
        1,          // MaxSendDataBuffers
        recvCQ,     // 수신 완료 큐
        sendCQ,     // 송신 완료 큐
        reinterpret_cast<void*>(static_cast<uintptr_t>(sessionId))
        // ↑ RequestContext: 완료 시 RIORESULT.SocketContext에 반환
    );

    return rioRQ != RIO_INVALID_RQ;
}
```

**`MaxOutstandingReceive = 1` 이유:**  
UDP는 데이터그램 기반이므로 하나의 `RIOReceiveEx`가 하나의 완전한 패킷을 수신.  
TCP처럼 스트림을 조각내 받지 않으므로 1개 등록으로 충분하다.  
수신 완료 직후 `RecvIOCompleted`에서 즉시 다음 `DoRecv`를 호출해 연속 수신을 유지한다.

**`MaxOutstandingSend = MAX_SEND_BUFFER_SIZE = 32` 이유:**  
`MakeSendStream`에서 32KB send 버퍼에 여러 패킷을 묶어 보내므로,  
실제로는 1번의 `RIOSend` 호출로 처리된다. 이 값은 `MakeSendStream`의  
배치 한도와 일치시켜야 한다.

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
    LONG     Status;          // 0=성공, 음수=NTSTATUS 오류 코드
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

    // SocketContext에서 sessionId 복원 (보조 확인)
    SessionIdType sessionId = static_cast<SessionIdType>(rioResults[i].SocketContext);

    ioHandler->IOCompleted(context, rioResults[i].BytesTransferred, threadId);
}
```

---

## 7. 버퍼 등록/해제

**`RIORegisterBuffer`** (SessionRecvContext, SessionSendContext에서 호출):

```cpp
// 세션 recv 버퍼 (16KB)
RIO_BUFFERID recvBufferId = rioFunc.RIORegisterBuffer(
    recvBuffer.buffer,     // char[16384] 포인터
    RECV_BUFFER_SIZE       // 16384
);

// 클라이언트 주소 버퍼 (sizeof SOCKADDR_INET = 28 bytes)
RIO_BUFFERID clientAddrBufferId = rioFunc.RIORegisterBuffer(
    clientAddrBuffer,
    sizeof(SOCKADDR_INET)
);

// 세션 send 버퍼 (32KB)
RIO_BUFFERID sendBufferId = rioFunc.RIORegisterBuffer(
    rioSendBuffer,
    MAX_SEND_BUFFER_SIZE_BYTES
);
```

**`RIODeregisterBuffer`** (세션 해제 시):

```cpp
// SessionRecvContext::Cleanup()
rioFunc.RIODeregisterBuffer(recvBufferId);
rioFunc.RIODeregisterBuffer(clientAddrBufferId);
rioFunc.RIODeregisterBuffer(localAddrBufferId);

// SessionSendContext::Cleanup()
rioFunc.RIODeregisterBuffer(sendBufferId);
```

> 버퍼 등록은 해당 메모리 영역을 page-lock한다.  
> 세션 해제 시 반드시 `RIODeregisterBuffer`를 호출해 page-lock을 해제해야 한다.  
> 해제 없이 세션 객체가 소멸되면 page-lock이 남아 메모리 누수 + 성능 저하 발생.

---

## 8. 완료 큐 크기 계산

```
CQ 크기 = ceil(numOfSockets / numOfWorkerThread) × MAX_SEND_BUFFER_SIZE

예: numOfSockets=1000, numOfWorkerThread=4, MAX_SEND_BUFFER_SIZE=32
  → ceil(1000/4) × 32 = 250 × 32 = 8000

의미: 각 IO Worker Thread가 담당하는 최대 세션(250개)에서
      한꺼번에 최대 32개의 Send 완료가 발생해도 수용 가능
```

**CQ 크기가 너무 작으면:**  
`RIODequeueCompletion`이 완료 결과를 잃을 수 있다.  
서버 시작 시 `RIOCreateCompletionQueue` 성공 여부를 반드시 확인해야 한다.

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
