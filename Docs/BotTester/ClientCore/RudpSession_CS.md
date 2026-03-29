# RudpSession (C# 클라이언트 세션)

> C# UDP 클라이언트 측 RUDP 세션 구현.  
> [[RUDPClientCore|C++ 클라이언트 코어]]와 동일한 프로토콜을 C#/`UdpClient`로 구현.

---

## 구조

```
RudpSession (abstract)
 ├── SessionInfo          ← 세션 키, 솔트, AesGcm, 상태
 ├── TargetServerInfo     ← 서버 IP/Port
 ├── UdpClient            ← UDP 소켓
 ├── BufferStore          ← 재전송 추적 (SortedDictionary)
 ├── HoldingPacketStore   ← 순서 대기 패킷 (SortedDictionary)
 └── CancellationTokenSource

Client : RudpSession      ← Contents 레이어
 ├── ActionGraph          ← 행동 트리 엔진
 └── GlobalContext        ← RuntimeContext
```

---

## 초기화 (`ParseSessionBrokerResponse`)

```
byte[] sessionInfoStream → NetBuffer 파싱
  ├─ HeaderCode (Skip 3B)
  ├─ ConnectResultCode → 실패 시 Exception
  ├─ serverIp, serverPort, sessionId
  ├─ sessionKey (16B), sessionSalt (16B)
  └─ AesGcm = new AesGcm(sessionKey, tagSizeInBytes: 16)

udpClient.Connect(serverIp, serverPort)
_ = ReceiveAsync()           ← 수신 루프 시작
_ = RetransmissionAsync()    ← 재전송 루프 시작
_ = SendConnectPacketAsync() ← CONNECT 패킷 전송
```

---

## 패킷 송신 (`SendPacket`)

```
SendPacket(packetBuffer, packetId, packetType=SendType)
  1. sequence = Interlocked.Increment(ref lastSendSequence)
  2. InsertPacketType()    ← 헤더 뒤에 삽입 (Array.Copy 이동)
  3. InsertPacketSequence() ← PacketType 뒤에 삽입
  4. InsertPacketId()      ← Sequence 뒤에 삽입
  5. NetBuffer.EncodePacket(AesGcm, sequence, ClientToServer, sessionSalt, isCorePacket=false)
  6. SendPacketInternal(sendPacketInfo)
       → bufferStore.EnqueueSendBuffer()
       → udpClient.SendAsync()
```

---

## 패킷 수신 (`ProcessReceivedPacket`)

```
PacketType 추출
isCorePacket = (type != SendType)
direction = ServerToClient (Send/Heartbeat) | ServerToClientReply

NetBuffer.DecodePacket(AesGcm, isCorePacket, sessionSalt, direction)

switch PacketType:
  HeartbeatType → SendReplyToServer(sequence)
  SendType      → SendReplyToServer(sequence)
                  순서 보장 (expectedRecvSequence 비교)
                    == expected+1 → OnRecvPacket() + ProcessHoldingPackets()
                    > expected+1  → HoldingPacketStore.Add()
                    <= expected   → 중복, 무시
  SendReplyType → OnSendReply(sequence)
                    sequence == 0 → isConnected=true, StartServerAliveCheck
                    bufferStore.RemoveSendBuffer(sequence)
```

---

## 재전송 (`RetransmissionAsync`)

```
PeriodicTimer(30ms)
  bufferStore.GetAllSendPacketInfos()
  foreach info where IsRetransmissionTime(now):  (now - SendTimeStamp >= 32ms)
    if IsExceedMaxRetransmissionCount():  (count >= 16)
      DisconnectAsync()
      return
    info.RefreshSendPacketInfo()
    udpClient.SendAsync(info.SentBuffer)
```

---

## 서버 생존 확인 (`StartServerAliveCheck`)

```
PeriodicTimer(15초)
  if expectedRecvSequence == prev → 서버 무응답 → DisconnectAsync()
  else prev = curr
```

---

## Nonce 생성 (C# 버전)

```csharp
// NetBuffer.GenerateNonce (C# 구현)
// 주의: C++ 서버와 바이트 순서 일치 필요
nonce[0..3] = sessionSalt[0..3]  // ← C++ 버전과 배치 다름
nonce[4..11] = packetSequence (big-endian 변형)
nonce[4] |= (byte)direction << 6
```

> C++ `CryptoHelper::GenerateNonce`와 완전히 동일한 Nonce를 생성해야 AES-GCM 인증이 통과한다.  
> 서버-클라이언트 간 Nonce 생성 로직 불일치 → `CryptographicException` → 패킷 폐기

---

## 관련 문서
- [[BotTesterCore]] — 세션 생성 및 관리
- [[SessionGetter_CS]] — 세션 정보 수신
- [[CryptoSystem]] — C++ 서버 측 Nonce 생성
- [[PacketFormat]] — 공유 패킷 구조
