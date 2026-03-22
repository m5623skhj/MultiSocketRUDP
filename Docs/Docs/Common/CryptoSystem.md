# 암호화 시스템 (Crypto System)

> **패킷 1개를 전송할 때 적용되는 AES-256-GCM 기반 암호화 전체 설계를 다룬다.**  
> 서버가 키/솔트를 생성하는 방법, Nonce를 구성하는 방법,  
> 패킷 버퍼 어디부터 어디까지 암호화하는지, 위변조 감지가 어떻게 동작하는지를 한 곳에서 파악할 수 있다.

---

## 목차

1. [전체 구성도](#1-전체-구성도)
2. [세션 키와 솔트 생성](#2-세션-키와-솔트-생성)
3. [Nonce 구조 (12 bytes)](#3-nonce-구조-12-bytes)
4. [패킷 내 암호화 범위](#4-패킷-내-암호화-범위)
5. [AAD (Additional Authenticated Data)](#5-aad-additional-authenticated-data)
6. [AuthTag (인증 태그, 16 bytes)](#6-authtag-인증-태그-16-bytes)
7. [암호화 흐름 단계별](#7-암호화-흐름-단계별)
8. [복호화 흐름 단계별](#8-복호화-흐름-단계별)
9. [방향별 Nonce 분리 설계](#9-방향별-nonce-분리-설계)
10. [isCorePacket 플래그의 영향](#10-iscorepacket-플래그의-영향)
11. [AES-GCM 선택 이유](#11-aes-gcm-선택-이유)
12. [C# 클라이언트와의 호환성](#12-c-클라이언트와의-호환성)

---

## 1. 전체 구성도

![[CryptoStructure.svg]]

```
레이어 구조:
 ┌─────────────────────────────────────────────────────────┐
 │ PacketCryptoHelper  ← 패킷 버퍼 구조를 이해하는 래퍼   │
 │   EncodePacket() / DecodePacket()                       │
 ├─────────────────────────────────────────────────────────┤
 │ CryptoHelper (thread_local)  ← BCrypt 원시 연산        │
 │   EncryptAESGCM() / DecryptAESGCM()                    │
 │   GenerateNonce() / GetSymmetricKeyHandle()             │
 ├─────────────────────────────────────────────────────────┤
 │ Windows BCrypt API (OS 제공)                            │
 │   BCryptEncrypt / BCryptDecrypt                         │
 │   BCRYPT_CHAIN_MODE_GCM                                 │
 └─────────────────────────────────────────────────────────┘
```

**파일 구조:**
```
Common/
 ├── CryptoHelper.h / .cpp        ← BCrypt 래퍼, thread_local 인스턴스
 ├── PacketCryptoHelper.h / .cpp  ← 패킷 버퍼 기반 암복호화
 └── CryptoType.h                 ← AUTH_TAG_SIZE, SESSION_KEY_SIZE 등 상수
```

---

## 2. 세션 키와 솔트 생성

세션마다 고유한 16바이트 키(`sessionKey`)와 16바이트 솔트(`sessionSalt`)를  
서버(SessionBroker)에서 생성해 TLS 채널로 클라이언트에게 전달한다.

```cpp
// RUDPSessionBroker::InitSessionCrypto
bool InitSessionCrypto(RUDPSession& session) const
{
    // ① sessionKey 생성 (16 bytes, CSPRNG)
    auto keyBytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_KEY_SIZE);
    if (!keyBytes) return false;
    session.SetSessionKey(keyBytes->data());

    // ② sessionSalt 생성 (16 bytes, CSPRNG)
    auto saltBytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_SALT_SIZE);
    if (!saltBytes) return false;
    session.SetSessionSalt(saltBytes->data());

    // ③ BCrypt 키 핸들 생성 (세션 유지 동안 재사용)
    auto& crypto = CryptoHelper::GetTLSInstance();
    ULONG keyObjSize = crypto.GetKeyObjectSize();
    unsigned char* keyObj = new unsigned char[keyObjSize];

    BCRYPT_KEY_HANDLE handle = crypto.GetSymmetricKeyHandle(keyObj, keyBytes->data());
    if (handle == nullptr) { delete[] keyObj; return false; }

    session.SetKeyObjectBuffer(keyObj);      // 소유권 이전
    session.SetSessionKeyHandle(handle);
    return true;
}
```

### `GenerateSecureRandomBytes` 내부

```cpp
std::optional<std::vector<unsigned char>>
CryptoHelper::GenerateSecureRandomBytes(unsigned short length)
{
    std::vector<unsigned char> buffer(length);
    NTSTATUS status = BCryptGenRandom(
        nullptr,                              // 알고리즘 공급자 (nullptr = OS 기본)
        buffer.data(),
        length,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG      // OS RNG 사용 (CNG 기반)
    );
    if (!BCRYPT_SUCCESS(status)) return std::nullopt;
    return buffer;
}
```

> `BCRYPT_USE_SYSTEM_PREFERRED_RNG`는 Windows Vista 이상에서  
> 하드웨어 엔트로피 소스(RDRAND 등)를 사용하는 CSPRNG에 위임한다.

### 키/솔트 전달 경로

```
[서버 SessionBroker]
  GenerateSecureRandomBytes(16) → sessionKey
  GenerateSecureRandomBytes(16) → sessionSalt
  GetSymmetricKeyHandle()       → sessionKeyHandle (서버 보관)
        │
   TLS 암호화 채널
        │
[클라이언트]
  recv sessionKey, sessionSalt
  GetSymmetricKeyHandle()       → sessionKeyHandle (클라이언트 보관)
```

클라이언트는 서버와 동일한 키/솔트로 `GetSymmetricKeyHandle()`을 로컬 실행해  
각자 BCrypt 핸들을 가진다. **키 자체는 네트워크를 통해 한 번만 전달된다.**

---

## 3. Nonce 구조 (12 bytes)

AES-GCM은 96비트(12바이트) Nonce를 사용한다.  
**같은 키로 같은 Nonce를 두 번 사용하면 보안이 완전히 붕괴**되므로  
세션 키/솔트/시퀀스/방향의 조합으로 유일성을 보장한다.

```
Byte Index | 크기 | 내용
───────────┼──────┼──────────────────────────────────────────────
    0      │  1B  │ (direction << 6) | (sessionSalt[0] & 0x3F)
   1~3     │  3B  │ sessionSalt[1..3]
   4~11    │  8B  │ packetSequence (big-endian, uint64)
───────────┴──────┴──────────────────────────────────────────────
```

**방향(direction) 비트 매핑:**

| `PACKET_DIRECTION` enum | 값 | 상위 2비트 | 의미 |
|-------------------------|----|-----------|------|
| `CLIENT_TO_SERVER`       | 0  | `0b00`    | 클→서버 데이터 |
| `CLIENT_TO_SERVER_REPLY` | 1  | `0b01`    | 클→서버 ACK |
| `SERVER_TO_CLIENT`       | 2  | `0b10`    | 서→클라이언트 데이터 |
| `SERVER_TO_CLIENT_REPLY` | 3  | `0b11`    | 서→클라이언트 ACK |

**Byte 0 구성 예시:**

```
direction = SERVER_TO_CLIENT (0b10)
sessionSalt[0] = 0xA5 (0b10100101)

Byte 0 = (0b10 << 6) | (0xA5 & 0x3F)
       = 0b10000000  |  0b00100101
       = 0b10100101 = 0xA5
```

```cpp
// CryptoHelper::GenerateNonce 구현
std::vector<unsigned char> CryptoHelper::GenerateNonce(
    const unsigned char* sessionSalt,
    size_t sessionSaltSize,
    PacketSequence packetSequence,
    PACKET_DIRECTION direction)
{
    constexpr size_t NONCE_SIZE = 12;
    std::vector<unsigned char> nonce(NONCE_SIZE, 0);

    // Byte 0: 방향 비트 + 솔트 하위 6비트
    nonce[0] = (static_cast<unsigned char>(direction) << 6)
             | (sessionSalt[0] & 0x3F);

    // Byte 1-3: sessionSalt[1..3]
    nonce[1] = sessionSalt[1];
    nonce[2] = sessionSalt[2];
    nonce[3] = sessionSalt[3];

    // Byte 4-11: packetSequence big-endian
    for (int i = 0; i < 8; ++i) {
        nonce[11 - i] = static_cast<unsigned char>(packetSequence & 0xFF);
        packetSequence >>= 8;
    }

    return nonce;
}
```

**유일성 보장 논리:**

```
같은 세션 내에서:
  packetSequence는 단조 증가 → 같은 sequence 두 번 없음

다른 세션 간:
  sessionSalt가 CSPRNG → 충돌 확률 ≈ 2^-96

같은 sequence라도 방향이 다르면:
  direction 비트가 다름 → 다른 Nonce
  (서버→클 sequence=5 ≠ 클→서버 sequence=5)
```

---

## 4. 패킷 내 암호화 범위

### 일반 데이터 패킷 (`isCorePacket = false`)

```
바이트 위치:   0    1  2  3    4      12     16      16+N    16+N+16
              ┌───┬─────┬────┬──────────┬──────────┬────────┬──────────┐
              │ H │PayLen│ T │ Sequence │ PacketId │Payload │ AuthTag  │
              │3B │     │1B │   8 B    │   4 B    │  N B   │  16 B    │
              └───┴─────┴────┴──────────┴──────────┴────────┴──────────┘
              ←────── AAD (12 bytes) ──────→
                                           ←── AES-GCM 암호화 ──────→
```

- **AAD 범위**: `[0..11]` = Header(3B) + PacketType(1B) + Sequence(8B)
- **암호화 범위**: `[16..]` = PacketId(4B) + Payload(NB) → 결과는 제자리(in-place)
- **AuthTag**: 암호화 완료 후 버퍼 끝에 추가 (16B)

### 코어 패킷 (`isCorePacket = true`)

```
               0    1  2  3    4      12     12+N    12+N+16
              ┌───┬─────┬────┬──────────┬─────────┬──────────┐
              │ H │PayLen│ T │ Sequence │ Payload │ AuthTag  │
              │3B │     │1B │   8 B    │  N B    │  16 B    │
              └───┴─────┴────┴──────────┴─────────┴──────────┘
              ←────── AAD (12 bytes) ──────→
                                           ← 암호화 ──────────→
```

- PacketId 필드 없음 (CONNECT, DISCONNECT, HEARTBEAT 등은 PacketId 불필요)
- **암호화 시작 오프셋**: `HEADER(3) + TYPE(1) + SEQ(8)` = 12B

### 오프셋 상수

```cpp
// CryptoType.h 또는 PacketCryptoHelper.h
constexpr size_t HEADER_SIZE     = df_HEADER_SIZE;         // 3 bytes
constexpr size_t PACKET_TYPE_SIZE = sizeof(PACKET_TYPE);   // 1 byte
constexpr size_t SEQUENCE_SIZE    = sizeof(PacketSequence); // 8 bytes
constexpr size_t PACKET_ID_SIZE   = sizeof(PacketId);       // 4 bytes
constexpr size_t AUTH_TAG_SIZE    = 16;                     // bytes

// 코어 패킷 body 시작 오프셋 (헤더 포함)
constexpr size_t bodyOffsetWithHeaderForCorePacket
    = HEADER_SIZE + PACKET_TYPE_SIZE + SEQUENCE_SIZE;      // 3+1+8 = 12

// 일반 패킷 body 시작 오프셋 (헤더 포함)
constexpr size_t bodyOffsetWithHeader
    = bodyOffsetWithHeaderForCorePacket + PACKET_ID_SIZE;  // 12+4 = 16
```

---

## 5. AAD (Additional Authenticated Data)

AAD는 **암호화하지 않지만** GCM 인증 태그 계산에 포함된다.  
수신 측에서 AAD가 조금이라도 변조되면 `DecryptAESGCM`이 `false`를 반환한다.

```
AAD = [HeaderCode 1B][PayloadLength 2B][PacketType 1B][PacketSequence 8B]
    = 총 12 bytes
```

**왜 Header와 PacketType을 AAD에 포함하는가?**

| 필드 | 포함 이유 |
|------|-----------|
| `HeaderCode` | 헤더 코드 변조 감지 (서버/클라이언트 프로토콜 식별자) |
| `PayloadLength` | 길이 필드 변조 감지 (버퍼 오버플로우 공격 방지) |
| `PacketType` | 타입 변조 감지 (SEND_TYPE을 CONNECT_TYPE으로 위장 방지) |
| `PacketSequence` | 재전송 공격 감지 (같은 시퀀스로 재생 불가) |

> `PacketSequence`가 Nonce에도 포함되고 AAD에도 포함되는 이유:  
> Nonce는 GCM의 암호화 기밀성에 사용되고,  
> AAD는 무결성 검증에 사용되어 두 역할이 다르다.

---

## 6. AuthTag (인증 태그, 16 bytes)

AES-128-GCM 기준 128비트 인증 태그. BCrypt는 기본적으로 128비트를 사용한다.

```cpp
// BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO 구조체
BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
authInfo.pbNonce    = nonce;
authInfo.cbNonce    = 12;
authInfo.pbTag      = tagBuffer;
authInfo.cbTag      = 16;   // AUTH_TAG_SIZE
authInfo.pbAuthData = aad;
authInfo.cbAuthData = aadSize;
authInfo.dwFlags    = 0;    // 암호화: 0, 복호화: BCRYPT_AUTH_MODE_CHAIN_CALLS_FLAG (연속 시)
```

**복호화 시 인증 실패(false) 반환 조건:**
- Nonce 불일치 (방향/시퀀스 오류)
- AAD 변조 (헤더/타입/시퀀스 조작)
- 페이로드 변조 (중간자 공격)
- AuthTag 변조 (태그 위조 시도)
- 재전송 공격 (같은 Nonce 재사용 — Nonce에 시퀀스 포함으로 방지)

---

## 7. 암호화 흐름 단계별

`RUDPSession::SendPacketImmediate()` → `PacketCryptoHelper::EncodePacket()` 호출 내부:

```
입력: NetBuffer (평문 페이로드 포함)
      packetSequence, direction, sessionSalt, sessionKeyHandle, isCorePacket

Step 1. 헤더 완성 (SetHeader)
  buffer[0] = HeaderCode
  buffer[1..2] = (uint16)(m_iWrite - HEADER_SIZE + AUTH_TAG_SIZE)
  m_iWriteLast = m_iWrite + AUTH_TAG_SIZE   ← AuthTag 공간 예약

Step 2. AAD 추출
  aad = buffer[0..11]

Step 3. Nonce 생성
  nonce = CryptoHelper::GenerateNonce(sessionSalt, 4, packetSequence, direction)
  // 12 bytes

Step 4. 암호화 범위 결정
  if isCorePacket:
    bodyStart = 12  (Header+Type+Seq)
  else:
    bodyStart = 16  (Header+Type+Seq+PacketId)
  bodySize = m_iWrite - bodyStart

Step 5. AES-GCM 암호화 (in-place)
  authTag[16] 버퍼 준비
  CryptoHelper::EncryptAESGCM(
    nonce.data(), 12,
    aad, 12,
    buffer[bodyStart], bodySize,   ← 평문 (수정됨)
    buffer[bodyStart], bodySize,   ← 암호문 (같은 위치에 덮어씀)
    authTag,
    sessionKeyHandle
  )

Step 6. AuthTag 추가
  memcpy(buffer[m_iWrite], authTag, 16)
  m_iWrite += 16   (= m_iWriteLast)

Step 7. 완료
  buffer.m_bIsEncoded = true
```

---

## 8. 복호화 흐름 단계별

`RUDPPacketProcessor::DECODE_PACKET()` 매크로 → `PacketCryptoHelper::DecodePacket()`:

```
입력: NetBuffer (암호문 + AuthTag 포함)
      PacketType은 이미 buffer에서 추출됨
      sessionSalt, sessionKeyHandle, isCorePacket, direction

Step 1. 최소 크기 검사
  if isCorePacket:
    minSize = TYPE(1) + SEQ(8) + AUTH_TAG_SIZE(16)
  else:
    minSize = TYPE(1) + SEQ(8) + PACKETID(4) + AUTH_TAG_SIZE(16)
  if buffer.GetUseSize() < minSize → return false

Step 2. PacketSequence 추출 (암호화 범위 밖)
  offset = HEADER_SIZE + PACKET_TYPE_SIZE  // = 4
  memcpy(&packetSequence, buffer[offset], 8)

Step 3. AuthTag 위치 계산
  authTagOffset = m_iWrite - AUTH_TAG_SIZE  // 버퍼 끝 16B

Step 4. body 범위 결정
  if isCorePacket:
    bodyStart = HEADER(3) + TYPE(1) + SEQ(8) = 12
  else:
    bodyStart = HEADER(3) + TYPE(1) + SEQ(8) + PACKETID(4) = 16
  bodySize = authTagOffset - bodyStart

Step 5. AAD 추출
  aad = buffer[0..11]

Step 6. Nonce 생성 (수신 측에서 동일하게 재현)
  nonce = CryptoHelper::GenerateNonce(sessionSalt, 4, packetSequence, direction)

Step 7. AES-GCM 복호화 + 태그 검증 (in-place)
  ok = CryptoHelper::DecryptAESGCM(
    nonce.data(), 12,
    aad, 12,
    buffer[bodyStart], bodySize,   ← 암호문
    authTag,                       ← 태그 검증
    buffer[bodyStart], bodySize,   ← 평문 (같은 위치에 복원)
    sessionKeyHandle
  )
  if !ok → return false  (인증 실패 → 패킷 폐기)

Step 8. 완료
  m_iWrite -= AUTH_TAG_SIZE   ← AuthTag 제거 (이미 검증됨)
  return true
```

---

## 9. 방향별 Nonce 분리 설계

같은 시퀀스 번호로 양방향에서 패킷이 생성될 수 있다.  
예: 서버 sequence=1 (S→C), 클라이언트 sequence=1 (C→S).

**Nonce의 direction 비트가 없으면:**
```
서버 sequence=1 Nonce = [salt] + [00 00 00 00 00 00 00 01]
클라이언트 sequence=1 Nonce = [salt] + [00 00 00 00 00 00 00 01]
→ 동일! → GCM 보안 붕괴
```

**direction 비트 포함 시:**
```
서버 S→C: Nonce[0] = (0b10 << 6) | (salt[0] & 0x3F)
클라이언트 C→S: Nonce[0] = (0b00 << 6) | (salt[0] & 0x3F)
→ 항상 다름
```

**4방향 분리 이유:**

| 방향 | 사용 상황 |
|------|-----------|
| `CLIENT_TO_SERVER (0)` | 클라이언트 → 서버 데이터 패킷 |
| `CLIENT_TO_SERVER_REPLY (1)` | 클라이언트 → 서버 ACK (SEND_REPLY_TYPE) |
| `SERVER_TO_CLIENT (2)` | 서버 → 클라이언트 데이터/하트비트 |
| `SERVER_TO_CLIENT_REPLY (3)` | 서버 → 클라이언트 ACK |

ACK와 데이터도 분리하는 이유: 같은 시퀀스로 ACK와 데이터가 동시에 존재 가능.  
(예: 서버가 sequence=5 데이터를 보내고, 클라이언트 sequence=5에 대한 ACK도 전송)

---

## 10. `isCorePacket` 플래그의 영향

| 속성 | `isCorePacket = false` | `isCorePacket = true` |
|------|------------------------|----------------------|
| PacketId 포함 | ✅ (4 bytes) | ❌ |
| 암호화 시작 오프셋 | 16 bytes | 12 bytes |
| 사용 패킷 | SEND_TYPE | CONNECT, DISCONNECT, HEARTBEAT, SEND_REPLY |
| 재전송 추적 | ✅ | HEARTBEAT만 ✅ (REPLY류는 ❌) |

---

## 11. AES-GCM 선택 이유

| 요구사항 | AES-GCM | 대안 |
|----------|---------|------|
| 기밀성 (Confidentiality) | ✅ AES-CTR 기반 암호화 | AES-CBC (패딩 필요, 속도 느림) |
| 무결성 (Integrity) | ✅ GMAC 인증 태그 | HMAC 별도 추가 필요 |
| 인증 (Authentication) | ✅ AAD 포함 | 별도 서명 필요 |
| 병렬 처리 | ✅ CTR 모드 특성 | CBC는 순차적 |
| 하드웨어 가속 | ✅ AES-NI + PCLMULQDQ | - |
| 재전송 공격 방지 | ✅ Nonce에 시퀀스 포함 | 별도 replay window 필요 |
| Windows BCrypt 지원 | ✅ BCRYPT_CHAIN_MODE_GCM | - |

---

## 12. C# 클라이언트와의 호환성

C# 클라이언트(`RudpSession_CS.cs`)는 `AesGcm` 클래스를 사용한다.

```csharp
// C# 클라이언트 AES-GCM 복호화
var nonce = GenerateNonce(sessionSalt, packetSequence, direction);
var aesGcm = new AesGcm(sessionKey, AES_TAG_SIZE);
aesGcm.Decrypt(
    nonce,          // 12 bytes
    ciphertext,     // 암호문
    tag,            // 16 bytes
    plaintext,      // 복호화 결과
    aad             // 12 bytes
);
```

**C++ ↔ C# 호환 시 주의사항:**

```
Nonce 생성 로직이 byte 단위로 동일해야 함:
  C++: nonce[11-i] = (packetSequence >> (8*i)) & 0xFF  (big-endian)
  C#:  동일 로직으로 구현해야 함 (BitConverter는 little-endian!)

sessionSalt 인덱스 범위:
  C++: sessionSalt[0..3]  (4 bytes만 사용)
  C#: 동일

direction enum 값:
  C++ CLIENT_TO_SERVER=0, CLIENT_TO_SERVER_REPLY=1, ...
  C#: 동일 순서로 정의
```

→ 불일치 시 `DecryptAESGCM`이 `false` 반환 → [[TroubleShooting]] `DecryptAESGCM 항상 false 반환` 참조

---

## 관련 문서
- [[CryptoHelper]] — BCrypt API 래퍼 상세
- [[PacketCryptoHelper]] — EncodePacket/DecodePacket 구현
- [[PacketFormat]] — 패킷 버퍼 레이아웃 전체
- [[RUDPSessionBroker]] — 세션 키/솔트 생성
- [[TLSHelper]] — 키 전달 채널 (TLS 1.2)
- [[TroubleShooting]] — Nonce 불일치 디버깅
