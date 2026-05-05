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

## 함수 설명

### `HoldingPacketStore`

#### `Add(PacketSequence sequence, HeldPacket packet)`
- 순서가 도착하지 않은 패킷을 보류 저장소에 추가한다.

#### `Remove(PacketSequence sequence)`
- 보류 패킷을 제거한다.

#### `TryGetFirst(...)`
- 가장 앞선 sequence의 보류 패킷을 조회한다.

#### `GetCount()`
- 현재 보류 패킷 개수를 반환한다.

#### `TryGetRange(...)`
- 보류 중인 sequence의 최소/최대 범위를 구한다.

#### `Clear()`
- 보류 저장소를 비운다.

### `RudpSession`

#### `SessionIdType GetSessionId()`
- 현재 세션 ID를 반환한다.

#### `bool IsConnected()`
- 세션이 연결 완료 상태인지 반환한다.

#### `ParseSessionBrokerResponse(byte[] data)`
- SessionBroker 응답을 파싱해 서버 주소, 포트, 세션 키/솔트, 세션 ID를 초기화한다.

#### `void Disconnect()`
- 외부에서 세션 정리를 시작하는 진입점이다.

#### `Task SendPacket(...)`
- 패킷 타입/시퀀스/ID를 삽입하고 암호화한 뒤 실제 UDP 송신 큐에 넣는다.

#### `Task<bool> SendPacketInternal(SendPacketInfo sendPacketInfo)`
- 재전송 저장소 등록과 실제 `UdpClient.SendAsync` 호출을 수행한다.

#### `Task SendConnectPacketAsync()`
- 서버에 CONNECT 코어 패킷을 전송한다.

#### `NetBuffer MakeConnectPacket()`
- CONNECT 패킷 버퍼를 조립한다.

#### `Task ReceiveAsync()`
- UDP 수신 루프를 유지한다.

#### `Task PacketProcessorAsync()`
- 수신 버퍼를 순서/패킷 타입 규칙에 맞게 처리하는 메인 루프다.

#### `Task ProcessReceivedPacketAsync(byte[] data)`
- 수신 데이터 1개를 복호화하고 패킷 타입에 따라 분기한다.

#### `Task SendReplyToServerAsync(PacketSequence packetSequence)`
- ACK 응답을 서버로 보낸다.

#### `void OnSendReply(PacketSequence packetSequence)`
- 서버 ACK를 수신했을 때 재전송 추적 상태를 정리한다.

#### `Task RetransmissionAsync()`
- 재전송 타이머 루프를 돌며 미ACK 패킷을 다시 전송한다.

#### `Task DisconnectAsync()`
- 비동기 종료 패킷 송신과 리소스 정리를 수행한다.

#### `NetBuffer BuildDisconnectPacket()`
- DISCONNECT 코어 패킷을 조립한다.

#### `Task StartServerAliveCheck()`
- 일정 시간 수신이 없을 때 서버 무응답으로 간주하는 감시 루프를 시작한다.

#### `void Cleanup()`
- 소켓, 토큰, 저장소, 작업 상태를 정리한다.

---

## 관련 문서
- [[BotTesterCore]] — 세션 생성 및 관리
- [[SessionGetter_CS]] — 세션 정보 수신
- [[CryptoSystem]] — C++ 서버 측 Nonce 생성
- [[PacketFormat]] — 공유 패킷 구조
