# PacketCryptoHelper

> **`NetBuffer` 기반 패킷 구조를 이해하고 [[CryptoHelper]]를 호출하는 암복호화 래퍼.**  
> 서버([[RUDPSession]])와 클라이언트([[RUDPClientCore]]) 양쪽에서 공통으로 사용하며,  
> 패킷 버퍼의 어디부터 어디까지 암호화할지, 헤더를 어떻게 구성하는지를 전담한다.

---

## 목차

1. [설계 위치](#1-설계-위치)
2. [SetHeader — 헤더 완성](#2-setheader--헤더-완성)
3. [EncodePacket — 패킷 암호화](#3-encodepacket--패킷-암호화)
4. [DecodePacket — 패킷 복호화](#4-decodepacket--패킷-복호화)
5. [오프셋 상수 정리](#5-오프셋-상수-정리)
6. [IPacketCryptoHelper / Adapter 패턴](#6-ipacketcryptohelper--adapter-패턴)
7. [m_bIsEncoded 중복 인코딩 방지](#7-m_bisencoded-중복-인코딩-방지)
8. [콜 사이트 전체 목록](#8-콜-사이트-전체-목록)

---

## 1. 설계 위치

```
[RUDPSession::SendPacketImmediate]     [RUDPClientCore::SendPacket]
              │                                    │
              └──────────────┬─────────────────────┘
                             ▼
                  PacketCryptoHelper::EncodePacket()
                             │
                   CryptoHelper::GetTLSInstance()
                             │
                   CryptoHelper::EncryptAESGCM()


[RUDPPacketProcessor::ProcessByPacketType]  [RUDPClientCore::ProcessRecvPacket]
                    │                                    │
                    └──────────────┬─────────────────────┘
                                   ▼
                      PacketCryptoHelper::DecodePacket()
                                   │
                         CryptoHelper::DecryptAESGCM()
```

---

## 2. SetHeader — 헤더 완성

```cpp
static void PacketCryptoHelper::SetHeader(
    OUT NetBuffer& netBuffer,
    const int extraSize = 0
)
```

**역할:** `NetBuffer`에 쌓인 페이로드 기준으로 3바이트 헤더를 완성한다.

```cpp
void PacketCryptoHelper::SetHeader(OUT NetBuffer& netBuffer, const int extraSize)
{
    // Byte 0: HeaderCode
    netBuffer.m_pSerializeBuffer[0] = NetBuffer::m_byHeaderCode;

    // Byte 1-2: PayloadLength (헤더 이후 바이트 수)
    //   m_iWrite = 현재까지 쓰인 바이트 (헤더 포함)
    //   extraSize = AUTH_TAG_SIZE(16) → 아직 AuthTag는 안 썼지만 길이에 포함
    *reinterpret_cast<uint16_t*>(&netBuffer.m_pSerializeBuffer[1])
        = static_cast<uint16_t>(netBuffer.m_iWrite - df_HEADER_SIZE + extraSize);

    // 읽기 커서 초기화
    netBuffer.m_iRead = 0;

    // 최종 끝 위치 (AuthTag 포함)
    netBuffer.m_iWriteLast = netBuffer.m_iWrite + extraSize;
}
```

**`extraSize`가 필요한 이유:**

```
EncodePacket 호출 전 버퍼 상태:
  [Header 3B][Type 1B][Seq 8B][PacketId 4B][Payload N B]
  m_iWrite = 3+1+8+4+N

SetHeader(extraSize=AUTH_TAG_SIZE=16):
  PayloadLen = m_iWrite - 3 + 16 = 1+8+4+N+16  ← AuthTag 포함
  m_iWriteLast = m_iWrite + 16                  ← EncodePacket 후 AuthTag를 여기까지 추가

EncodePacket 후:
  [Header 3B][Type 1B][Seq 8B][PacketId 4B][Ciphertext N B][AuthTag 16 B]
  m_iWrite = m_iWriteLast
```

---

## 3. EncodePacket — 패킷 암호화

```cpp
static void PacketCryptoHelper::EncodePacket(
    OUT NetBuffer& packet,
    PacketSequence packetSequence,
    CryptoHelper::PACKET_DIRECTION direction,
    const unsigned char* sessionSalt,
    size_t sessionSaltSize,
    const BCRYPT_KEY_HANDLE& keyHandle,
    bool isCorePacket
)
```

**내부 구현:**

```cpp
{
    // ① 중복 인코딩 방지
    if (packet.m_bIsEncoded) return;

    // ② 헤더 완성 (PayloadLen에 AuthTag 크기 미리 포함)
    SetHeader(packet, AUTH_TAG_SIZE);

    // ③ AAD 추출 (헤더 3B + PacketType 1B + Sequence 8B = 12 bytes)
    const unsigned char* aad = reinterpret_cast<const unsigned char*>(
        packet.m_pSerializeBuffer);
    constexpr size_t aadSize = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
    // = 3 + 1 + 8 = 12

    // ④ Nonce 생성
    auto nonce = CryptoHelper::GenerateNonce(
        sessionSalt, sessionSaltSize, packetSequence, direction);

    // ⑤ 암호화 범위 결정
    size_t bodyOffset;
    if (isCorePacket) {
        bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
        // = 3 + 1 + 8 = 12  (PacketId 없음)
    } else {
        bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE)
                   + sizeof(PacketSequence) + sizeof(PacketId);
        // = 3 + 1 + 8 + 4 = 16
    }

    char* bodyStart   = packet.m_pSerializeBuffer + bodyOffset;
    size_t bodySize   = packet.m_iWrite - bodyOffset;
    // 현재 m_iWrite는 SetHeader 이전 값 (AuthTag 아직 안 씀)

    // ⑥ AES-GCM 암호화 (in-place)
    unsigned char authTag[AUTH_TAG_SIZE];
    bool ok = CryptoHelper::EncryptAESGCM(
        nonce.data(), nonce.size(),
        aad, aadSize,
        bodyStart, bodySize,   // plaintext
        bodyStart, bodySize,   // ciphertext (같은 버퍼 → in-place)
        authTag,
        keyHandle
    );

    if (!ok) {
        LOG_ERROR("EncodePacket: EncryptAESGCM failed");
        return;
    }

    // ⑦ AuthTag 버퍼 끝에 추가
    memcpy(packet.m_pSerializeBuffer + packet.m_iWrite, authTag, AUTH_TAG_SIZE);
    packet.m_iWrite = packet.m_iWriteLast;  // m_iWrite 갱신

    // ⑧ 인코딩 완료 표시
    packet.m_bIsEncoded = true;
}
```

---

## 4. DecodePacket — 패킷 복호화

```cpp
static bool PacketCryptoHelper::DecodePacket(
    OUT NetBuffer& packet,
    const unsigned char* sessionSalt,
    size_t sessionSaltSize,
    const BCRYPT_KEY_HANDLE& keyHandle,
    bool isCorePacket,
    CryptoHelper::PACKET_DIRECTION direction
)
```

> `PacketType`은 `ProcessByPacketType`에서 이미 읽은 후 호출됨.  
> 즉, `m_iRead`는 PacketType(1B) 이후에 위치한 상태.

**내부 구현:**

```cpp
{
    // ① 최소 크기 검사
    // GetUseSize() = m_iWrite - m_iRead (현재 읽기 가능한 바이트)
    const size_t minBodySize = isCorePacket
        ? sizeof(PacketSequence) + AUTH_TAG_SIZE               // 8 + 16 = 24
        : sizeof(PacketSequence) + sizeof(PacketId) + AUTH_TAG_SIZE; // 8+4+16=28

    if (packet.GetUseSize() < minBodySize) {
        LOG_ERROR("DecodePacket: packet too small");
        return false;
    }

    // ② PacketSequence 추출 (암호화 밖, 헤더+Type 뒤)
    //    m_iRead는 아직 PacketType 이후이므로 Sequence 위치 = m_iRead
    PacketSequence packetSequence;
    memcpy(&packetSequence,
           packet.m_pSerializeBuffer + df_HEADER_SIZE + sizeof(PACKET_TYPE),
           sizeof(PacketSequence));
    // 직접 버퍼에서 읽음 (m_iRead 변경 안 함)

    // ③ AuthTag 위치 계산
    const unsigned char* authTag = reinterpret_cast<const unsigned char*>(
        packet.m_pSerializeBuffer + packet.m_iWrite - AUTH_TAG_SIZE);

    // ④ body 범위 결정
    size_t bodyOffset;
    if (isCorePacket) {
        bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
        // = 12
    } else {
        bodyOffset = df_HEADER_SIZE + sizeof(PACKET_TYPE)
                   + sizeof(PacketSequence) + sizeof(PacketId);
        // = 16
    }

    char* bodyStart = packet.m_pSerializeBuffer + bodyOffset;
    size_t bodySize = (packet.m_iWrite - AUTH_TAG_SIZE) - bodyOffset;

    // ⑤ AAD (헤더 + Type + Sequence = 12 bytes, 암호화 안 됨)
    const unsigned char* aad = reinterpret_cast<const unsigned char*>(
        packet.m_pSerializeBuffer);
    constexpr size_t aadSize = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);

    // ⑥ Nonce 생성
    auto nonce = CryptoHelper::GenerateNonce(
        sessionSalt, sessionSaltSize, packetSequence, direction);

    // ⑦ AES-GCM 복호화 + 인증 태그 검증
    bool ok = CryptoHelper::DecryptAESGCM(
        nonce.data(), nonce.size(),
        aad, aadSize,
        bodyStart, bodySize,    // ciphertext
        authTag,                // 검증할 태그
        bodyStart, bodySize,    // plaintext (in-place)
        keyHandle
    );

    if (!ok) {
        // 인증 실패 → 로그만 남기고 false 반환 (호출자가 패킷 폐기)
        LOG_ERROR(std::format(
            "DecodePacket: DecryptAESGCM failed. direction={}", static_cast<int>(direction)));
        return false;
    }

    // ⑧ AuthTag 제거 (m_iWrite 조정)
    packet.m_iWrite -= AUTH_TAG_SIZE;

    return true;
}
```

---

## 5. 오프셋 상수 정리

```
PacketType 이후 기준 (m_iRead 기준):

isCorePacket=false (일반 패킷):
  m_iRead → [Sequence 8B][PacketId 4B][Payload N B]
  bodyOffsetWithNotHeader = sizeof(PacketType) + sizeof(Sequence) + sizeof(PacketId)
                          = 1 + 8 + 4 = 13

isCorePacket=true (코어 패킷):
  m_iRead → [Sequence 8B][Payload N B]
  bodyOffsetWithNotHeaderForCorePacket = sizeof(PacketType) + sizeof(Sequence)
                                       = 1 + 8 = 9

절대 오프셋 기준 (m_pSerializeBuffer[0]):

isCorePacket=false:
  bodyOffset = df_HEADER_SIZE + PacketType + Sequence + PacketId
             = 3 + 1 + 8 + 4 = 16
  →  bodyOffsetWithHeader = 16

isCorePacket=true:
  bodyOffset = df_HEADER_SIZE + PacketType + Sequence
             = 3 + 1 + 8 = 12
  →  bodyOffsetWithHeaderForCorePacket = 12

AAD 범위 (항상 동일):
  [0 .. 11] = df_HEADER_SIZE(3) + PacketType(1) + Sequence(8)

AuthTag 위치:
  m_iWrite - AUTH_TAG_SIZE  (복호화 전)
  m_iWrite (복호화 후, AUTH_TAG_SIZE만큼 줄어듦)
```

---

## 6. IPacketCryptoHelper / Adapter 패턴

테스트 용이성을 위해 인터페이스와 어댑터로 분리:

```cpp
class IPacketCryptoHelper {
public:
    virtual ~IPacketCryptoHelper() = default;

    virtual bool DecodePacket(
        OUT NetBuffer& packet,
        const unsigned char* sessionSalt,
        size_t sessionSaltSize,
        const BCRYPT_KEY_HANDLE& keyHandle,
        bool isCorePacket,
        CryptoHelper::PACKET_DIRECTION direction
    ) = 0;

    virtual void EncodePacket(
        OUT NetBuffer& packet,
        PacketSequence packetSequence,
        CryptoHelper::PACKET_DIRECTION direction,
        const unsigned char* sessionSalt,
        size_t sessionSaltSize,
        const BCRYPT_KEY_HANDLE& keyHandle,
        bool isCorePacket
    ) = 0;

    virtual void SetHeader(OUT NetBuffer& netBuffer, int extraSize = 0) = 0;
};

class PacketCryptoHelperAdapter final : public IPacketCryptoHelper {
    bool DecodePacket(...) override {
        return PacketCryptoHelper::DecodePacket(...);  // 정적 메서드 위임
    }
    void EncodePacket(...) override {
        PacketCryptoHelper::EncodePacket(...);
    }
    void SetHeader(...) override {
        PacketCryptoHelper::SetHeader(...);
    }
};
```

**`RUDPPacketProcessor`는 `IPacketCryptoHelper*`를 생성자 주입으로 받아**  
단위 테스트에서 `MockPacketCryptoHelper`로 대체 가능하다.

---

## 7. `m_bIsEncoded` 중복 인코딩 방지

```cpp
// NetBuffer 멤버
bool m_bIsEncoded = false;

// EncodePacket 내부
if (packet.m_bIsEncoded) return;  // 이미 암호화됨 → 즉시 반환
// ...
packet.m_bIsEncoded = true;        // 완료 후 표시
```

**중복 인코딩이 발생할 수 있는 시나리오:**

```
재전송 경로:
  최초 전송: EncodePacket() → m_bIsEncoded=true
  재전송:    core.SendPacket(sendPacketInfo)
                → 같은 NetBuffer* 재사용
                → EncodePacket() 다시 호출될 수 있음
                → m_bIsEncoded 체크로 스킵
```

`SendPacketImmediate`에서 `m_bIsEncoded == false`인 경우만 `EncodePacket`을 호출하도록  
체크하는 경우도 있지만, `EncodePacket` 자체도 내부에서 중복 방지한다.

---

## 8. 콜 사이트 전체 목록

### `EncodePacket` 호출처

```cpp
// 서버
RUDPSession::SendPacketImmediate()
  → if (!buffer.m_bIsEncoded)
       PacketCryptoHelper::EncodePacket(buffer, sequence, direction,
           session.GetSessionSalt(), SESSION_SALT_SIZE,
           session.GetSessionKeyHandle(), isCorePacket)

// 클라이언트 (C++)
RUDPClientCore::SendPacket(buffer, sequence, isCorePacket)
  → PacketCryptoHelper::EncodePacket(...)
```

### `DecodePacket` 호출처

```cpp
// 서버 (DECODE_PACKET 매크로로 감싸서 호출)
RUDPPacketProcessor::ProcessByPacketType()
  → switch PacketType:
      CONNECT_TYPE:
        DecodePacket(buffer, salt, saltSize, keyHandle,
                     isCorePacket=true, CLIENT_TO_SERVER)
      SEND_TYPE:
        DecodePacket(buffer, ..., isCorePacket=false, CLIENT_TO_SERVER)
      SEND_REPLY_TYPE / HEARTBEAT_REPLY_TYPE:
        DecodePacket(buffer, ..., isCorePacket=true, CLIENT_TO_SERVER_REPLY)
      DISCONNECT_TYPE:
        DecodePacket(buffer, ..., isCorePacket=true, CLIENT_TO_SERVER)

// 클라이언트 (C++)
RUDPClientCore::ProcessRecvPacket()
  → switch PacketType:
      SEND_TYPE:
        DecodePacket(buffer, ..., isCorePacket=false, SERVER_TO_CLIENT)
      HEARTBEAT_TYPE:
        DecodePacket(buffer, ..., isCorePacket=true, SERVER_TO_CLIENT)
      SEND_REPLY_TYPE:
        DecodePacket(buffer, ..., isCorePacket=true, SERVER_TO_CLIENT_REPLY)
```

### `SetHeader` 호출처

```cpp
// 직접 호출 (EncodePacket 내부가 아닌 경우)
RUDPSessionBroker::SendSessionInfoToClient()
  → SetHeader(sendBuffer)  ← extraSize=0 (암호화 없이 TLS로 전송)
```

---

## 관련 문서
- [[CryptoHelper]] — BCrypt 저수준 연산
- [[CryptoSystem]] — Nonce 구조, 암호화 범위 설계
- [[PacketFormat]] — 패킷 레이아웃 및 오프셋 상수
- [[PacketProcessing]] — DECODE_PACKET 매크로 사용처
- [[RUDPSession]] — EncodePacket 호출 (SendPacketImmediate)
