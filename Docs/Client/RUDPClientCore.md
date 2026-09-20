# RUDPClientCore

> **C++ 클라이언트 측 RUDP 코어.**  
> TLS로 세션 정보를 수신하고, UDP 소켓을 생성해 서버에 연결한다.  
> 재전송·순서 보장 신뢰 채널과 최신성 우선 신뢰성 없는 채널, 서버 생존 확인, 흐름 제어를 클라이언트 관점에서 처리한다.

---

## 목차

1. [전체 연결 흐름](#1-전체-연결-흐름)
2. [시작 — `Start`](#2-시작-start)
3. [종료 — `Stop`](#3-종료-stop)
4. [TLS 세션 정보 수신](#4-tls-세션-정보-수신)
5. [CONNECT 패킷 전송](#5-connect-패킷-전송)
6. [데이터 송신 — `SendPacket` / `SendUnreliablePacket`](#6-데이터-송신-sendpacket)
7. [데이터 수신 — `GetReceivedPacket` / `GetReceivedUnreliablePacket`](#7-데이터-수신-getreceivedpacket)
8. [수신 스레드 — `recvThread`](#8-수신-스레드-recvthread)
9. [수신 처리 — `ProcessRecvPacket`](#9-수신-처리-processrecvpacket)
10. [ACK 수신 — `OnSendReply`](#10-ack-수신-onsendreply)
11. [송신 스레드 — `sendThread`](#11-송신-스레드-sendthread)
12. [재전송 스레드 — `RunRetransmissionThread`](#12-재전송-스레드-runretransmissionthread)
13. [흐름 제어 — `TryFlushPendingQueue`](#13-흐름-제어-tryflushpendingqueue)
14. [옵션 파일 설정값](#14-옵션-파일-설정값)
15. [주요 멤버 변수](#15-주요-멤버-변수)
16. [스레드 구조 요약](#16-스레드-구조-요약)
17. [주의사항](#17-주의사항)

---

## 1. 전체 연결 흐름

```
[클라이언트]                           [서버]

Start() 호출
 │
 ├─[1] TLS TCP 연결 ──────────────────► RUDPSessionBroker (TCP Port)
 │       TLSHelperClient::Handshake
 │       RUDP_PROTOCOL_VERSION(2) ──────►│
 │       ◄─────── {version, result,       │
 │                 serverIp, port,        │
 │                 sessionId,             │  (AcquireSession → RESERVED)
 │                 sessionKey,            │  CreateRUDPSocket(port=X)
 │                 sessionSalt} ──────────┘
 │       TLS close_notify 수신
 │       closesocket (TLS 소켓)
 │
 ├─[2] UDP 소켓 생성
 │       socket(SOCK_DGRAM) + bind(port=0)
 │       serverAddr = {serverIp, port=X}
 │
 ├─[3] 스레드 시작
 │       recvThread           (recvfrom 블로킹)
 │       sendThread           (Semaphore 대기 → sendto)
 │       retransmissionThread (타임아웃 체크)
 │
 └─[4] CONNECT 패킷 전송 ────────────► RUDPSession (Port X)
         CONNECT_TYPE | seq=0 | sessionId
         (AES-GCM, CLIENT_TO_SERVER)
                                        TryConnect() 성공
                                        OnConnected() 호출
                                        SEND_REPLY_TYPE | seq=0 ◄──
 recvThread: OnSendReply(seq=0)
   isConnected = true
   ServerAliveChecker 시작
```

---

## 2. 시작 — `Start`

```cpp
bool RUDPClientCore::Start(
    const std::wstring& clientCoreOptionFile,
    const std::wstring& sessionGetterOptionFilePath,
    bool printLogToConsole
)
```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `clientCoreOptionFile` | `const wstring&` | 재전송/생존 확인 설정 INI |
| `sessionGetterOptionFilePath` | `const wstring&` | SessionBroker IP/Port/헤더코드 설정 INI |
| `printLogToConsole` | `bool` | 콘솔 출력 여부 |

**내부 실행 순서:**

```
1. Logger::GetInstance().RunLoggerThread(printLogToConsole)

2. ReadOptionFile(clientCoreOptionFile, sessionGetterOptionFilePath)
   → maxPacketRetransmissionCount, retransmissionMs, serverAliveCheckMs
   → unreliableQueueCapacity (기본 64, 범위 1..65535)
   → sessionBrokerIp, sessionBrokerPort
   → NetBuffer::m_byHeaderCode, m_byXORCode

3. 프로세스 전역 Winsock 참조 획득

4. RunGetSessionFromServer(sessionGetterOptionFilePath)
   → TLS TCP 연결 → 프로토콜 버전 2 전송 → 세션 정보 수신
   → 응답 버전 검증 → 파싱 → BCrypt 키 핸들 생성
   (실패 → return false)

5. CreateRUDPSocket()
   → socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
   → bind(INADDR_ANY, port=0)
   → serverAddr 초기화 {serverIp, serverPort}

6. RunThreads()
   → sendEventHandles[0] = CreateSemaphore(0, LONG_MAX)  ← 패킷 대기
   → sendEventHandles[1] = CreateEvent(manual, FALSE)     ← 종료 신호

   → recvThread = thread([this] { RunRecvThread(); })
   → sendThread = thread([this] { RunSendThread(); })
   → retransmissionThread = thread([this] { RunRetransmissionThread(); })

7. Sleep(1000)으로 송수신 스레드 초기화 대기

8. ShouldSendConnectPacketOnStart()가 true이면 SendConnectPacket()
   → CONNECT_TYPE | seq=0 | sessionId
   → EncodePacket(CLIENT_TO_SERVER, isCorePacket=true)
   → SendPacket(..., isCorePacket=true) 경로를 통해 send 큐에 등록

9. return true
```

---

## 3. 종료 — `Stop`

```cpp
void RUDPClientCore::Stop()
```

```
1. closesocket(sessionBrokerSocket)   ← TLS 연결 정리 (이미 닫혔으면 no-op)

2. threadStopFlag = true              ← 모든 스레드 루프 종료 신호

3. SetEvent(sendEventHandles[1])      ← sendThread 깨우기 (종료 이벤트)

4. JoinThreads()
   ├─ serverAliveChecker.StopServerAliveCheck()
   │    → 자기 자신이면 detach(), 아니면 join()
   ├─ retransmissionThread.join()
   ├─ sendThread.join()
   └─ recvThread.join()
       (recvThread: threadStopFlag 확인 or closesocket으로 recvfrom 에러 유발)

5. 자원 정리
   ├─ closesocket(rudpSocket)
   ├─ CloseHandle(sendEventHandles[0,1])
   ├─ sendPacketInfoMap 전체 Free()
   ├─ pendingPacketQueue 전체 Free()
   ├─ recvPacketHoldingQueue 전체 Free()
   ├─ sendBufferQueue 전체 Free()
   ├─ CryptoHelper::DestroySymmetricKeyHandle(sessionKeyHandle)
   └─ delete[] keyObjectBuffer

6. Logger::GetInstance().StopLoggerThread()

7. WSACleanup()
```

**`recvThread` 종료 방법:**  
`recvfrom()`은 블로킹 호출이다. `closesocket(rudpSocket)` 시 `recvfrom`이  
`WSAENOTSOCK` 에러와 함께 즉시 반환된다. 이후 `threadStopFlag` 확인으로 루프 종료.

---

## 4. TLS 세션 정보 수신

### `RunGetSessionFromServer`

```cpp
bool RunGetSessionFromServer(const std::wstring& optionFilePath)
```

```
TryConnectToSessionBroker() — 최대 5회, 1초 간격 재시도
  → connect(sessionBrokerSocket, {sessionBrokerIp, sessionBrokerPort})
  → 실패 시 Sleep(1000) 후 재시도
  → 5회 모두 실패 → return false

TLSHelperClient::Initialize()
  → SCHANNEL_CRED.grbitEnabledProtocols = 0 (Windows 기본 프로토콜 정책)
  → SCHANNEL_CRED.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION
  → AcquireCredentialsHandle(SECPKG_CRED_OUTBOUND)

TLSHelperClient::Handshake(sessionBrokerSocket)
  → InitializeSecurityContext 루프
  → ClientHello / ServerHello / Certificate / Finished 교환

TrySetTargetSessionInfo()
  → recv 루프 + DecryptDataStream
  → payload 완료와 close_notify 확인
  → SetTargetSessionInfo(recvBuffer)
```

### `TrySetTargetSessionInfo` — TLS 스트림 수신 루프

```cpp
bool TrySetTargetSessionInfo()
{
    auto& recvBuffer = *NetBuffer::Alloc();
    std::vector<char> encryptedStream;
    char plainBuffer[MAX_TLS_PACKET_SIZE];
    int totalPlainReceived = 0;
    BYTE code = 0;
    WORD payloadLength = 0;
    bool payloadComplete = false;

    while (true) {
        char tlsRecvBuf[MAX_TLS_PACKET_SIZE];
        int bytes = recv(sessionBrokerSocket, tlsRecvBuf, MAX_TLS_PACKET_SIZE, 0);
        if (bytes <= 0) break;

        // 암호화 스트림에 누적
        encryptedStream.insert(encryptedStream.end(),
                               tlsRecvBuf, tlsRecvBuf + bytes);

        size_t plainSize = 0;
        auto result = tlsHelper.DecryptDataStream(encryptedStream, plainBuffer, plainSize);
        if (result == TLSHelper::TlsDecryptResult::Error) {
            return false;
        }

        if (plainSize > 0) {
            recvBuffer.m_iWrite = 0;
            recvBuffer.WriteBuffer(plainBuffer, static_cast<int>(plainSize));
            totalPlainReceived += static_cast<int>(plainSize);
        }

        if (totalPlainReceived < df_HEADER_SIZE) continue;

        if (payloadLength == 0) {
            recvBuffer >> code >> payloadLength;
        }

        if (totalPlainReceived >= payloadLength + df_HEADER_SIZE) {
            payloadComplete = true;
        }

        if (payloadComplete && result == TLSHelper::TlsDecryptResult::CloseNotify) {
            break;
        }
    }

    shutdown(sessionBrokerSocket, SD_BOTH);
    closesocket(sessionBrokerSocket);
    sessionBrokerSocket = INVALID_SOCKET;

    if (!payloadComplete) {
        NetBuffer::Free(&recvBuffer);
        return false;
    }

    recvBuffer.m_iRead = df_HEADER_SIZE;
    const bool result = SetTargetSessionInfo(recvBuffer);
    NetBuffer::Free(&recvBuffer);
    return result;
}
```

### `SetTargetSessionInfo` — 세션 정보 파싱

```cpp
bool SetTargetSessionInfo(NetBuffer& receivedBuffer)
{
    uint32_t version;
    receivedBuffer >> version;
    if (version != RUDP_PROTOCOL_VERSION) {
        LOG_ERROR("Unsupported RUDP server protocol version");
        return false;
    }

    char connectResultCode;
    receivedBuffer >> connectResultCode;

    if (connectResultCode != 0) {
        LOG_ERROR("Session broker returned error");
        return false;
    }

    // 서버 UDP 주소, 세션 식별 정보, 암호화 키/솔트
    receivedBuffer >> serverIp >> port >> sessionId >> sessionKey >> sessionSalt;

    if (keyObjectBuffer == nullptr) {
        keyObjectBuffer = new unsigned char[
            CryptoHelper::GetTLSInstance().GetKeyObjectSize()];
    }

    if (sessionKeyHandle != nullptr) {
        CryptoHelper::DestroySymmetricKeyHandle(sessionKeyHandle);
        sessionKeyHandle = nullptr;
    }

    sessionKeyHandle = CryptoHelper::GetTLSInstance()
        .GetSymmetricKeyHandle(keyObjectBuffer, sessionKey);
    return sessionKeyHandle != nullptr;
}
```

**수신되는 세션 정보 페이로드 구조:**

```
[HeaderCode 1B][PayloadLen 2B][Reserved 2B]
[RUDP_PROTOCOL_VERSION 4B, little-endian]
[CONNECT_RESULT_CODE 1B]
[serverIp string (len 2B + bytes)]
[serverUdpPort 2B]
[sessionId 2B]
[sessionKey 16B]
[sessionSalt 16B]
```

---

## 5. CONNECT 패킷 전송

```cpp
void SendConnectPacket()
{
    NetBuffer& connectPacket = *NetBuffer::Alloc();

    auto type = PACKET_TYPE::CONNECT_TYPE;
    PacketSequence seq = LOGIN_PACKET_SEQUENCE;  // = 0

    connectPacket << type << seq << sessionId;
    // Total payload: Type(1) + Seq(8) + SessionId(2) = 11 bytes

    SendPacket(connectPacket, seq, true);
}
```

스레드 종료 뒤에는 신뢰성 없는 송신·수신 deque, 신뢰 수신 홀딩 큐, ACK 대기 map, pending queue를 각각의 mutex 아래에서 비우고 시퀀스 상태를 초기화한다. `lifecycleLock`과 `unreliableSendGeneration`은 `Stop()` 도중 직렬화 중이던 신뢰성 없는 송신이 파괴된 키나 이벤트 핸들을 사용하지 못하게 한다.

CONNECT 패킷은 현재 구현에서 일반 콘텐츠 패킷처럼 흐름 제어 대상은 아니지만,
`SendPacket(connectPacket, 0, true)`를 통해 암호화, 재전송 등록, send 큐 삽입 경로를 사용한다.

---

## 6. 데이터 송신 — `SendPacket`

### 외부 API

```cpp
void RUDPClientCore::SendPacket(OUT IPacket& packet)
bool RUDPClientCore::SendUnreliablePacket(IPacket& packet)
```

**콘텐츠 레이어에서 사용하는 주 API.** (BotTester ActionNode 등에서 호출)

```cpp
{
    // ① 시퀀스 번호 증가
    PacketSequence seq = ++lastSendPacketSequence;

    // ② 직렬화
    NetBuffer* buffer = NetBuffer::Alloc();
    auto type = PACKET_TYPE::SEND_TYPE;
    *buffer << type << seq << packet.GetPacketId();
    packet.PacketToBuffer(*buffer);

    // ③ 내부 전송 경로
    SendPacket(*buffer, seq, /*isCorePacket=*/false);
}
```

`SendUnreliablePacket()`은 별도 64비트 시퀀스와 `CLIENT_TO_SERVER_UNREL` nonce 방향을 사용한다. ACK map, remote advertised window, pending queue, 재전송 스레드를 우회한다. 큐가 가득 차면 가장 오래된 항목을 버리고 최신 항목을 넣으며, 반환값 `true`는 원격 전달이 아닌 로컬 큐 수락을 뜻한다. 중지·미연결·직렬화 도중 세대 변경·암호화 실패 시 `false`다.

### 내부 `SendPacket(NetBuffer&, PacketSequence, bool)`

```cpp
void SendPacket(NetBuffer& buffer, PacketSequence seq, bool isCorePacket)
{
    // ① AES-GCM 암호화
    PacketCryptoHelper::EncodePacket(
        buffer, seq,
        PACKET_DIRECTION::CLIENT_TO_SERVER,
        sessionSalt, SESSION_SALT_SIZE,
        sessionKeyHandle, isCorePacket
    );

    // ② 흐름 제어 확인
    const BYTE window = remoteAdvertisedWindow.load(std::memory_order_relaxed);
    if (window == 0) {
        std::scoped_lock lock(pendingPacketQueueLock);
        pendingPacketQueue.push({ seq, &buffer });
        return;
    }

    BYTE outstanding;
    {
        std::scoped_lock lock(sendPacketInfoMapLock);
        outstanding = static_cast<BYTE>(sendPacketInfoMap.size());
    }

    if (outstanding >= window) {
        std::scoped_lock lock(pendingPacketQueueLock);
        pendingPacketQueue.push({ seq, &buffer });
        return;
    }

    // ③ 재전송 등록 + 송신 큐에 삽입
    RegisterSendPacketInfo(buffer, seq);
}
```

### `RegisterSendPacketInfo`

```cpp
void RegisterSendPacketInfo(NetBuffer& buf, PacketSequence seq)
{
    // ① SendPacketInfo 할당
    auto* info = sendPacketInfoPool->Alloc();
    info->Initialize(&buf, seq);
    info->retransmissionTimeStamp = GetTickCount64() + retransmissionMs;

    // ② 재전송 맵에 등록
    {
        std::unique_lock lock(sendPacketInfoMapLock);
        sendPacketInfoMap.insert({ seq, info });
    }

    // ③ send 큐에 삽입
    sendBufferQueue.Enqueue(&buf);

    // ④ send 스레드 깨우기
    ReleaseSemaphore(sendEventHandles[0], 1, nullptr);
}
```

---

## 7. 데이터 수신 — `GetReceivedPacket`

```cpp
NetBuffer* RUDPClientCore::GetReceivedPacket()
```

`recvPacketHoldingQueue`(min-heap, 시퀀스 오름차순)에서  
`nextRecvPacketSequence` 순서로 패킷을 꺼낸다.

```cpp
{
    std::scoped_lock lock(recvPacketHoldingQueueLock);
    DrainReceivedControlPackets();
    if (recvPacketHoldingQueue.empty()
        || recvPacketHoldingQueue.top().packetSequence != nextRecvPacketSequence)
        return nullptr;

    auto* buffer = recvPacketHoldingQueue.top().buffer;
    recvPacketHoldingQueue.pop();
    ++nextRecvPacketSequence;
    DrainReceivedControlPackets();
    return buffer;  // 호출자가 NetBuffer::Free() 책임
}
```

`DrainReceivedControlPackets()`는 앞선 중복과 현재 순번의 heartbeat를 내부에서 소비한다. 따라서 애플리케이션이 추가 수신 호출을 하지 않아도 콘텐츠 패킷 뒤에 연속된 heartbeat가 다음 콘텐츠 시퀀스를 막지 않는다.

**사용 예시 (폴링 방식):**

```cpp
// BotActionGraph 실행 루프 예시
while (client.IsConnected() && !stopFlag) {
    NetBuffer* buf = client.GetReceivedPacket();
    if (buf == nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
    }

    PacketId packetId;
    *buf >> packetId;

    switch (static_cast<PACKET_ID>(packetId)) {
    case PACKET_ID::PONG:
        HandlePong(*buf);
        break;
    case PACKET_ID::MOVE_RES:
        HandleMoveRes(*buf);
        break;
    }

    NetBuffer::Free(buf);  // ← 반드시 호출
}
```

> ⚠️ `GetReceivedPacket`으로 받은 `NetBuffer`는 **반드시 `NetBuffer::Free()`** 해야 한다.  
> 해제하지 않으면 메모리 풀 고갈이 발생한다.

### `GetReceivedUnreliablePacket`

```cpp
NetBuffer* RUDPClientCore::GetReceivedUnreliablePacket();
```

인증과 최신 시퀀스 검사를 통과한 신뢰성 없는 패킷을 수신 순서대로 하나 반환한다. 반환 버퍼의 read 위치는 `PacketId` 앞이며, `GetReceivedPacket()`과 마찬가지로 호출자가 `NetBuffer::Free()` 해야 한다. 큐가 비어 있으면 `nullptr`을 반환한다. 수신 큐가 가득 차면 가장 오래된 미소비 패킷을 제거한다.

---

## 8. 수신 스레드 — `recvThread`

```cpp
void RunRecvThread()
{
    char recvBuffer[RECV_BUFFER_SIZE];
    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);

    while (!threadStopFlag) {
        int bytes = recvfrom(
            rudpSocket,
            recvBuffer, RECV_BUFFER_SIZE,
            0,
            reinterpret_cast<sockaddr*>(&fromAddr),
            &fromLen
        );

        if (bytes == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAENOTSOCK || err == WSAEINTR) break;  // 소켓 닫힘
            LOG_ERROR(std::format("recvfrom error: {}", err));
            continue;
        }

        if (bytes == 0) continue;

        // 서버 주소 확인 (스푸핑 방지)
        if (fromAddr.sin_addr.S_un.S_addr != serverAddr.sin_addr.S_un.S_addr
         || fromAddr.sin_port             != serverAddr.sin_port) {
            LOG_ERROR("Unknown sender address");
            continue;
        }

        // NetBuffer에 복사 후 처리
        NetBuffer* buf = NetBuffer::Alloc();
        memcpy(buf->m_pSerializeBuffer, recvBuffer, bytes);
        buf->m_iWrite = static_cast<WORD>(bytes);

        ProcessRecvPacket(*buf);
    }
}
```

---

## 9. 수신 처리 — `ProcessRecvPacket`

```cpp
void ProcessRecvPacket(NetBuffer& recvBuffer)
{
    PACKET_TYPE packetType;
    PacketSequence packetSequence;
    recvBuffer >> packetType;

    switch (packetType) {

    case PACKET_TYPE::UNRELIABLE_SEND_TYPE:
    {
        if (!isConnected || threadStopFlag) return;
        if (!PacketCryptoHelper::DecodePacket(
                recvBuffer, sessionSalt, SESSION_SALT_SIZE,
                sessionKeyHandle, false,
                PACKET_DIRECTION::SERVER_TO_CLIENT_UNREL)) return;

        authenticatedReceiveCount.fetch_add(1, std::memory_order_relaxed);
        recvBuffer >> packetSequence;
        std::scoped_lock lock(recvPacketHoldingQueueLock);
        if (!unreliableReceiveState.Accept(packetSequence)) return;
        if (unreliableReceivedPackets.size() >= unreliableQueueCapacity) {
            NetBuffer::Free(unreliableReceivedPackets.front());
            unreliableReceivedPackets.pop_front();
        }
        NetBuffer::AddRefCount(&recvBuffer);
        unreliableReceivedPackets.push_back(&recvBuffer);
        return;
    }

    case PACKET_TYPE::SEND_TYPE:
    case PACKET_TYPE::HEARTBEAT_TYPE:
    {
        const bool isCorePacket = packetType == PACKET_TYPE::HEARTBEAT_TYPE;
        if (!PacketCryptoHelper::DecodePacket(
                recvBuffer, sessionSalt, SESSION_SALT_SIZE,
                sessionKeyHandle, isCorePacket,
                PACKET_DIRECTION::SERVER_TO_CLIENT)) break;

        authenticatedReceiveCount.fetch_add(1, std::memory_order_relaxed);
        recvBuffer >> packetSequence;
        unsigned int packetId = 0;
        if (packetType == PACKET_TYPE::SEND_TYPE) {
            const WORD originalRead = recvBuffer.m_iRead;
            recvBuffer >> packetId;
            recvBuffer.m_iRead = originalRead;
        }
        NetBuffer::AddRefCount(&recvBuffer);
        {
            std::scoped_lock lock(recvPacketHoldingQueueLock);
            recvPacketHoldingQueue.emplace(&recvBuffer, packetSequence, packetType);
            DrainReceivedControlPackets();
        }
        if (ShouldSendReplyToServer(packetSequence, packetId)) {
            SendReplyToServer(packetSequence,
                isCorePacket ? PACKET_TYPE::HEARTBEAT_REPLY_TYPE
                             : PACKET_TYPE::SEND_REPLY_TYPE);
        }
        break;
    }

    case PACKET_TYPE::SEND_REPLY_TYPE:
    {
        if (!PacketCryptoHelper::DecodePacket(
                recvBuffer, sessionSalt, SESSION_SALT_SIZE,
                sessionKeyHandle, true,
                PACKET_DIRECTION::SERVER_TO_CLIENT_REPLY)) break;

        authenticatedReceiveCount.fetch_add(1, std::memory_order_relaxed);
        recvBuffer >> packetSequence;
        OnSendReply(recvBuffer, packetSequence);
        break;
    }

    default:
        LOG_ERROR(std::format("Unknown packet type: {}", packetType));
        break;
    }

}
```

`OnRecvStream()`이 데이터그램을 분해해 각 `NetBuffer`의 header code와 payload 길이를 먼저 검증한다. `ProcessRecvPacket()`은 참조가 필요한 큐에 넣을 때만 `AddRefCount()`하며, 호출자인 `OnRecvStream()`은 처리 후 자신의 원래 참조를 항상 해제한다.

### `SendReplyToServer` — ACK 전송

```cpp
void SendReplyToServer(PacketSequence inRecvPacketSequence, PACKET_TYPE packetType = PACKET_TYPE::SEND_REPLY_TYPE);
```

ACK는 PendingQueue나 SendPacketInfo 등록 없이 직접 전송한다. 서버와 같은 논리: ACK는 손실 시 원본 패킷 재전송으로 자연스럽게 재요청된다.

복호화에 성공한 `SEND_TYPE`, `HEARTBEAT_TYPE`, `SEND_REPLY_TYPE`, `UNRELIABLE_SEND_TYPE`은 모두 `authenticatedReceiveCount`를 증가시킨다. `ServerAliveChecker`는 신뢰 패킷 시퀀스가 아니라 이 카운터의 진행 여부로 서버 생존을 판단한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `inRecvPacketSequence` | `PacketSequence` | 응답할 패킷의 시퀀스 번호 |
| `packetType` | `PACKET_TYPE` | 전송할 패킷 타입. 기본값은 `PACKET_TYPE::SEND_REPLY_TYPE` |
## 10. ACK 수신 — `OnSendReply`

```cpp
void OnSendReply(NetBuffer& recvPacket, PacketSequence packetSequence);
```

상대방으로부터 수신된 ACK 패킷을 처리하여 윈도우 크기를 갱신하고, 재전송 맵에서 해당 패킷을 제거한다.

마지막 송신 시퀀스보다 큰 ACK는 즉시 무시한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `recvPacket` | `NetBuffer&` | 수신된 ACK 패킷 버퍼 |
| `packetSequence` | `PacketSequence` | 확인 응답된 패킷 시퀀스 |

### 내부 동작
1. `recvPacket`에서 `advertiseWindow` 정보를 추출하여 `remoteAdvertisedWindow`를 갱신한다.
2. `lastAckedSequence`를 수신된 `packetSequence`로 갱신한다.
3. 시퀀스가 `LOGIN_PACKET_SEQUENCE`이고 연결 전이라면 `isConnected`를 활성화하고 서버 생존 확인을 시작한다.
4. 재전송 맵(`sendPacketInfoMap`)에서 해당 시퀀스의 패킷 정보를 찾아 해제한다.
5. `TryFlushPendingQueue`를 호출하여 보류 중인 패킷 전송을 시도한다.
## 11. 송신 스레드 — `sendThread`

```cpp
void RunSendThread()
{
    while (true) {
        const DWORD result = WaitForMultipleObjects(
            2, sendEventHandles, FALSE, INFINITE);

        if (result == WAIT_OBJECT_0) {
            DoSend();       // 두 큐를 번갈아 선택하며 모두 drain
            continue;
        }
        if (result == WAIT_OBJECT_0 + 1) {
            DoSend();       // 중지 전에 이미 수락한 로컬 항목 정리
            break;
        }
        LOG_ERROR("invalid send wait result");
        break;
    }
}
```

**Semaphore 방식 이유:**  
`ManualResetEvent`는 단일 신호만 기억하므로, 짧은 시간에 패킷이 여러 개 쌓이면  
일부를 처리하지 못할 수 있다. `Semaphore`는 `Release` 횟수를 누적한다.

신뢰·신뢰성 없는 큐에 모두 데이터가 있으면 `preferUnreliableSend`를 토글해 번갈아 꺼낸다. send thread는 깨어날 때 선택된 큐를 모두 drain한다.

---

## 12. 재전송 스레드 — `RunRetransmissionThread`

```cpp
void RunRetransmissionThread()
{
    TickSet tickSet;

    while (!threadStopFlag) {
        tickSet.UpdateTick();

        {
            std::scoped_lock lock(sendPacketInfoMapLock);

            for (auto& [seq, info] : sendPacketInfoMap) {
                if (info->retransmissionTimeStamp > tickSet.nowTick) continue;

                // 재전송 횟수 초과 → 연결 종료
                if (++info->retransmissionCount >= maxPacketRetransmissionCount) {
                    LOG_ERROR(std::format(
                        "Max retransmission exceeded. SessionId={}. Stopping.", sessionId));
                    isConnected = false;
                    threadStopFlag = true;
                    SetEvent(sendEventHandles[1]);  // sendThread 종료
                    return;
                }

                // 재전송: 이미 암호화된 버퍼 그대로 재사용
                sendBufferQueue.Enqueue(info->buffer);
                ReleaseSemaphore(sendEventHandles[0], 1, nullptr);

                // 다음 재전송 시각 갱신
                info->retransmissionTimeStamp = GetTickCount64() + retransmissionMs;
            }
        }

        SleepRemainingFrameTime(tickSet, retransmissionMs);
    }
}
```

**서버 재전송 스레드와의 차이점:**

| 속성 | 서버 | 클라이언트 |
|------|------|-----------|
| sendPacketInfoMap 보호 | `shared_mutex` (복수 스레드 접근) | `mutex` (단일 스레드 주로 접근) |
| 재전송 시 행동 | `core.SendPacket(info)` | `sendBufferQueue.Enqueue` |
| 횟수 초과 시 | `session->DoDisconnect()` | `isConnected=false` + `threadStopFlag=true` |
| RefCount 패턴 | 복잡한 다중 참조자 | 단순 (소유자 1명) |
## 13. 흐름 제어 — `TryFlushPendingQueue`

```cpp
void TryFlushPendingQueue()
{
    BYTE window = remoteAdvertisedWindow.load(std::memory_order_acquire);
    if (window == 0) return;

    std::scoped_lock pendingLock(pendingPacketQueueLock);

    while (!pendingPacketQueue.empty()) {
        {
            std::scoped_lock infoLock(sendPacketInfoMapLock);
            // outstanding >= window → 전송 불가
            if (sendPacketInfoMap.size() >= static_cast<size_t>(window)) break;
        }

        auto [seq, buf] = pendingPacketQueue.top();
        pendingPacketQueue.pop();

        // 재전송 등록 + 송신 큐
        RegisterSendPacketInfo(*buf, seq);
    }
}
```

**`pendingPacketQueue`는 `priority_queue<tuple<seq, buf>>`** — 시퀀스 오름차순.  
낮은 시퀀스부터 전송해야 서버의 순서 보장 로직이 정상 작동한다.

---

## 14. 옵션 파일 설정값

### clientCoreOptionFile (`ClientOptionFile/CoreOption.txt`)

```ini
:CORE
{
    UNRELIABLE_QUEUE_CAPACITY = 64
    MAX_PACKET_RETRANSMISSION_COUNT = 16
    RETRANSMISSION_MS = 50
    SERVER_ALIVE_CHECK_MS = 15000
}
```

### sessionGetterOptionFilePath (`ClientOptionFile/SessionGetterOption.txt`)

```ini
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

위 값은 현재 샘플 옵션 파일의 기본값이다. `PACKET_CODE`와 `PACKET_KEY`는 서버 설정과 동일해야 한다.

`UNRELIABLE_QUEUE_CAPACITY`는 송신 deque와 수신 deque 각각의 상한으로 사용한다. 생략 시 `64`, 허용 범위는 `1..65535`다.

---

## 15. 주요 멤버 변수

| 변수 | 타입 | 설명 |
|------|------|------|
| `sessionId` | `SessionIdType` | 서버 발급 세션 ID |
| `sessionKey[16]` | `unsigned char[]` | AES 암호화 키 |
| `sessionSalt[16]` | `unsigned char[]` | Nonce 생성용 솔트 |
| `sessionKeyHandle` | `BCRYPT_KEY_HANDLE` | BCrypt 키 핸들 |
| `keyObjectBuffer` | `unsigned char*` | BCrypt 키 오브젝트 버퍼 |
| `rudpSocket` | `SOCKET` | UDP 소켓 |
| `serverAddr` | `sockaddr_in` | 서버 UDP 주소 |
| `authenticatedReceiveCount` | `atomic<uint64_t>` | 인증에 성공한 서버 패킷 누계. 생존 검사 기준 |
| `lastSendPacketSequence` | `PacketSequence` | 마지막 전송 시퀀스 (atomic) |
| `nextRecvPacketSequence` | `PacketSequence` | 다음 기대 수신 시퀀스 |
| `lastAckedSequence` | `atomic<PacketSequence>` | 서버로부터 마지막 ACK |
| `remoteAdvertisedWindow` | `atomic<BYTE>` | 서버 수신 윈도우 크기 |
| `isConnected` | `bool` | sequence=0 ACK 수신 후 true |
| `threadStopFlag` | `bool` | 전체 스레드 종료 플래그 |
| `sendPacketInfoMap` | `map<seq, info*>` | 재전송 추적 맵 |
| `sendPacketInfoMapLock` | `mutex` | sendPacketInfoMap 보호 |
| `pendingPacketQueue` | `priority_queue<seq, buf>` | 흐름 제어 보류 큐 |
| `pendingPacketQueueLock` | `mutex` | pendingPacketQueue 보호 |
| `recvPacketHoldingQueue` | `priority_queue<seq, type, buf>` | 순서 보장 홀딩 큐 |
| `recvPacketHoldingQueueLock` | `mutex` | recvPacketHoldingQueue 보호 |
| `sendBufferQueue` | `CListBaseQueue<NetBuffer*>` | sendThread에 전달할 버퍼 큐 |
| `unreliableSendQueue` | `deque<NetBuffer*>` | 최신 항목 교체가 가능한 신뢰성 없는 송신 큐 |
| `unreliableReceivedPackets` | `deque<NetBuffer*>` | 최신 시퀀스를 통과한 신뢰성 없는 수신 큐 |
| `unreliableReceiveState` | `LatestPacketSequence` | 최초 임의 번호 및 이후 단조 증가 검사 |
| `sendEventHandles[2]` | `HANDLE[]` | [0]=Semaphore, [1]=종료이벤트 |
| `serverAliveChecker` | `ServerAliveChecker` | 서버 생존 감시 |

---

## 16. 스레드 구조 요약

```
[메인 스레드]
  Start() → Stop()

[recvThread]
  recvfrom() 블로킹
  → ProcessRecvPacket()
  → recvPacketHoldingQueue.push
  → unreliableReceivedPackets.push
  → OnSendReply() → TryFlushPendingQueue

[sendThread]
  WaitForMultipleObjects([Semaphore, StopEvent])
  → 신뢰 queue / 신뢰성 없는 deque 교대 선택
  → sendto()

[retransmissionThread]
  sleep(retransmissionMs)
  → sendPacketInfoMap 순회
  → 타임아웃 시 sendBufferQueue.Enqueue + Semaphore.Release
  → 횟수 초과 시 threadStopFlag=true

[serverAliveCheckThread] (isConnected=true 이후 시작)
  sleep(serverAliveCheckMs)
  → authenticatedReceiveCount 변화 없으면 Stop() 호출

[콘텐츠 스레드 / BotTester]
  GetReceivedPacket() 폴링
  GetReceivedUnreliablePacket() 폴링
  SendPacket(packet)
  SendUnreliablePacket(packet)
```

---

## 17. 주의사항

```
□ GetReceivedPacket()이 반환한 NetBuffer는 반드시 NetBuffer::Free() 호출
□ GetReceivedUnreliablePacket()이 반환한 NetBuffer도 반드시 NetBuffer::Free() 호출
□ Stop() 후에는 SendPacket() 호출 금지 (threadStopFlag=true)
□ SESSION_BROKER.IP가 클라이언트에서 도달 가능한 주소인지 확인
□ PACKET_CODE / PACKET_KEY 서버-클라이언트 일치 확인
□ SERVER_ALIVE_CHECK_MS > 서버 HEARTBEAT_THREAD_SLEEP_MS × 2 이상
□ 재전송 횟수 초과로 Stop()이 호출되면 다시 Start() 호출 가능
```

---

## 관련 문서
- [[ServerAliveChecker]] — 서버 생존 감시 상세
- [[Client/RUDPClientCoreHooks]] — 기본 hook과 테스트 override 차이
- [[TLSHelper]] — TLS 세션 수신 채널
- [[CryptoSystem]] — AES-GCM 암호화
- [[Common/PacketFormat]] — 서버와 클라이언트가 공유하는 패킷 구조
- [[FlowController]] — advertiseWindow 기반 흐름 제어 이론
- [[Server/RUDPSessionBroker]] — 서버 측 세션 발급
- [[Troubleshooting]] — 연결 오류 해결
- [[UnreliableChannel]] — 신뢰성 없는 채널의 보장 범위와 큐 정책
---

## 현재 코드 기준 함수 설명

### 공개 함수

#### `bool IsStopped() const`
- 클라이언트 코어가 완전히 중지되었는지 반환한다.

#### `bool IsConnected() const`
- UDP RUDP 연결이 `CONNECT` ACK까지 끝난 상태인지 반환한다.

#### `unsigned int GetRemainPacketSize()`
- 애플리케이션이 아직 꺼내지 않은 수신 패킷 수를 반환한다.

#### `NetBuffer* GetReceivedPacket()`
- 정렬과 복호화가 끝난 패킷 하나를 반환한다.
- 반환된 `NetBuffer`는 호출 측에서 해제해야 한다.

#### `NetBuffer* GetReceivedUnreliablePacket()`
- 최신 시퀀스 검사를 통과한 신뢰성 없는 패킷을 반환한다.
- 반환된 `NetBuffer`는 호출 측에서 해제해야 한다.

#### `void SendPacket(IPacket& packet)`
- 일반 콘텐츠 패킷 송신 진입점이다.
- 내부에서 시퀀스 부여, 암호화, 재전송 추적 등록까지 이어진다.

#### `bool SendUnreliablePacket(IPacket& packet)`
- 최신성 우선 채널 송신 진입점이다.
- 로컬 bounded queue 수락 여부를 반환하며 ACK와 재전송을 제공하지 않는다.

#### `void Disconnect()`
- 연결 해제 코어 패킷을 보낸 뒤 종료 흐름으로 들어간다.

### 내부 핵심 함수

#### `bool Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath, bool printLogToConsole)`
- 옵션 로드, Logger 시작, SessionBroker TLS 연결, UDP 소켓 생성, 송수신 스레드 시작, CONNECT 패킷 송신까지 초기화를 담당한다.

#### `void Stop()`
- SessionBroker 소켓, RUDP 소켓, 송수신 스레드, 재전송 큐, 보류 큐, 암호화 핸들을 정리한다.

#### `void JoinThreads()`
- `recvThread`, `sendThread`, `retransmissionThread`, `serverAliveChecker` 종료를 조율한다.

#### `bool CreateRUDPSocket()`
- 클라이언트 UDP 소켓을 생성하고 `serverAddr`를 준비한다.

#### `void SendConnectPacket()`
- `LOGIN_PACKET_SEQUENCE`를 사용하는 코어 CONNECT 패킷을 전송한다.

#### `bool RunGetSessionFromServer(const std::wstring& optionFilePath)`
#### `bool GetSessionFromServer()`
#### `bool TryConnectToSessionBroker() const`
#### `bool TrySetTargetSessionInfo()`
#### `bool SetTargetSessionInfo(NetBuffer& receivedBuffer)`
- SessionBroker TLS 연결, 세션 정보 수신, 세션 키와 포트 파싱을 담당한다.

#### `bool RunThreads()`
#### `void RunRecvThread()`
#### `void RunSendThread()`
#### `void RunRetransmissionThread()`
- `RunThreads()`는 recv/send/retransmission 스레드와 송신 이벤트 초기화 성공 여부를 반환한다.
- 나머지 함수는 각 스레드의 실행 진입점이다.

#### `void OnRecvStream(NetBuffer& recvBuffer, int recvSize)`
#### `void ProcessRecvPacket(NetBuffer& receivedBuffer)`
#### `void OnSendReply(NetBuffer& recvPacket, PacketSequence packetSequence)`
### `SendReplyToServer`

```cpp
void SendReplyToServer(PacketSequence inRecvPacketSequence, PACKET_TYPE packetType = PACKET_TYPE::SEND_REPLY_TYPE);
```

수신 패킷 분해, ACK 처리, 서버 reply 전송을 담당한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `inRecvPacketSequence` | `PacketSequence` | 수신한 패킷의 시퀀스 |
| `packetType` | `PACKET_TYPE` | 전송할 패킷의 타입. 기본값은 `PACKET_TYPE::SEND_REPLY_TYPE` |
#### `void DoSend()`
#### `void SendPacket(NetBuffer& buffer, PacketSequence inSendPacketSequence, bool isCorePacket)`
#### `void SendPacket(const SendPacketInfo& sendPacketInfo)`
#### `void RegisterSendPacketInfo(NetBuffer& buffer, PacketSequence inSendPacketSequence)`
#### `void TryFlushPendingQueue()`
- 실제 UDP 송신, 재전송 추적, 흐름 제어 해소 후 보류 큐 재송신을 담당한다.

#### `static inline WORD GetPayloadLength(const NetBuffer& buffer)`
- 패킷 헤더에서 payload 길이를 읽는다.

#### `bool ReadOptionFile(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath)`
#### `bool ReadClientCoreOptionFile(const std::wstring& optionFilePath)`
#### `bool ReadSessionGetterOptionFile(const std::wstring& optionFilePath)`
- 클라이언트 코어와 SessionBroker 접속 설정을 읽는다.

### SessionGetter 분기

`USE_IOCP_SESSION_GETTER` 빌드에서는 중첩 `SessionGetter` 클래스가 TLS 세션 조회를 담당한다.

#### `bool SessionGetter::Start(const std::wstring& optionFilePath)`
#### `void OnConnectionComplete()`
#### `void OnRecv(CNetServerSerializationBuf* recvBuffer)`
#### `void OnSend(int sendsize)`
#### `void OnWorkerThreadBegin()`
#### `void OnWorkerThreadEnd()`
#### `void OnError(st_Error* error)`
- IOCP 기반 SessionBroker 조회 수명주기 훅이다.
