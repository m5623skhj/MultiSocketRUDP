# RUDPSessionBroker

> **TLS 1.2 채널로 클라이언트에게 RUDP 세션 정보를 안전하게 전달하는 세션 발급 서버.**  
> 클라이언트는 TLS TCP 연결 → 세션 정보({IP, Port, SessionId, Key, Salt}) 수신 →  
> 이후 해당 정보로 UDP RUDP 연결을 수립한다.

---

## 목차

1. [역할 정의와 전체 흐름](#1-역할-정의와-전체-흐름)
2. [스레드 구성](#2-스레드-구성)
3. [Start — 브로커 시작](#3-start--브로커-시작)
4. [Stop — 브로커 종료](#4-stop--브로커-종료)
5. [HandleClientConnection — 클라이언트 처리](#5-handleclientconnection--클라이언트-처리)
6. [ReserveSession — 세션 발급 4단계](#6-reservesession--세션-발급-4단계)
7. [InitSessionCrypto — 암호화 키 생성](#7-initsessioncrypto--암호화-키-생성)
8. [SendSessionInfoToClient — TLS 전송](#8-sendsessioninfotoclient--tls-전송)
9. [실패 처리 매트릭스](#9-실패-처리-매트릭스)
10. [예약 세션 타임아웃](#10-예약-세션-타임아웃)
11. [TLS 인증서 설정](#11-tls-인증서-설정)
12. [세션 정보 페이로드 구조](#12-세션-정보-페이로드-구조)

---

## 1. 역할 정의와 전체 흐름

```
[클라이언트]                [RUDPSessionBroker]          [MultiSocketRUDPCore]

TCP connect ──────────────►
                            accept()
                            TLS Handshake ◄──────────────►
                            ─────────────────────────────────► AcquireSession()
                                                                 풀에서 세션 할당
                            ─────────────────────────────────► InitReserveSession()
                                                                 UDP 소켓 생성
                                                                 RIO 초기화
                                                                 DoRecv 등록
                                                                 SetReserved()
                            InitSessionCrypto()
                              BCryptGenRandom → sessionKey (16B)
                              BCryptGenRandom → sessionSalt (16B)
                              BCryptGenerateSymmetricKey → keyHandle

                            SendSessionInfoToClient()
◄──────── {serverIp, port,
            sessionId,
            sessionKey,
            sessionSalt} ──
TLS close_notify ◄──────────
TCP close ──────────────────►

UDP CONNECT ─────────────────────────────────────────────────► RUDPSession (port)
                                                                TryConnect() → CONNECTED
```

---

## 2. 스레드 구성

```
sessionBrokerThread (1개)
  └─ while !stopped:
        clientSocket = accept(listenSocket, &clientAddr, &addrLen)
        if INVALID_SOCKET → break (Stop에서 closesocket했으면 오류 반환)
        clientQueue.push({clientSocket, clientAddr})
        clientQueueCV.notify_one()

threadPool (BROKER_THREAD_POOL_SIZE = 4개)
  └─ while !stopped:
        unique_lock lock(clientQueueMutex)
        clientQueueCV.wait(lock, [this]{ return !clientQueue.empty() || stopped; })
        if stopped → break
        auto [socket, addr] = clientQueue.front(); clientQueue.pop()
        lock.unlock()
        HandleClientConnection(socket, rudpServerIp)
```

**왜 worker 스레드 4개인가:**

TLS 핸드셰이크(수백 ms) + RIO 초기화 + BCryptGenRandom 비용이 크다.  
accept 루프가 블락되지 않고 다음 클라이언트를 즉시 수락하려면  
worker 풀이 필요하다. 4개는 일반적인 동시 접속 버스트를 처리하기에 충분하다.

---

## 3. Start — 브로커 시작

```cpp
bool RUDPSessionBroker::Start(
    unsigned short sessionBrokerPort,
    const std::string& coreServerIp
)
```

```
1. rudpServerIp = coreServerIp  ← 클라이언트에게 알릴 UDP 서버 IP

2. listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
   서버 주소: INADDR_ANY, sessionBrokerPort
   bind(listenSocket, ...)
   listen(listenSocket, SOMAXCONN)

3. tlsHelper.Initialize(storeName, certSubjectName)
   → CertOpenStore + CertFindCertificateInStore
   → AcquireCredentialsHandle(SP_PROT_TLS1_2_SERVER)
   실패 → return false

4. threadPool 시작 (4개 jthread)

5. sessionBrokerThread 시작 (accept 루프)

6. return true
```

---

## 4. Stop — 브로커 종료

```cpp
void RUDPSessionBroker::Stop()
```

```
1. stopped = true

2. closesocket(listenSocket)
   → accept()가 WSAENOTSOCK 에러로 즉시 반환
   → sessionBrokerThread 루프 break

3. clientQueueCV.notify_all()
   → 대기 중인 worker 스레드 깨우기 (stopped=true 확인 → break)

4. jthread 소멸자 (stop_token 신호 + join 자동)
```

---

## 5. HandleClientConnection — 클라이언트 처리

```cpp
void RUDPSessionBroker::HandleClientConnection(
    SOCKET clientSocket,
    const std::string& rudpServerIp
)
```

```
// 이 함수는 threadPool worker 중 하나에서 실행됨

// ① TLSHelperServer 인스턴스 생성 (클라이언트마다 독립)
TLSHelper::TLSHelperServer localTlsHelper;
localTlsHelper.Initialize(storeName, certSubjectName);
// → 같은 인증서를 사용하지만 보안 컨텍스트(CtxtHandle)는 각 연결마다 새로 생성

// ② TLS 핸드셰이크
if (!localTlsHelper.Handshake(clientSocket)) {
    LOG_ERROR("TLS Handshake failed");
    closesocket(clientSocket);
    return;
}

// ③ 세션 정보 버퍼 준비
NetBuffer sendBuffer;

// ④ 세션 발급
auto resultCode = ReserveSession(sendBuffer, rudpServerIp);
// → resultCode가 버퍼에 기록됨 (SUCCESS 또는 오류 코드)

// ⑤ 전송
if (!SendSessionInfoToClient(clientSocket, localTlsHelper, sendBuffer)) {
    LOG_ERROR("SendSessionInfoToClient failed");

    // 전송 실패 시 발급된 세션 회수
    if (resultCode == CONNECT_RESULT_CODE::SUCCESS) {
        // sendBuffer에서 sessionId 추출
        SessionIdType failedSessionId = ExtractSessionIdFromBuffer(sendBuffer);
        sessionDelegate.AbortReservedSession(failedSessionId);
    }
}

closesocket(clientSocket);
```

**왜 `TLSHelperServer` 인스턴스를 per-connection으로 생성하는가:**

`TLSHelperServer`는 내부에 `CtxtHandle`(보안 컨텍스트 핸들)을 보유한다.  
이 핸들은 연결별로 독립적인 세션 키를 관리하므로 공유하면 보안 위반이다.  
싱글 인스턴스를 worker 4개가 공유하면 서로의 TLS 상태를 덮어쓴다.

---

## 6. ReserveSession — 세션 발급 4단계

```cpp
CONNECT_RESULT_CODE RUDPSessionBroker::ReserveSession(
    OUT NetBuffer& sendBuffer,
    const std::string& rudpServerIp
) const
```

### 단계 1: 세션 풀에서 할당

```cpp
RUDPSession* session = MultiSocketRUDPCoreFunctionDelegate::AcquireSession();
// → RUDPSessionManager::unusedSessionIdList.pop_front()
// → sessionList[id] 반환

if (session == nullptr) {
    // 풀 고갈
    sendBuffer << CONNECT_RESULT_CODE::SERVER_FULL;
    return CONNECT_RESULT_CODE::SERVER_FULL;
}

// RAII: 이후 단계 실패 시 세션 자동 회수
auto sessionGuard = ScopeExit([&] {
    if (!success) sessionDelegate.AbortReservedSession(*session);
});
```

### 단계 2: UDP 소켓 + RIO 초기화

```cpp
auto code = MultiSocketRUDPCoreFunctionDelegate::InitReserveSession(*session);

if (code != CONNECT_RESULT_CODE::SUCCESS) {
    sendBuffer << code;  // CREATE_SOCKET_FAILED / RIO_INIT_FAILED / DO_RECV_FAILED
    return code;
}
```

**`InitReserveSession` 내부 (MultiSocketRUDPCore에서):**

```
① CreateRUDPSocket()
   WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, WSA_FLAG_REGISTERED_IO)
   bind(INADDR_ANY, port=0)    ← OS가 임시 포트 자동 할당
   getsockname() → serverPort

② rioManager->InitializeSessionRIO(session, threadId)
   RIORegisterBuffer(recvBuffer, 16KB)
   RIORegisterBuffer(clientAddrBuffer, 28B)
   RIORegisterBuffer(localAddrBuffer, 28B)
   RIORegisterBuffer(sendBuffer, 32KB)
   RIOCreateRequestQueue(sock, recvCQ, sendCQ, sessionId)

③ ioHandler->DoRecv(session)
   RIOReceiveEx(...) → 수신 대기 등록

④ session.stateMachine.SetReserved()
```

### 단계 3: 암호화 키/솔트 생성

```cpp
if (!InitSessionCrypto(*session)) {
    sendBuffer << CONNECT_RESULT_CODE::SESSION_KEY_GENERATION_FAILED;
    return CONNECT_RESULT_CODE::SESSION_KEY_GENERATION_FAILED;
}
```

→ 상세: [7. InitSessionCrypto](#7-initsessioncrypto--암호화-키-생성) 참조

### 단계 4: 전송 버퍼 구성

```cpp
sendBuffer << CONNECT_RESULT_CODE::SUCCESS;
SetSessionInfoToBuffer(*session, rudpServerIp, sendBuffer);
// → sendBuffer << rudpServerIp   (string)
// → sendBuffer << serverPort      (WORD, OS 자동 할당된 UDP 포트)
// → sendBuffer << sessionId       (SessionIdType = unsigned short)
// → 버퍼에 sessionKey 16B 복사
// → 버퍼에 sessionSalt 16B 복사

success = true;  // RAII 가드 해제
sessionGuard.Dismiss();
return CONNECT_RESULT_CODE::SUCCESS;
```

---

## 7. InitSessionCrypto — 암호화 키 생성

```cpp
bool RUDPSessionBroker::InitSessionCrypto(RUDPSession& session) const
{
    // ① sessionKey 생성 (16 bytes, CSPRNG)
    auto keyBytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_KEY_SIZE);
    if (!keyBytes.has_value()) {
        LOG_ERROR("GenerateSecureRandomBytes for sessionKey failed");
        return false;
    }
    sessionDelegate.SetSessionKey(session, keyBytes->data());

    // ② sessionSalt 생성 (16 bytes, CSPRNG)
    auto saltBytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_SALT_SIZE);
    if (!saltBytes.has_value()) {
        LOG_ERROR("GenerateSecureRandomBytes for sessionSalt failed");
        return false;
    }
    sessionDelegate.SetSessionSalt(session, saltBytes->data());

    // ③ BCrypt 키 오브젝트 버퍼 할당
    auto& crypto = CryptoHelper::GetTLSInstance();
    ULONG keyObjSize = crypto.GetKeyObjectSize();
    auto* keyObjBuffer = new unsigned char[keyObjSize];

    // ④ BCrypt 키 핸들 생성
    BCRYPT_KEY_HANDLE handle = crypto.GetSymmetricKeyHandle(
        keyObjBuffer, keyBytes->data());

    if (handle == nullptr) {
        LOG_ERROR("GetSymmetricKeyHandle failed");
        delete[] keyObjBuffer;
        return false;
    }

    // ⑤ 세션에 저장 (소유권 이전)
    sessionDelegate.SetKeyObjectBuffer(session, keyObjBuffer);
    sessionDelegate.SetSessionKeyHandle(session, handle);

    return true;
}
```

**키/솔트 전달 이후 클라이언트 처리:**

```
클라이언트:
  sessionKey, sessionSalt를 TLS로 수신
  CryptoHelper::GetSymmetricKeyHandle(keyObjBuffer, sessionKey) → 로컬 keyHandle 생성
  → 이후 SERVER_TO_CLIENT 패킷 복호화 / CLIENT_TO_SERVER 패킷 암호화에 사용
```

---

## 8. SendSessionInfoToClient — TLS 전송

```cpp
static bool RUDPSessionBroker::SendSessionInfoToClient(
    const SOCKET& clientSocket,
    TLSHelper::TLSHelperServer& localTlsHelper,
    OUT NetBuffer& sendBuffer
)
```

```
1. PacketCryptoHelper::SetHeader(sendBuffer)
   → HeaderCode 1B + PayloadLen 2B 완성 (extraSize=0, TLS로 보호되므로 AES-GCM 불필요)

2. TLSHelperServer::EncryptData(
       sendBuffer.m_pSerializeBuffer,
       sendBuffer.m_iWrite,
       encryptedBuffer,
       encryptedSize)
   → SECBUFFER_STREAM_HEADER + DATA + TRAILER 구성
   → EncryptMessage() → in-place 암호화

3. SendAll(clientSocket, encryptedBuffer, encryptedSize)
   → while (sent < encryptedSize):
        send(clientSocket, encryptedBuffer + sent, encryptedSize - sent, 0)
        sent += result
   → partial send를 루프로 보장

4. TLSHelperServer::EncryptCloseNotify(closeNotifyBuf, closeNotifySize)
   → ApplyControlToken(SCHANNEL_SHUTDOWN)
   → InitializeSecurityContext → close_notify Alert 레코드 생성

5. send(clientSocket, closeNotifyBuf, closeNotifySize, 0)
   → close_notify 전송 (정상 TLS 종료 신호)

6. shutdown(clientSocket, SD_SEND)
   → TCP FIN 전송 (클라이언트에게 서버 전송 완료 신호)

7. 클라이언트 FIN 대기 (4-way handshake 완료)
   while recv(clientSocket, ...) > 0:
     continue  ← 클라이언트가 보내는 데이터 무시 (close_notify 등)
   → recv == 0 → 클라이언트 FIN 도착

8. sendBuffer.Init()
```

**shutdown + recv 루프가 필요한 이유:**

```
서버: send(close_notify) → shutdown(SD_SEND) → TCP FIN 전송
클라이언트: close_notify 수신 → 자체 close_notify 응답 → closesocket

서버가 recv 없이 closesocket하면:
  RST 패킷 전송 → 클라이언트의 응답이 RST로 종료
  → 클라이언트가 데이터를 완전히 수신했는지 보장 불가

recv 루프로 대기하면:
  클라이언트의 FIN(= recv 반환 0)까지 기다림
  → 완전한 4-way handshake → 데이터 신뢰성 보장
```

---

## 9. 실패 처리 매트릭스

| 단계 | 실패 조건 | 응답 코드 | 세션 처리 |
|------|-----------|-----------|-----------|
| `AcquireSession()` | 풀 고갈 | `SERVER_FULL` | 없음 (세션 없음) |
| `InitReserveSession` 소켓 | `WSASocket`/`bind` 실패 | `CREATE_SOCKET_FAILED` | `AbortReservedSession` |
| `InitReserveSession` RIO | `RIOCreateRequestQueue` 실패 | `RIO_INIT_FAILED` | `AbortReservedSession` |
| `InitReserveSession` DoRecv | `RIOReceiveEx` 실패 | `DO_RECV_FAILED` | `AbortReservedSession` |
| `InitSessionCrypto` | `BCryptGenRandom` 실패 | `SESSION_KEY_GENERATION_FAILED` | `AbortReservedSession` |
| TLS 핸드셰이크 | `AcceptSecurityContext` 실패 | 연결 끊김 (코드 없음) | `AbortReservedSession` |
| `SendSessionInfoToClient` | `send` 실패 | 연결 끊김 | `AbortReservedSession` |

**`AbortReservedSession` 동작:**

```cpp
void AbortReservedSession(RUDPSession& session) {
    if (!session.stateMachine.TryAbortReserved()) return;
    // TryAbortReserved: RESERVED → RELEASING (CAS)
    // 이미 CONNECTED이면 실패 (DoDisconnect가 처리)

    sessionDelegate.CloseSocket(session);
    sessionDelegate.InitializeSession(session);
    // → sessionKey/salt/keyHandle 초기화
    // → RIO 컨텍스트 정리

    session.stateMachine.SetDisconnected();
    DisconnectSession(session.GetSessionId());
    // → unusedSessionIdList.push_back(id)
}
```

---

## 10. 예약 세션 타임아웃

RESERVED 상태 세션이 30초 이상 유지되면 HeartbeatThread가 회수한다.

```cpp
// MultiSocketRUDPCore::RunHeartbeatThread
for (auto* session : sessionManager->GetSessionList()) {
    if (!session->IsReserved()) continue;

    if (GetTickCount64() - session->GetReservedTimestamp()
        >= RESERVED_SESSION_TIMEOUT_MS) {  // = 30000ms

        sessionDelegate.AbortReservedSession(*session);
    }
}
```

**타임아웃이 필요한 시나리오:**

```
1. 클라이언트가 세션 정보를 받았지만 UDP 연결을 시도하지 않음
   (클라이언트 크래시, 네트워크 단절, 악성 클라이언트)

2. 세션 정보 수신 중 클라이언트가 끊김
   (SendSessionInfoToClient 실패, 세션은 RESERVED 상태로 남음)
```

**`TryAbortReserved` 경쟁 조건 처리:**

```cpp
// 타임아웃 30초 직전에 CONNECT 패킷이 도착하는 경우:
// HeartbeatThread: TryAbortReserved() → RESERVED → RELEASING (CAS 성공)
// RecvLogic Worker: TryConnect() → RESERVED → CONNECTED (CAS 실패, 이미 RELEASING)
// → TryConnect 실패, DoDisconnect가 나중에 정리

// HeartbeatThread: TryAbortReserved() → CAS 시도 (RESERVED)
// RecvLogic Worker: TryConnect() → RESERVED → CONNECTED (먼저 CAS 성공)
// → TryAbortReserved 실패 (이미 CONNECTED) → HeartbeatThread가 건너뜀
```

---

## 11. TLS 인증서 설정

### 개발 환경

```batch
Tool\ForTLS\CreateDevTLSCert.bat
```

```powershell
# 내부 동작
New-SelfSignedCertificate `
    -Subject "CN=DevServerCert" `
    -CertStoreLocation "Cert:\LocalMachine\MY" `
    -KeyExportPolicy Exportable `
    -KeyLength 2048 `
    -HashAlgorithm SHA256 `
    -NotAfter (Get-Date).AddYears(5)
```

```cpp
// 서버 코드
MultiSocketRUDPCore core(
    L"MY",            // Windows 인증서 저장소 (LocalMachine\MY)
    L"DevServerCert"  // 인증서 Subject Name (CN=)
);
```

### 운영 환경

```
1. CA 발급 인증서 PFX 획득
2. certmgr.msc → 로컬 컴퓨터 → 개인 → 가져오기
3. 서버 코드: MultiSocketRUDPCore core(L"MY", L"your.domain.com")
4. 클라이언트: SCH_CRED_MANUAL_CRED_VALIDATION 제거 → 자동 검증 활성화
```

---

## 12. 세션 정보 페이로드 구조

`SendSessionInfoToClient`에서 TLS로 전송하는 페이로드:

```
오프셋   크기   내용
───────────────────────────────────────────────────────
  0      1B    HeaderCode   (NetBuffer::m_byHeaderCode)
  1      2B    PayloadLen   (little-endian uint16)
  3      4B    CONNECT_RESULT_CODE (enum class, int32_t)
  7     ?B    serverIp     (uint16 len + bytes, UTF-8)
  ?      2B    serverPort   (uint16_t, OS 자동 할당 UDP 포트)
  ?      2B    sessionId    (uint16_t)
  ?     16B    sessionKey   (raw bytes)
  ?     16B    sessionSalt  (raw bytes)
───────────────────────────────────────────────────────
```

**클라이언트 파싱 (`SetTargetSessionInfo`):**

```cpp
NetBuffer buf;
// ... 복사 ...
buf.m_iRead = df_HEADER_SIZE;  // 헤더 스킵

CONNECT_RESULT_CODE code;
buf >> code;
if (code != SUCCESS) return false;

std::string serverIp;
WORD serverPort;
SessionIdType sid;
buf >> serverIp >> serverPort >> sid;

buf.GetBuffer(sessionKey,  SESSION_KEY_SIZE);   // 16B raw 복사
buf.GetBuffer(sessionSalt, SESSION_SALT_SIZE);  // 16B raw 복사
```

---

## 관련 문서
- [[TLSHelper]] — TLS 핸드셰이크 상세
- [[CryptoSystem]] — 세션 키/솔트 생성 (BCryptGenRandom)
- [[SessionLifecycle]] — RESERVED 상태 전이 조건
- [[MultiSocketRUDPCore]] — InitReserveSession 구현, AcquireSession
- [[RUDPClientCore]] — 클라이언트 측 세션 수신
- [[TroubleShooting]] — TLS 핸드셰이크 실패 해결
