# PacketCryptoHelper

> `NetBuffer`에 담긴 RUDP 패킷을 AES-GCM으로 암호화/복호화하는 공통 헬퍼다.
> 서버 `RUDPSession`과 C++ 클라이언트 `RUDPClientCore`가 같은 패킷 레이아웃과 AAD 범위를 사용하도록 맞춘다.

---

## 역할

`PacketCryptoHelper`는 아래 작업을 담당한다.

- `NetBuffer` 헤더 작성
- `sessionSalt`, `PacketSequence`, `PACKET_DIRECTION` 기반 nonce 생성
- 헤더/패킷 타입/시퀀스를 AAD로 사용
- 일반 패킷과 core packet의 암호화 시작 오프셋 분리
- AES-GCM AuthTag를 패킷 끝에 추가하거나 검증
- `m_bIsEncoded`로 중복 인코딩 방지

---

## 패킷 범위

### 일반 데이터 패킷

일반 패킷은 `PacketId`와 payload를 암호화한다.

```text
[Header 5B][PacketType 1B][Sequence 8B][PacketId 4B][Payload N][AuthTag 16B]
|<---------------- AAD 14B ---------------->|<------ encrypted ------>|
```

### Core packet

CONNECT, DISCONNECT, SEND_REPLY, HEARTBEAT 계열 core packet에는 `PacketId`가 없다.

```text
[Header 5B][PacketType 1B][Sequence 8B][Payload N][AuthTag 16B]
|<---------------- AAD 14B ---------------->|<- encrypted ->|
```

---

## 공개 함수

### `EncodePacket`

```cpp
static void EncodePacket(
    NetBuffer& packet,
    PacketSequence packetSequence,
    PACKET_DIRECTION direction,
    const std::vector<unsigned char>& sessionSalt,
    const BCRYPT_KEY_HANDLE& sessionKeyHandle,
    bool isCorePacket);

static void EncodePacket(
    NetBuffer& packet,
    PacketSequence packetSequence,
    PACKET_DIRECTION direction,
    const unsigned char* sessionSalt,
    size_t sessionSaltSize,
    const BCRYPT_KEY_HANDLE& sessionKeyHandle,
    bool isCorePacket);
```

패킷이 이미 인코딩된 상태면 즉시 반환한다.
그 외에는 nonce를 만들고, `SetHeader(packet, AUTH_TAG_SIZE)`로 AuthTag 공간을 반영한 뒤 body 영역을 in-place 암호화한다.

암호화 성공 시 AuthTag를 버퍼 끝에 쓰고 `packet.m_bIsEncoded = true`로 설정한다.

### `DecodePacket`

```cpp
static bool DecodePacket(
    NetBuffer& packet,
    const std::vector<unsigned char>& sessionSalt,
    const BCRYPT_KEY_HANDLE& sessionKeyHandle,
    bool isCorePacket,
    PACKET_DIRECTION direction);

static bool DecodePacket(
    NetBuffer& packet,
    const unsigned char* sessionSalt,
    size_t sessionSaltSize,
    const BCRYPT_KEY_HANDLE& sessionKeyHandle,
    bool isCorePacket,
    PACKET_DIRECTION direction);
```

수신 패킷의 sequence와 AuthTag를 읽고, 송신 시와 같은 nonce/AAD 범위로 AES-GCM 복호화를 수행한다.
복호화 또는 인증 검증에 실패하면 `false`를 반환한다.

### `SetHeader`

```cpp
static void SetHeader(NetBuffer& netBuffer, int extraSize = 0);
```

`HeaderCode`와 payload 길이를 기록한다. `extraSize`는 아직 쓰기 전인 AuthTag 같은 후미 데이터를 payload 길이에 포함할 때 사용한다.

```cpp
PayloadLength = m_iWrite - df_HEADER_SIZE + extraSize;
m_iRead = 0;
m_iWriteLast = m_iWrite + extraSize;
```

SessionBroker가 TLS로 세션 정보를 보낼 때는 암호화하지 않으므로 `extraSize = 0`으로 헤더만 구성한다.

---

## 오프셋 상수

```cpp
bodyOffsetWithHeader =
    df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);

bodyOffsetWithHeaderForCorePacket =
    df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);

bodyOffsetWithNotHeader =
    sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);

bodyOffsetWithNotHeaderForCorePacket =
    sizeof(PACKET_TYPE) + sizeof(PacketSequence);
```

`EncodePacket`은 `NetBuffer::GetUseSize()` 기준 body 크기를 계산하므로, header 포함 오프셋과 header 제외 오프셋을 모두 사용한다.

---

## 호출 지점

### 서버

- `RUDPSession` 송신 경로에서 `EncodePacket(...)` 호출
- `RUDPPacketProcessor` 수신 경로에서 packet type별 `DecodePacket(...)` 호출
- `RUDPSessionBroker` 세션 정보 송신 경로에서 `SetHeader(...)` 호출

### C++ 클라이언트

- `RUDPClientCore` 송신 경로에서 `EncodePacket(...)` 호출
- `RUDPClientCore::ProcessRecvPacket(...)`에서 수신 packet type별 `DecodePacket(...)` 호출

---

## 주의사항

- `PACKET_DIRECTION`은 송신자와 수신자가 동일하게 해석해야 한다. 방향이 다르면 nonce가 달라져 복호화가 실패한다.
- 일반 패킷과 core packet은 `PacketId` 존재 여부가 다르므로 `isCorePacket`을 잘못 넘기면 body 오프셋이 틀어진다.
- AAD 범위는 항상 header 5B, packet type 1B, sequence 8B를 합친 14B다.
- AuthTag는 암호문 뒤 16B이며, payload length에도 포함된다.

---

## 관련 문서

- [[CryptoHelper]] - BCrypt 기반 AES-GCM 처리
- [[CryptoSystem]] - nonce 구조와 암호화 설계
- [[Common/PacketFormat]] - 공용 패킷 레이아웃
- [[PacketProcessing]] - 서버 수신 처리 흐름
- [[RUDPSession]] - 서버 송신 경로
