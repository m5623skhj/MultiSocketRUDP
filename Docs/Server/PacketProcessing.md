# 패킷 처리 파이프라인

> UDP 데이터그램이 RIO 수신 버퍼에 도착해서 콘텐츠 핸들러까지 전달되는 전체 경로.  
> 각 단계마다 어느 스레드가 실행하는지, 어떤 락/플래그가 사용되는지, 실패 시 어떻게 처리되는지 정리한다.

---

## 목차

1. [전체 파이프라인 다이어그램](#1-전체-파이프라인-다이어그램)
2. [단계 1: RIO 완료 큐 디큐 (IO Worker)](#2-단계-1-rio-완료-큐-디큐-io-worker)
3. [단계 2: RecvIOCompleted — 버퍼 복사](#3-단계-2-recviocompletd--버퍼-복사)
4. [단계 3: RecvLogic Worker 깨우기](#4-단계-3-recvlogic-worker-깨우기)
5. [단계 4: 사전 유효성 검사](#5-단계-4-사전-유효성-검사)
6. [단계 5: PacketType 분기](#6-단계-5-packettype-분기)
7. [단계 6: AES-GCM 복호화](#7-단계-6-aes-gcm-복호화)
8. [단계 7: 순서 보장 (SessionPacketOrderer)](#8-단계-7-순서-보장-sessionpacketorderer)
9. [단계 8: ProcessPacket (역직렬화 + 핸들러)](#9-단계-8-processpacket-역직렬화--핸들러)
10. [ACK 전송 (SendReplyToClient)](#10-ack-전송-sendreplytoeclient)
11. [이상 패킷 처리 매트릭스](#11-이상-패킷-처리-매트릭스)

---

## 1. 전체 파이프라인 다이어그램

![[PacketFlow.svg]]

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ [IO Worker Thread id=N]                                                      │
│                                                                              │
│  RIODequeueCompletion()                                                      │
│    │                                                                         │
│    ├─ BytesTransferred=0 → 세션 종료 신호 → DoDisconnect()                  │
│    ├─ Status≠0           → RIO 에러 → DoDisconnect()                        │
│    └─ 정상 완료          → IOCompleted(context, transferred, threadId)       │
│                                   │                                          │
│                          ┌────────┴────────┐                                │
│                          │ ioType?         │                                 │
│                    OP_RECV│                 │OP_SEND                         │
│                          ▼                 ▼                                 │
│              RecvIOCompleted()     SendIOCompleted()                         │
│              NetBuffer 복사        IO_NONE_SENDING 복원                      │
│              Semaphore.Release(1)  DoSend() 재호출                           │
│              DoRecv() 재등록                                                 │
└──────────────────────────────────────────────────────────────────────────────┘
                          │
                     Semaphore
                          │
┌──────────────────────────────────────────────────────────────────────────────┐
│ [RecvLogic Worker Thread id=N]                                               │
│                                                                              │
│  WaitForMultipleObjects(semaphore, stopEvent)                                │
│    │                                                                         │
│    └─ OnRecvPacket(threadId)                                                 │
│         │                                                                    │
│         ├─ 사전 유효성 검사 (헤더 크기, 클라이언트 주소 크기)               │
│         │                                                                    │
│         └─ ProcessByPacketType(session, buffer, clientAddr)                  │
│              │                                                               │
│              ├─ CONNECT_TYPE  → TryConnect()                                 │
│              ├─ SEND_TYPE     → OnRecvPacket() → 순서 보장 → 핸들러        │
│              ├─ SEND_REPLY_TYPE → OnSendReply() → CWND 증가                 │
│              ├─ DISCONNECT_TYPE → DoDisconnect()                             │
│              └─ HEARTBEAT_REPLY_TYPE → OnSendReply()                        │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 단계 1: RIO 완료 큐 디큐 (IO Worker)

```cpp
RIORESULT rioResults[1024];
ULONG numOfResults = rioManager->DequeueCompletions(threadId, rioResults, 1024);
// → RIODequeueCompletion(rioCompletionQueues[threadId], rioResults, 1024)
// → 비블로킹, 완료된 작업 수 반환
```

**`RIORESULT` 구조체:**

```cpp
struct RIORESULT {
    LONG    Status;               // 0 = 성공, 양수 = NTSTATUS 에러
    ULONG   BytesTransferred;     // 전송된 바이트 수
    ULONGLONG SocketContext;      // RIOCreateRequestQueue 3번째 파라미터
    ULONGLONG RequestContext;     // RIOReceiveEx/RIOSendEx 마지막 파라미터
    // → IOContext* 포인터로 reinterpret
};
```

**Status가 0이 아닌 경우:**

| Status 예시 | 의미 |
|------------|------|
| `0xC0000120` (STATUS_CANCELLED) | 소켓이 닫힘 (`CloseAllSessions()` 후 발생) |
| `0xC000014B` (STATUS_PIPE_BROKEN) | 원격 측에서 강제 종료 |
| `0xC0000005` (STATUS_ACCESS_VIOLATION) | 버퍼 접근 오류 (RIO 버퍼 미등록 등) |

---

## 3. 단계 2: RecvIOCompleted — 버퍼 복사

```cpp
bool RUDPIOHandler::RecvIOCompleted(IOContext* context, ULONG transferred, BYTE threadId) const
{
    auto& session = *context->session;

    // ① 새 NetBuffer 할당 (메모리 풀에서, lock-free)
    NetBuffer* recvPacketBuffer = NetBuffer::Alloc();
    if (recvPacketBuffer == nullptr) {
        session.DoDisconnect();
        return false;
    }

    // ② RIO recv 버퍼에서 NetBuffer로 복사
    auto& recvBuffer = session.rioContext.GetRecvBuffer();
    memcpy_s(
        recvPacketBuffer->m_pSerializeBuffer,
        RECV_BUFFER_SIZE,                        // 16KB
        recvBuffer.buffer,                       // RIO 등록 버퍼 (고정 위치)
        transferred
    );
    recvPacketBuffer->m_iWrite = static_cast<WORD>(transferred);

    // ③ 세션의 수신 버퍼 리스트에 삽입
    session.EnqueueToRecvBufferList(recvPacketBuffer);
    // → recvBuffer.recvBufferList.Enqueue(recvPacketBuffer)

    // ④ RecvLogic Worker에게 알림
    auto* completedContext = recvIOCompletedContextPool.Alloc();
    completedContext->Initialize(context);       // session, clientAddrBuffer 복사
    MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult(completedContext, threadId);
    // → recvIOCompletedContexts[threadId].Enqueue(completedContext)
    // → ReleaseSemaphore(recvLogicThreadEventHandles[threadId], 1, nullptr)

    // ⑤ 즉시 다음 수신 등록 (연속 수신)
    return DoRecv(session);
}
```

**왜 복사가 필요한가?**

RIO recv 버퍼(`recvBuffer.buffer`)는 세션 객체에 고정된 16KB 영역이다.  
`DoRecv()`를 즉시 호출해 다음 패킷을 받아야 하므로, 이 버퍼를 그대로 사용하면  
아직 Logic Worker가 처리 중인 상태에서 덮어써질 수 있다.  
따라서 `NetBuffer`로 복사해 Logic Worker에게 전달하고, recv 버퍼는 즉시 재사용한다.

---

## 4. 단계 3: RecvLogic Worker 깨우기

`ReleaseSemaphore(handle, 1, nullptr)` 호출로 Semaphore 카운트가 1 증가한다.

```
Semaphore 카운트:
  초기: 0
  RecvIOCompleted 호출: +1
  Logic Worker WaitForSingleObject 해소: -1
```

**Semaphore vs AutoResetEvent 선택 이유:**

Semaphore는 Release 횟수를 누적한다.  
IO Worker가 빠르게 여러 패킷을 처리하면, Logic Worker가 대기 중에도  
`Release(1)`이 여러 번 쌓일 수 있다. Logic Worker는 재시작 후 남은 카운트만큼  
즉시 처리한다.

AutoResetEvent는 단 하나의 Signal만 저장하므로 패킷이 여러 개 도착하면  
일부를 놓칠 수 있다.

---

## 5. 단계 4: 사전 유효성 검사

`RUDPPacketProcessor::OnRecvPacket`에서 본 처리 전 두 가지 조건 확인:

```cpp
void RUDPPacketProcessor::OnRecvPacket(
    RUDPSession& session,
    NetBuffer& buffer,
    std::span<const unsigned char> clientAddrBuffer)
{
    // ① 클라이언트 주소 버퍼 크기 확인
    if (clientAddrBuffer.size() < sizeof(sockaddr_in)) {
        LOG_ERROR("Client address buffer too small");
        NetBuffer::Free(&buffer);
        return;
    }

    // ② 페이로드 길이 일치 확인
    // GetPayloadLength() = buffer에서 헤더 파싱 (HeaderCode + PayloadLength)
    if (buffer.GetUseSize() != GetPayloadLength(buffer)) {
        LOG_ERROR(std::format("Invalid packet size. Expected={}, Got={}",
            GetPayloadLength(buffer), buffer.GetUseSize()));
        NetBuffer::Free(&buffer);
        return;
    }

    // ③ 패킷 타입 읽기 (헤더 이후 첫 바이트)
    BYTE packetType;
    buffer >> packetType;

    ProcessByPacketType(session, buffer, clientAddrBuffer, packetType);
    NetBuffer::Free(&buffer);
}
```

**`GetPayloadLength` 동작:**

```cpp
// 헤더 구조: [HeaderCode 1B][PayloadLength 2B]  (총 3 bytes = df_HEADER_SIZE)
//             offset 0        offset 1-2
static WORD GetPayloadLength(const NetBuffer& buffer) {
    static constexpr int PAYLOAD_LENGTH_POSITION = 1;
    return *reinterpret_cast<const WORD*>(&buffer.m_pSerializeBuffer[PAYLOAD_LENGTH_POSITION]);
    // 반환값 = PayloadLength 필드 값 (헤더 이후 바이트 수, AuthTag 포함)
}
```

---

## 6. 단계 5: PacketType 분기

`OnRecvPacket`에서 `std::span<const unsigned char>`로 받은 클라이언트 주소를 `sockaddr_in`으로 변환한 뒤 `ProcessByPacketType`을 호출한다. `packetType`은 파라미터로 전달되지 않고 함수 내부에서 버퍼로부터 직접 추출한다.

```cpp
void RUDPPacketProcessor::ProcessByPacketType(
    RUDPSession& session,
    const sockaddr_in& clientAddr,
    NetBuffer& recvPacket)
{
    PACKET_TYPE packetType;
    recvPacket >> packetType;

    switch (packetType) {
    case PACKET_TYPE::CONNECT_TYPE:
        // ...
    case PACKET_TYPE::SEND_TYPE:
        // ...
    case PACKET_TYPE::SEND_REPLY_TYPE:
        // ...
    case PACKET_TYPE::DISCONNECT_TYPE:
        // ...
    case PACKET_TYPE::HEARTBEAT_TYPE:
        // ...
    case PACKET_TYPE::HEARTBEAT_REPLY_TYPE:
        // ...
    default:
        LOG_ERROR(std::format("Unknown packet type: {}", packetType));
    }
}
```

### CONNECT_TYPE 처리

```cpp
case PACKET_TYPE::CONNECT_TYPE:
{
    // 조건: 세션 키/솔트가 null이 아닌지 확인 (SessionBroker가 설정했는지)
    if (session.GetSessionKeyHandle() == nullptr) {
        LOG_ERROR("CONNECT_TYPE but session not initialized");
        break;
    }

    // 복호화 (isCorePacket=true, direction=CLIENT_TO_SERVER)
    DECODE_PACKET()

    // RESERVED → CONNECTED 전이 시도
    if (!session.TryConnect(recvPacket, clientAddr)) {
        LOG_ERROR("TryConnect failed");
        break;
    }

    // 연결 카운터 증가
    sessionManager.IncrementConnectedCount();

    LOG_DEBUG(std::format("Connected. SessionId={}", session.GetSessionId()));
}
break;
```

### SEND_TYPE 처리

```cpp
case PACKET_TYPE::SEND_TYPE:
{
    // ① 요청한 클라이언트가 이 세션의 클라이언트인지 확인
    //    (IP + Port 일치 검사, 스푸핑 방지)
    if (!session.CanProcessPacket(clientAddr)) break;

    // ② 해제 중인 세션은 처리 안 함
    if (session.IsReleasing()) break;

    // ③ 복호화 (isCorePacket=false, direction=CLIENT_TO_SERVER)
    DECODE_PACKET()

    // ④ 수신 파이프라인 (순서 보장 + 핸들러)
    if (!session.OnRecvPacket(recvPacket)) {
        session.DoDisconnect();
    }

    // ⑤ TPS 카운터
    tps.fetch_add(1, std::memory_order_relaxed);
}
break;
```

### SEND_REPLY_TYPE 처리 (ACK 수신)

```cpp
case PACKET_TYPE::SEND_REPLY_TYPE:
{
    if (!session.CanProcessPacket(clientAddr)) break;

    // 복호화 (direction=CLIENT_TO_SERVER_REPLY)
    DECODE_PACKET()

    session.OnSendReply(recvPacket);
    // → sendPacketInfoMap에서 제거
    // → flowManager.OnAckReceived()
    // → EraseSendPacketInfo()
    // → TryFlushPendingQueue()
}
break;
```

### DISCONNECT_TYPE 처리

```cpp
case PACKET_TYPE::DISCONNECT_TYPE:
{
    if (!session.CanProcessPacket(clientAddr)) break;
    DECODE_PACKET()

    session.DoDisconnect();
    LOG_DEBUG(std::format("Client disconnected. SessionId={}", session.GetSessionId()));
}
break;
```

---

## 7. 단계 6: AES-GCM 복호화

### `DECODE_PACKET` 매크로

```cpp
#define DECODE_PACKET() \
    if (!PacketCryptoHelper::DecodePacket( \
            recvPacket, \
            session.GetSessionSalt(), \
            SESSION_SALT_SIZE, \
            session.GetSessionKeyHandle(), \
            isCorePacket, \
            direction)) \
    { \
        LOG_ERROR(std::format("DecodePacket failed. SessionId={}, type={}", \
            session.GetSessionId(), packetType)); \
        break; \
    }
```

**break로 조용히 폐기하는 이유:**  
AES-GCM 인증 실패는 다음 중 하나를 의미한다:
- 위변조된 패킷
- 재전송 공격 (Nonce 재사용)
- 네트워크 오류로 인한 데이터 깨짐

어떤 경우든 세션을 즉시 종료할 이유는 없다. 단순 폐기가 더 안전하다.  
(단, 반복적으로 발생하면 로그를 통해 파악 가능)

### DecodePacket 내부 동작

```cpp
bool PacketCryptoHelper::DecodePacket(
    NetBuffer& buffer,
    const unsigned char* sessionSalt,
    size_t saltSize,
    const BCRYPT_KEY_HANDLE& keyHandle,
    bool isCorePacket,
    CryptoHelper::DIRECTION direction)
{
    // ① 시퀀스 번호 추출 (암호화 범위 밖)
    PacketSequence sequence;
    memcpy(&sequence, buffer.m_pSerializeBuffer + CRYPTO_START_OFFSET + PACKET_TYPE_SIZE,
           sizeof(PacketSequence));

    // ② Nonce 생성
    auto nonce = CryptoHelper::GetTLSInstance().GenerateNonce(
        sessionSalt, saltSize, sequence, direction);

    // ③ AAD 설정 (PacketType 1B + Sequence 8B = 9B)
    auto aad = std::span(buffer.m_pSerializeBuffer + CRYPTO_START_OFFSET,
                         PACKET_TYPE_SIZE + sizeof(PacketSequence));

    // ④ AES-GCM 복호화 + 태그 검증
    // isCorePacket=false → PacketId(4B)도 암호화 범위에 포함
    // isCorePacket=true  → PacketId 없음 (CONNECT, HEARTBEAT 등)

    return CryptoHelper::GetTLSInstance().DecryptAESGCM(
        buffer, nonce, aad, isCorePacket);
}
```

→ 상세 구조: [[CryptoSystem]]

---

## 8. 단계 7: 순서 보장 (SessionPacketOrderer)

```cpp
bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket)
{
    PacketSequence packetSequence;
    recvPacket >> packetSequence;

    // ① 수신 윈도우 확인
    if (!flowManager.CanAccept(packetSequence)) {
        // 윈도우 범위 밖 → 폐기 (중복이거나 너무 먼 미래 시퀀스)
        return true;
    }

    // ② 순서 보장 처리
    auto result = sessionPacketOrderer.OnReceive(
        packetSequence,
        recvPacket,                               // 포인터가 아닌 참조(NetBuffer&)
        [this](NetBuffer& buf, PacketSequence seq) {
            return ProcessPacket(buf, seq);
        }
    );

    switch (result) {
    case ON_RECV_RESULT::PROCESSED:
        return true;

    case ON_RECV_RESULT::DUPLICATED_RECV:
        // 이미 처리한 패킷 → 재ACK만 전송
        SendReplyToClient(packetSequence);
        return true;

    case ON_RECV_RESULT::PACKET_HELD:
        // 순서 맞는 패킷 아직 안 옴 → HoldingQueue에 보관
        return true;

    case ON_RECV_RESULT::ERROR_OCCURED:
        // HoldingQueue 가득 참 → 세션 종료
        return false;
    }
}
```

### SessionPacketOrderer 내부 로직

```cpp
// enum class ON_RECV_RESULT : uint8_t { PROCESSED, DUPLICATED_RECV, PACKET_HELD, ERROR_OCCURED };

ON_RECV_RESULT SessionPacketOrderer::OnReceive(
    PacketSequence sequence,
    NetBuffer& buffer,                        // 포인터가 아닌 참조
    const PacketProcessCallback& callback)    // std::function<bool(NetBuffer&, PacketSequence)>
{
    if (sequence == nextRecvPacketSequence) {
        // ① 정상 순서 패킷 → 즉시 처리
        if (!ProcessAndAdvance(buffer, sequence, callback)) return ON_RECV_RESULT::ERROR_OCCURED;

        // ② HoldingQueue에서 연속으로 처리 가능한 패킷 처리
        if (!ProcessHoldingPacket(callback)) return ON_RECV_RESULT::ERROR_OCCURED;
        return ON_RECV_RESULT::PROCESSED;

    } else if (SeqDiff(sequence, nextRecvPacketSequence) > 0) {
        // ③ 미래 시퀀스 → HoldingQueue에 보관 (priority_queue, min-heap)
        if (recvPacketHolderQueue.size() >= maxHoldingQueueSize) {
            LOG_ERROR(std::format("Holding queue full. maxSize={}", maxHoldingQueueSize));
            return ON_RECV_RESULT::ERROR_OCCURED;
        }
        recvPacketHolderQueue.emplace(&buffer, sequence);
        recvHoldingPacketSequences.insert(sequence);
        return ON_RECV_RESULT::PACKET_HELD;

    } else {
        // ④ 과거 시퀀스 → 중복 패킷
        return ON_RECV_RESULT::DUPLICATED_RECV;
    }
}
```

**구체적인 예시:**

```
수신 순서: 1, 4, 3, 2

sequence=1: expected=1 → ProcessPacket(1) → expected=2
            HoldingQueue: {}

sequence=4: expected=2 → HoldingQueue: {4}
            (4 > 2이므로 보관)

sequence=3: expected=2 → HoldingQueue: {3, 4}
            (3 > 2이므로 보관)

sequence=2: expected=2 → ProcessPacket(2) → expected=3
            ProcessHoldingPacket():
              top=3 == expected=3 → ProcessPacket(3) → expected=4
              top=4 == expected=4 → ProcessPacket(4) → expected=5
            HoldingQueue: {}
```

---

## 9. 단계 8: ProcessPacket (역직렬화 + 핸들러)

```cpp
bool RUDPSession::ProcessPacket(NetBuffer& recvPacket, PacketSequence recvPacketSequence)
{
    // ① PacketId 추출 (4바이트)
    PacketId packetId;
    recvPacket >> packetId;

    // ② 핸들러 검색
    auto itor = packetFactoryMap.find(packetId);
    if (itor == packetFactoryMap.end()) {
        LOG_ERROR(std::format("Unknown packet. packetId: {}", packetId));
        return false;   // → 호출자가 DoDisconnect()
    }

    // ③ 역직렬화 + 핸들러 호출
    // itor->second 는 std::function<std::function<bool()>(RUDPSession*, NetBuffer*)>
    // 호출 시:
    //   1. DerivedType* 캐스팅
    //   2. PacketManager::MakePacket(packetId) → shared_ptr<PacketType>
    //   3. packet->BufferToPacket(buffer) → 역직렬화
    //   4. (derived->*func)(static_cast<PacketType&>(*packet)) → 핸들러 호출
    if (!itor->second(this, &recvPacket)()) {
        LOG_ERROR(std::format("Packet handler failed. packetId: {}", packetId));
        return false;
    }

    // ④ 수신 윈도우 마킹
    flowManager.MarkReceived(recvPacketSequence);
    // → RUDPReceiveWindow: bitmap[recvPacketSequence - windowStart] = true
    // → 앞부분이 모두 수신됐으면 windowStart 슬라이딩

    // ⑤ ACK 전송
    SendReplyToClient(recvPacketSequence);

    return true;
}
```

---

## 10. ACK 전송 (SendReplyToClient)

```cpp
void RUDPSession::SendReplyToClient(PacketSequence recvPacketSequence)
{
    NetBuffer* replyBuffer = NetBuffer::Alloc();

    auto packetType = PACKET_TYPE::SEND_REPLY_TYPE;
    BYTE advertiseWindow = flowManager.GetAdvertisableWindow();
    // → windowSize - usedCount (현재 비어 있는 수신 슬롯 수)

    *replyBuffer << packetType << recvPacketSequence << advertiseWindow;
    // 총 1 + 8 + 1 = 10 bytes (헤더 제외)

    // isReplyType=true → PendingQueue 우회, 재전송 추적 없음
    if (!SendPacket(*replyBuffer, recvPacketSequence, true, true)) {
        DoDisconnect();
    }
}
```

**`advertiseWindow`의 역할:**  
클라이언트가 이 값을 보고 전송 속도를 조절한다 (flow control 수신 측).  
서버가 느리게 처리하면 `advertiseWindow`가 줄어들고 클라이언트 전송 속도가 감소한다.

---

## 11. 이상 패킷 처리 매트릭스

| 패킷 상황 | 처리 방식 | 세션 종료 여부 |
|-----------|-----------|----------------|
| 헤더 크기 불일치 | 폐기 + LOG_ERROR | ❌ |
| 알 수 없는 PacketType | LOG_ERROR | ❌ |
| CanProcessPacket 실패 (IP 불일치) | 조용히 폐기 | ❌ |
| AES-GCM 인증 실패 | LOG_ERROR + 폐기 | ❌ |
| 알 수 없는 PacketId | LOG_ERROR | ✅ (`DoDisconnect`) |
| BufferToPacket 실패 | LOG_ERROR | ✅ |
| HoldingQueue 가득 참 | LOG_ERROR | ✅ |
| TryConnect 실패 (SessionId 불일치 등) | LOG_ERROR | ❌ (단순 drop) |
| BytesTransferred=0 | 세션 종료 | ✅ |
| RIO Status≠0 | 세션 종료 | ✅ |

---

## 관련 문서
- [[RUDPSession]] — OnRecvPacket, ProcessPacket 구현 상세
- [[CryptoSystem]] — AES-GCM 복호화 전체 과정
- [[PacketFormat]] — 패킷 레이아웃 (오프셋, 필드 크기)
- [[ThreadModel]] — IO Worker / RecvLogic 스레드 구조
- [[FlowController]] — CanAccept, MarkReceived, advertiseWindow
- [[Troubleshooting]] — 패킷 처리 실패 시 디버깅
