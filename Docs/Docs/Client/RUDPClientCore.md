# RUDPClientCore

> **C++ 클라이언트 측 RUDP 코어.**  
> TLS로 세션 정보를 수신하고, UDP 소켓을 생성해 서버에 연결한다.  
> 재전송, 순서 보장, 서버 생존 확인, 흐름 제어를 클라이언트 관점에서 처리한다.

---

## 목차

1. [전체 연결 흐름](#1-전체-연결-흐름)
2. [시작 — Start](#2-시작--start)
3. [종료 — Stop](#3-종료--stop)
4. [TLS 세션 정보 수신](#4-tls-세션-정보-수신)
5. [CONNECT 패킷 전송](#5-connect-패킷-전송)
6. [데이터 송신 — SendPacket](#6-데이터-송신--sendpacket)
7. [데이터 수신 — GetReceivedPacket](#7-데이터-수신--getreceivedpacket)
8. [수신 스레드 — recvThread](#8-수신-스레드--recvthread)
9. [수신 처리 — ProcessRecvPacket](#9-수신-처리--processrecvpacket)
10. [ACK 수신 — OnSendReply](#10-ack-수신--onsendreply)
11. [송신 스레드 — sendThread](#11-송신-스레드--sendthread)
12. [재전송 스레드 — RunRetransmissionThread](#12-재전송-스레드--runretransmissionthread)
13. [흐름 제어 — TryFlushPendingQueue](#13-흐름-제어--tryflushpendingqueue)
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
 │       ◄─────── {serverIp, port,       │
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
   → sessionBrokerIp, sessionBrokerPort
   → NetBuffer::m_byHeaderCode, m_byXORCode

3. WSAStartup(MAKEWORD(2,2))

4. RunGetSessionFromServer(sessionGetterOptionFilePath)
   → TLS TCP 연결 → 세션 정보 수신 → 파싱 → BCrypt 키 핸들 생성
   (실패 → return false)

5. CreateRUDPSocket()
   → socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
   → bind(INADDR_ANY, port=0)
   → serverAddr 초기화 {serverIp, serverPort}

6. CreateEventHandles()
   → sendEventHandles[0] = CreateSemaphore(0, LONG_MAX)  ← 패킷 대기
   → sendEventHandles[1] = CreateEvent(manual, FALSE)     ← 종료 신호

7. 스레드 시작
   → recvThread = thread([this] { RunRecvThread(); })
   → sendThread = thread([this] { RunSendThread(); })
   → retransmissionThread = thread([this] { RunRetransmissionThread(); })

8. SendConnectPacketToServer()
   → CONNECT_TYPE | seq=0 | sessionId
   → EncodePacket(CLIENT_TO_SERVER, isCorePacket=true)
   → sendto(rudpSocket, buffer, ..., &serverAddr)

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
  → AcquireCredentialsHandle(SECPKG_CRED_OUTBOUND, SP_PROT_TLS1_2_CLIENT,
                              SCH_CRED_MANUAL_CRED_VALIDATION)

TLSHelperClient::Handshake(sessionBrokerSocket)
  → InitializeSecurityContext 루프
  → ClientHello / ServerHello / Certificate / Finished 교환

TrySetTargetSessionInfo(sessionBrokerSocket, tlsHelper)
  → recv 루프 + DecryptDataStream
  → SetTargetSessionInfo(plainBuffer, plainSize)
```

### `TrySetTargetSessionInfo` — TLS 스트림 수신 루프

```cpp
bool TrySetTargetSessionInfo(SOCKET socket, TLSHelperClient& tls)
{
    std::vector<unsigned char> encryptedStream;
    char plainBuffer[MAX_TLS_PACKET_SIZE];
    size_t totalPlainReceived = 0;
    WORD payloadLength = 0;
    bool payloadComplete = false;

    while (true) {
        char tlsRecvBuf[MAX_TLS_PACKET_SIZE];
        int bytes = recv(socket, tlsRecvBuf, MAX_TLS_PACKET_SIZE, 0);
        if (bytes <= 0) break;

        // 암호화 스트림에 누적
        encryptedStream.insert(encryptedStream.end(),
                               tlsRecvBuf, tlsRecvBuf + bytes);

        size_t plainSize = 0;
        auto result = tls.DecryptDataStream(encryptedStream, plainBuffer, plainSize);

        switch (result) {
        case TlsDecryptResult::NEED_MORE_DATA:
            continue;   // TLS 레코드 불완전, recv 더 필요

        case TlsDecryptResult::Error:
            return false;

        case TlsDecryptResult::CloseNotify:
            // 서버가 close_notify 전송 → 세션 정보 전송 완료
            if (payloadComplete) return true;
            return false;   // 데이터 덜 받았는데 닫힘

        case TlsDecryptResult::OK:
            // 첫 수신 시 헤더 파싱
            if (totalPlainReceived == 0 && plainSize >= df_HEADER_SIZE) {
                WORD payLen;
                memcpy(&payLen, &plainBuffer[1], sizeof(WORD));
                payloadLength = payLen;
            }

            totalPlainReceived += plainSize;

            if (totalPlainReceived >= payloadLength + df_HEADER_SIZE) {
                payloadComplete = true;
                // 세션 정보 파싱
                if (!SetTargetSessionInfo(plainBuffer, totalPlainReceived))
                    return false;
            }
            break;
        }
    }

    return payloadComplete;
}
```

### `SetTargetSessionInfo` — 세션 정보 파싱

```cpp
bool SetTargetSessionInfo(const char* buffer, size_t size)
{
    NetBuffer netBuf;
    memcpy(netBuf.m_pSerializeBuffer, buffer, size);
    netBuf.m_iWrite = static_cast<WORD>(size);
    netBuf.m_iRead  = df_HEADER_SIZE;  // 헤더 스킵

    CONNECT_RESULT_CODE resultCode;
    netBuf >> resultCode;

    if (resultCode != CONNECT_RESULT_CODE::SUCCESS) {
        LOG_ERROR(std::format("Session broker returned error: {}", static_cast<int>(resultCode)));
        return false;
    }

    // 서버 UDP 주소
    std::string serverIpStr;
    WORD serverUdpPort;
    netBuf >> serverIpStr >> serverUdpPort;

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(serverUdpPort);
    inet_pton(AF_INET, serverIpStr.c_str(), &serverAddr.sin_addr);

    // 세션 식별 정보
    netBuf >> sessionId;

    // 암호화 키/솔트 (raw 바이트)
    netBuf.GetBuffer(sessionKey, SESSION_KEY_SIZE);
    netBuf.GetBuffer(sessionSalt, SESSION_SALT_SIZE);

    // BCrypt 키 핸들 생성
    auto& crypto = CryptoHelper::GetTLSInstance();
    ULONG keyObjSize = crypto.GetKeyObjectSize();
    keyObjectBuffer = new unsigned char[keyObjSize];

    sessionKeyHandle = crypto.GetSymmetricKeyHandle(keyObjectBuffer, sessionKey);
    if (sessionKeyHandle == nullptr) {
        LOG_ERROR("GetSymmetricKeyHandle failed");
        return false;
    }

    LOG_DEBUG(std::format("Session info received. SessionId={}, Port={}",
        sessionId, serverUdpPort));
    return true;
}
```

**수신되는 세션 정보 페이로드 구조:**

```
[HeaderCode 1B][PayloadLen 2B]
[CONNECT_RESULT_CODE 4B]
[serverIp string (len 2B + bytes)]
[serverUdpPort 2B]
[sessionId 2B]
[sessionKey 16B]
[sessionSalt 16B]
```

---

## 5. CONNECT 패킷 전송

```cpp
void SendConnectPacketToServer()
{
    NetBuffer* buf = NetBuffer::Alloc();

    auto type = PACKET_TYPE::CONNECT_TYPE;
    PacketSequence seq = LOGIN_PACKET_SEQUENCE;  // = 0

    *buf << type << seq << sessionId;
    // Total payload: Type(1) + Seq(8) + SessionId(2) = 11 bytes

    // AES-GCM 암호화 (isCorePacket=true, direction=CLIENT_TO_SERVER)
    PacketCryptoHelper::EncodePacket(
        *buf, seq,
        CryptoHelper::PACKET_DIRECTION::CLIENT_TO_SERVER,
        sessionSalt, SESSION_SALT_SIZE,
        sessionKeyHandle,
        true  // isCorePacket
    );

    // 직접 sendto (send 스레드 우회)
    sendto(rudpSocket,
           buf->m_pSerializeBuffer,
           buf->m_iWriteLast,
           0,
           reinterpret_cast<sockaddr*>(&serverAddr),
           sizeof(serverAddr));

    NetBuffer::Free(buf);
}
```

> **sendto를 직접 호출하는 이유**: CONNECT 패킷은 send 큐에 넣지 않는다.  
> send 스레드가 아직 완전히 준비되기 전에 전송해야 하며,  
> 재전송 추적도 불필요(서버가 sequence=0 ACK로 응답하면 확인됨).

---

## 6. 데이터 송신 — `SendPacket`

### 외부 API

```cpp
void RUDPClientCore::SendPacket(OUT IPacket& packet)
```

**콘텐츠 레이어에서 사용하는 주 API.** (BotTester ActionNode 등에서 호출)

```cpp
{
    // ① 시퀀스 번호 증가
    PacketSequence seq = ++lastSendPacketSequence;

    // ② 직렬화
    NetBuffer* buf = NetBuffer::Alloc();
    auto type = PACKET_TYPE::SEND_TYPE;
    *buf << type << seq << packet.GetPacketId();
    packet.PacketToBuffer(*buf);

    // ③ 내부 전송 경로
    SendPacket(buf, seq, /*isCorePacket=*/false);
}
```

### 내부 `SendPacket(NetBuffer*, PacketSequence, bool)`

```cpp
void SendPacket(NetBuffer* buf, PacketSequence seq, bool isCorePacket)
{
    // ① AES-GCM 암호화
    PacketCryptoHelper::EncodePacket(
        *buf, seq,
        CryptoHelper::PACKET_DIRECTION::CLIENT_TO_SERVER,
        sessionSalt, SESSION_SALT_SIZE,
        sessionKeyHandle, isCorePacket
    );

    // ② 흐름 제어 확인
    {
        std::scoped_lock lock(pendingQueueLock);
        bool overflow = !pendingPacketQueue.empty()
                     || sendPacketInfoMap.size() >= remoteAdvertisedWindow.load();

        if (overflow) {
            // 보류 큐에 보관
            pendingPacketQueue.emplace(seq, buf);
            return;
        }
    }

    // ③ 재전송 등록 + 송신 큐에 삽입
    RegisterSendPacketInfo(buf, seq);
}
```

### `RegisterSendPacketInfo`

```cpp
void RegisterSendPacketInfo(NetBuffer* buf, PacketSequence seq)
{
    // ① SendPacketInfo 할당
    auto* info = sendPacketInfoPool->Alloc();
    info->buffer = buf;
    info->sequence = seq;
    info->retransmissionCount = 0;
    info->retransmissionTimeStamp = GetTickCount64() + retransmissionMs;

    // ② 재전송 맵에 등록
    {
        std::scoped_lock lock(sendPacketInfoLock);
        sendPacketInfoMap[seq] = info;
    }

    // ③ send 큐에 삽입
    sendBufferQueue.Enqueue(buf);

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
    while (true) {
        if (recvPacketHoldingQueue.empty()) return nullptr;

        auto& [topSeq, topType, topBuf] = recvPacketHoldingQueue.top();

        // ① 과거 시퀀스 (중복) → 폐기
        if (topSeq < nextRecvPacketSequence) {
            recvPacketHoldingQueue.pop();
            NetBuffer::Free(topBuf);
            continue;
        }

        // ② 순서가 맞지 않음 → 아직 앞 패킷이 안 왔음
        if (topSeq != nextRecvPacketSequence) return nullptr;

        // ③ HEARTBEAT_TYPE → 응답만 보내고 버림
        if (topType == PACKET_TYPE::HEARTBEAT_TYPE) {
            SendHeartbeatReply(topSeq);
            recvPacketHoldingQueue.pop();
            NetBuffer::Free(topBuf);
            ++nextRecvPacketSequence;
            continue;
        }

        // ④ 정상 데이터 패킷 → 반환
        recvPacketHoldingQueue.pop();
        ++nextRecvPacketSequence;
        return topBuf;  // 호출자가 NetBuffer::Free() 책임
    }
}
```

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
    // ① 헤더 유효성 검사
    if (recvBuffer.GetUseSize() < df_HEADER_SIZE + 1) {
        NetBuffer::Free(&recvBuffer);
        return;
    }

    // ② HeaderCode 확인
    if (recvBuffer.m_pSerializeBuffer[0] != NetBuffer::m_byHeaderCode) {
        LOG_ERROR("Invalid header code");
        NetBuffer::Free(&recvBuffer);
        return;
    }

    // ③ PacketType 추출 (m_iRead = df_HEADER_SIZE로 건너뜀)
    recvBuffer.m_iRead = df_HEADER_SIZE;
    BYTE packetType;
    recvBuffer >> packetType;

    switch (static_cast<PACKET_TYPE>(packetType)) {

    case PACKET_TYPE::SEND_TYPE:
    {
        // 복호화 (SERVER_TO_CLIENT, isCorePacket=false)
        if (!PacketCryptoHelper::DecodePacket(
                recvBuffer, sessionSalt, SESSION_SALT_SIZE,
                sessionKeyHandle, false,
                CryptoHelper::PACKET_DIRECTION::SERVER_TO_CLIENT)) {
            LOG_ERROR("DecodePacket failed (SEND_TYPE)");
            break;
        }

        PacketSequence seq;
        memcpy(&seq,
               recvBuffer.m_pSerializeBuffer + df_HEADER_SIZE + 1,
               sizeof(PacketSequence));

        // 홀딩 큐에 보관 (순서 보장은 GetReceivedPacket에서)
        recvPacketHoldingQueueLock.lock();
        recvPacketHoldingQueue.emplace(seq, PACKET_TYPE::SEND_TYPE, &recvBuffer);
        recvPacketHoldingQueueLock.unlock();

        SendReplyToServer(seq);   // ACK 전송
        return;  // Free 안 함 (GetReceivedPacket에서 Free)
    }

    case PACKET_TYPE::HEARTBEAT_TYPE:
    {
        if (!PacketCryptoHelper::DecodePacket(
                recvBuffer, sessionSalt, SESSION_SALT_SIZE,
                sessionKeyHandle, true,
                CryptoHelper::PACKET_DIRECTION::SERVER_TO_CLIENT)) break;

        PacketSequence seq;
        memcpy(&seq,
               recvBuffer.m_pSerializeBuffer + df_HEADER_SIZE + 1,
               sizeof(PacketSequence));

        // 홀딩 큐에 보관 (GetReceivedPacket에서 헤더비트로 처리 후 버림)
        recvPacketHoldingQueueLock.lock();
        recvPacketHoldingQueue.emplace(seq, PACKET_TYPE::HEARTBEAT_TYPE, &recvBuffer);
        recvPacketHoldingQueueLock.unlock();

        SendReplyToServer(seq);
        return;
    }

    case PACKET_TYPE::SEND_REPLY_TYPE:
    {
        if (!PacketCryptoHelper::DecodePacket(
                recvBuffer, sessionSalt, SESSION_SALT_SIZE,
                sessionKeyHandle, true,
                CryptoHelper::PACKET_DIRECTION::SERVER_TO_CLIENT_REPLY)) break;

        OnSendReply(recvBuffer);
        break;
    }

    default:
        LOG_ERROR(std::format("Unknown packet type: {}", packetType));
        break;
    }

    NetBuffer::Free(&recvBuffer);
}
```

### `SendReplyToServer` — ACK 전송

```cpp
void SendReplyToServer(PacketSequence ackedSeq)
{
    NetBuffer* buf = NetBuffer::Alloc();
    auto type = PACKET_TYPE::SEND_REPLY_TYPE;
    *buf << type << ackedSeq;

    PacketCryptoHelper::EncodePacket(
        *buf, ackedSeq,
        CryptoHelper::PACKET_DIRECTION::CLIENT_TO_SERVER_REPLY,
        sessionSalt, SESSION_SALT_SIZE,
        sessionKeyHandle, true
    );

    // ACK는 send 큐 우회, 직접 sendto
    sendto(rudpSocket,
           buf->m_pSerializeBuffer,
           buf->m_iWriteLast,
           0,
           reinterpret_cast<sockaddr*>(&serverAddr),
           sizeof(serverAddr));

    NetBuffer::Free(buf);
}
```

> ACK는 PendingQueue나 SendPacketInfo 등록 없이 직접 전송한다.  
> 서버와 같은 논리: ACK는 손실 시 원본 패킷 재전송으로 자연스럽게 재요청됨.

---

## 10. ACK 수신 — `OnSendReply`

```cpp
void OnSendReply(NetBuffer& recvBuffer)
{
    // ① 시퀀스 + advertiseWindow 추출
    PacketSequence ackedSeq;
    BYTE remoteWindow;
    recvBuffer >> ackedSeq >> remoteWindow;

    // ② advertiseWindow 갱신 (원자적)
    remoteAdvertisedWindow.store(remoteWindow, std::memory_order_release);

    // ③ lastAckedSequence 갱신
    lastAckedSequence.store(ackedSeq, std::memory_order_release);

    // ④ sequence=0 첫 ACK → isConnected 활성화
    if (ackedSeq == LOGIN_PACKET_SEQUENCE && !isConnected) {
        isConnected = true;
        LOG_DEBUG(std::format("Connected. SessionId={}", sessionId));
        serverAliveChecker.StartServerAliveCheck(serverAliveCheckMs);
    }

    // ⑤ 재전송 맵에서 제거
    {
        std::scoped_lock lock(sendPacketInfoLock);
        auto it = sendPacketInfoMap.find(ackedSeq);
        if (it != sendPacketInfoMap.end()) {
            NetBuffer::Free(it->second->buffer);
            sendPacketInfoPool->Free(it->second);
            sendPacketInfoMap.erase(it);
        }
    }

    // ⑥ 보류 큐 처리
    TryFlushPendingQueue();
}
```

---

## 11. 송신 스레드 — `sendThread`

```cpp
void RunSendThread()
{
    while (!threadStopFlag) {
        // 두 핸들 동시 대기:
        //   [0]: Semaphore (패킷 있을 때 Release(1))
        //   [1]: ManualResetEvent (종료 신호)
        DWORD result = WaitForMultipleObjects(
            2, sendEventHandles, FALSE, INFINITE);

        if (result == WAIT_OBJECT_0 + 1) break;  // 종료
        if (result != WAIT_OBJECT_0) continue;

        NetBuffer* buf = nullptr;
        if (!sendBufferQueue.Dequeue(&buf)) continue;
        if (buf == nullptr) continue;

        int sent = sendto(
            rudpSocket,
            buf->m_pSerializeBuffer,
            buf->m_iWriteLast,
            0,
            reinterpret_cast<sockaddr*>(&serverAddr),
            sizeof(serverAddr)
        );

        if (sent == SOCKET_ERROR) {
            LOG_ERROR(std::format("sendto failed: {}", WSAGetLastError()));
            // 패킷은 재전송 맵에 있으므로 재전송 스레드가 재전송
        }
    }
}
```

**Semaphore 방식 이유:**  
`ManualResetEvent`는 단일 신호만 기억하므로, 짧은 시간에 패킷이 여러 개 쌓이면  
일부를 처리하지 못할 수 있다. `Semaphore`는 `Release` 횟수를 누적한다.

---

## 12. 재전송 스레드 — `RunRetransmissionThread`

```cpp
void RunRetransmissionThread()
{
    TickSet tickSet;

    while (!threadStopFlag) {
        tickSet.UpdateTick();

        {
            std::scoped_lock lock(sendPacketInfoLock);

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

---

## 13. 흐름 제어 — `TryFlushPendingQueue`

```cpp
void TryFlushPendingQueue()
{
    BYTE window = remoteAdvertisedWindow.load(std::memory_order_acquire);
    if (window == 0) return;

    std::scoped_lock pendingLock(pendingQueueLock);
    std::scoped_lock infoLock(sendPacketInfoLock);

    while (!pendingPacketQueue.empty()) {
        // outstanding >= window → 전송 불가
        if (sendPacketInfoMap.size() >= static_cast<size_t>(window)) break;

        auto [seq, buf] = pendingPacketQueue.top();
        pendingPacketQueue.pop();

        // 재전송 등록 + 송신 큐
        auto* info = sendPacketInfoPool->Alloc();
        info->buffer = buf;
        info->sequence = seq;
        info->retransmissionCount = 0;
        info->retransmissionTimeStamp = GetTickCount64() + retransmissionMs;

        sendPacketInfoMap[seq] = info;
        sendBufferQueue.Enqueue(buf);
        ReleaseSemaphore(sendEventHandles[0], 1, nullptr);
    }
}
```

**`pendingPacketQueue`는 `priority_queue<tuple<seq, buf>>`** — 시퀀스 오름차순.  
낮은 시퀀스부터 전송해야 서버의 순서 보장 로직이 정상 작동한다.

---

## 14. 옵션 파일 설정값

### clientCoreOptionFile (CoreOption.ini)

```ini
[CORE]
MAX_PACKET_RETRANSMISSION_COUNT=15   ; 재전송 한계 초과 시 클라이언트 종료
RETRANSMISSION_MS=200                ; 재전송 타임아웃 (ms)
SERVER_ALIVE_CHECK_MS=5000           ; 서버 생존 확인 주기 (ms)
```

### sessionGetterOptionFilePath (SessionGetterOption.ini)

```ini
[SESSION_BROKER]
IP=192.168.1.100       ; SessionBroker 서버 IP
PORT=10001             ; SessionBroker 포트 (TCP)

[SERIALIZEBUF]
PACKET_CODE=0x89       ; 헤더 코드 (서버와 반드시 동일)
PACKET_KEY=0x99        ; XOR 키 (서버와 반드시 동일)
```

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
| `lastSendPacketSequence` | `PacketSequence` | 마지막 전송 시퀀스 (atomic) |
| `nextRecvPacketSequence` | `PacketSequence` | 다음 기대 수신 시퀀스 |
| `lastAckedSequence` | `atomic<PacketSequence>` | 서버로부터 마지막 ACK |
| `remoteAdvertisedWindow` | `atomic<BYTE>` | 서버 수신 윈도우 크기 |
| `isConnected` | `bool` | sequence=0 ACK 수신 후 true |
| `threadStopFlag` | `bool` | 전체 스레드 종료 플래그 |
| `sendPacketInfoMap` | `unordered_map<seq, info*>` | 재전송 추적 맵 |
| `sendPacketInfoLock` | `mutex` | sendPacketInfoMap 보호 |
| `pendingPacketQueue` | `priority_queue<seq, buf>` | 흐름 제어 보류 큐 |
| `pendingQueueLock` | `mutex` | pendingPacketQueue 보호 |
| `recvPacketHoldingQueue` | `priority_queue<seq, type, buf>` | 순서 보장 홀딩 큐 |
| `recvPacketHoldingQueueLock` | `mutex` | recvPacketHoldingQueue 보호 |
| `sendBufferQueue` | `CListBaseQueue<NetBuffer*>` | sendThread에 전달할 버퍼 큐 |
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
  → OnSendReply() → TryFlushPendingQueue

[sendThread]
  WaitForMultipleObjects([Semaphore, StopEvent])
  → sendBufferQueue.Dequeue()
  → sendto()

[retransmissionThread]
  sleep(retransmissionMs)
  → sendPacketInfoMap 순회
  → 타임아웃 시 sendBufferQueue.Enqueue + Semaphore.Release
  → 횟수 초과 시 threadStopFlag=true

[serverAliveCheckThread] (isConnected=true 이후 시작)
  sleep(serverAliveCheckMs)
  → nextRecvPacketSequence 변화 없으면 Stop() 호출

[콘텐츠 스레드 / BotTester]
  GetReceivedPacket() 폴링
  SendPacket(packet)
```

---

## 17. 주의사항

```
□ GetReceivedPacket()이 반환한 NetBuffer는 반드시 NetBuffer::Free() 호출
□ Stop() 후에는 SendPacket() 호출 금지 (threadStopFlag=true)
□ SESSION_BROKER.IP가 클라이언트에서 도달 가능한 주소인지 확인
□ PACKET_CODE / PACKET_KEY 서버-클라이언트 일치 확인
□ SERVER_ALIVE_CHECK_MS > 서버 HEARTBEAT_THREAD_SLEEP_MS × 2 이상
□ 재전송 횟수 초과로 Stop()이 호출되면 다시 Start() 호출 가능
```

---

## 관련 문서
- [[ServerAliveChecker]] — 서버 생존 감시 상세
- [[TLSHelper]] — TLS 세션 수신 채널
- [[CryptoSystem]] — AES-GCM 암호화
- [[PacketFormat]] — 패킷 구조
- [[FlowController]] — advertiseWindow 기반 흐름 제어 이론
- [[RUDPSessionBroker]] — 서버 측 세션 발급
- [[TroubleShooting]] — 연결 오류 해결
