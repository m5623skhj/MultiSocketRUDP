# RudpSession (C# 클라이언트 세션)

> BotTester가 사용하는 C# UDP 세션 구현을 현재 코드 기준으로 정리한다.

---

## 구조

핵심 구성은 아래다.

- `SessionInfo`
- `TargetServerInfo`
- `UdpClient`
- `HoldingPacketStore`
- `BufferStore`
- `Channel<Action>` 기반 수신 후속 처리 큐 (단일 리더/라이터)
- `Channel<Action>` 기반 비신뢰성 처리 큐 (단일 리더/라이터, 용량 초과 시 가장 오래된 패킷 삭제)

이 구현은 C++ `RUDPClientCore`와 프로토콜은 같지만 내부 구조는 다르다.
## 초기화

세션 브로커 응답을 파싱한 뒤 아래 작업을 수행한다.

1. `SessionInfo` 채움
2. `AesGcm` 생성
3. `UdpClient.Connect(...)`
4. `Task.Run(ReceiveAsync)`
5. `Task.Run(PacketProcessorAsync)`
6. `Task.Run(RetransmissionAsync)`
7. `Task.Run(SendConnectPacketAsync)`
8. `Task.Run(SendUnreliablePacketsAsync)`

### 비동기 내부 작업

모든 비동기 작업은 세션 생명주기 내에서 관리되며, `CancellationToken`에 의해 중단된다.

#### `SendUnreliablePacket`

```csharp
public bool SendUnreliablePacket(NetBuffer packetBuffer, PacketId packetId)
```

비신뢰성 패킷을 큐에 수용한다. 실제 전달이나 ACK를 보장하지 않는다. 연결이 종료되었거나 최종 크기가 IPv4 UDP 한도를 넘으면 버퍼를 변경하지 않고 `false`를 반환한다.

#### `PacketProcessorAsync`

```csharp
private async Task PacketProcessorAsync()
```

수신된 신뢰성 패킷(`recvProcessingChannel`)과 비신뢰성 패킷(`unreliableProcessingChannel`)을 처리한다. 각 채널별로 처리하여 비신뢰성 트래픽이 신뢰성 처리를 지연시키지 않도록 설계되었다.

#### `RetransmissionAsync`

```csharp
private async Task RetransmissionAsync()
```

`PeriodicTimer`를 사용하여 주기적으로 재전송 타임아웃을 검사한다. `bufferStore`를 조회하여 ACK를 받지 못한 패킷의 재전송을 수행한다.
## 세션 브로커 응답

현재 C#도 아래 순서로 읽는다.

```text
protocolVersion 4B (little endian, 값 2)
CONNECT_RESULT_CODE 1B
serverIp string
serverPort 2B
sessionId 2B
sessionKey 16B
sessionSalt 16B
```

응답 결과 코드는 `ConnectResultCode : byte`다.

---

## 송신

### `SendPacket`

```csharp
public async Task SendPacket(NetBuffer packetBuffer, PacketId packetId, PacketType packetType = PacketType.SendType)
```

지정한 `packetType`에 따라 패킷을 암호화하고 전송한다. `UnreliableSendType`인 경우 `SendUnreliablePacket`을 호출하며, 그 외의 경우 `sequence`를 발급하고 암호화한 뒤 비동기로 전송한다.

### `SendUnreliablePacket`

```csharp
public bool SendUnreliablePacket(NetBuffer packetBuffer, PacketId packetId)
```

비신뢰성 패킷을 큐에 삽입한다. 실제 전달이나 ACK를 보장하지 않는다.

| 반환값 | 조건 |
|--------|------|
| `true` | 패킷이 큐에 성공적으로 수용됨 |
| `false` | 세션이 종료되었거나 패킷 크기가 IPv4 UDP 한도를 초과함 |

> **주의:** 연결이 종료되었거나 최종 크기가 IPv4 UDP 한도를 넘으면 버퍼를 변경하지 않고 `false`를 반환한다. 번호 발급부터 큐 삽입까지 동일한 잠금(`aesGcmLock`)으로 보호하여 동시 송신 순서와 암호화 자원 수명을 보장한다.
## 수신

수신 데이터그램은 `ReceiveAsync()`에서 수신하며, `ProcessReceivedStreamAsync()`가 헤더 정보를 기준으로 하나의 데이터그램 내에 합쳐진 여러 패킷을 분리한다.

분리된 패킷은 `ProcessReceivedPacketAsync()`를 거쳐 복호화 및 타입 식별이 수행된다. 이후 패킷 타입에 따라 신뢰성(Reliable) 또는 비신뢰성(Unreliable) 채널로 분류되어 처리 대기열에 적재되며, `PacketProcessorAsync()`가 이를 비동기적으로 순차 처리한다.

- `UnreliableSendType`(7): `ServerToClientUnreliable` 방향으로 인증 및 수신 처리한다. ACK 및 신뢰성 순서 대기를 거치지 않으며, 비신뢰성 콜백 대기열이 가득 차면 오래된 대기열을 제거한다.
- 신뢰성 패킷: `ServerToClient` 및 `ServerToClientReply` 타입이 해당하며, 정해진 순서 보장 로직에 따라 처리된다.

`PacketProcessorAsync()`는 내부적으로 두 채널(`recvProcessingChannel`, `unreliableProcessingChannel`)의 데이터를 각각 읽어와 사용자 정의 콜백(ExecuteReceivedAction)을 번갈아 호출한다. 이를 통해 비신뢰성 트래픽이 연결 콜백이나 신뢰성 처리를 지연시키지 않도록 설계되었다.

서버 생존 검사는 인증에 성공한 수신 횟수를 기준으로 한다. 비신뢰성 패킷만 도착해도 연결을 유지하며 인증 실패 패킷은 반영하지 않는다. 64비트 번호 소진 및 순환 방어는 현재 구현 범위에 포함되지 않는다.
## 재전송 관련 현재 상수

현재 구현 기준 값은 아래다.

- `RetransmissionWakeUpMs = 16`
- `BufferStore.RetransmissionTimeoutMs = 20`
- `BufferStore.RetransmissionMaxCount = 16`
- 서버 생존 확인 주기 `15초`

예전 문서의 `30ms`, `32ms`, `50ms` 설명은 현재 코드와 맞지 않는다.

---

## 연결 완료와 종료

ACK sequence 0을 받으면:

1. `isConnected = true`
2. `SessionState = Connected`
3. `StartServerAliveCheck(CancellationToken)` 시작
4. `OnConnected()` 콜백 큐잉

종료 시에는 `Disconnect()`를 호출한다. 해당 메서드는 내부적으로 정리 프로세스(`Cleanup(SessionDisconnectReason)`)를 수행하여 소켓, 토큰, 보류 저장소, 재전송 저장소를 해제한다.

---
## 구현상 주의

- C# 구현은 `remoteAdvertisedWindow` 기반 단순 전송 창 모델을 그대로 쓰지 않는다.
- ACK 이후 정리는 `BufferStore` 중심으로 이뤄진다.
- 수신 후속 처리는 `recvProcessingChannel`(Unbounded)과 `unreliableProcessingChannel`(Bounded)을 통해 처리되며, 모두 단일 reader/writer 구조로 운영된다.
## 관련 문서

---

### `UnreliablePacketQueue`

> **비신뢰성 패킷을 위한 Bounded Channel 기반 큐.**  
> 패킷 저장 공간이 가득 찰 경우 가장 오래된 패킷을 삭제하는 정책(`DropOldest`)을 사용하며, 단일 리더(Single Reader) 환경에 최적화되어 있다.

---

### `UnreliablePacketQueue` 생성자

```csharp
public UnreliablePacketQueue(int capacity = ProtocolConstants.UnreliableQueueCapacity)
```

`UnreliablePacketQueue` 인스턴스를 초기화한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `capacity` | `int` | 큐의 최대 수용량. 기본값은 `ProtocolConstants.UnreliableQueueCapacity` |

---

### `Enqueue`

```csharp
public bool Enqueue(ReadOnlyMemory<byte> packet)
```

큐에 새로운 패킷을 삽입한다. 큐가 가득 찬 경우 가장 오래된 패킷을 제거하고 새로운 패킷을 수용한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `packet` | `ReadOnlyMemory<byte>` | 큐에 추가할 패킷 데이터 |

**반환값**: `true` 반환. (항상 성공)

---

### `ReadAllAsync`

```csharp
public IAsyncEnumerable<ReadOnlyMemory<byte>> ReadAllAsync(CancellationToken token)
```

큐에 저장된 패킷을 비동기적으로 읽기 위한 스트림을 반환한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `token` | `CancellationToken` | 작업 취소를 위한 토큰 |

---

### `Complete`

```csharp
public void Complete()
```

큐의 쓰기 작업을 종료한다. 이후 더 이상 패킷을 추가할 수 없다.

---

### `Clear`

```csharp
public void Clear()
```

큐에 남아있는 모든 패킷을 비운다. 큐의 상태를 비어있는 상태로 만든다.


- [[SessionGetter_CS]] - 브로커 응답 수신
- [[RUDPClientCore]] - C++ 클라이언트 구현
- [[FlowController]] - 공통 개념
- [[BufferStore]] - 미응답 송신 패킷과 재전송 횟수 추적
- [[PacketLossSimulator]] - 송수신 datagram 손실 시뮬레이션
