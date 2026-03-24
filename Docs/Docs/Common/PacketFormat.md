# 패킷 포맷

> MultiSocketRUDP에서 사용하는 패킷의 바이트 레이아웃, 필드별 의미, 오프셋 계산.  
> 서버(C++)와 클라이언트(C#)가 동일한 포맷을 공유한다.

---

## 기본 헤더 구조 (`df_HEADER_SIZE = 3 bytes`)

```
Offset  Size  Field          설명
─────────────────────────────────────────────────────
  0      1B   HeaderCode     패킷 식별 코드. NetBuffer::m_byHeaderCode
                             옵션 파일 PACKET_CODE와 동일해야 함.
                             수신 측에서 0번 바이트로 패킷 시작 여부 확인.
  1      2B   PayloadLength  헤더 이후 페이로드의 바이트 수 (little-endian).
                             = m_iWrite - df_HEADER_SIZE + (암호화 시 AUTH_TAG_SIZE 추가)
─────────────────────────────────────────────────────
```

---

## 일반 데이터 패킷 (SEND_TYPE, isCorePacket=false)

```
Offset  Size  Field          암호화  설명
─────────────────────────────────────────────────────────────────────
  0      3B   Header         ─ AAD  HeaderCode + PayloadLength
  3      1B   PacketType     ─ AAD  PACKET_TYPE::SEND_TYPE (0x03)
  4      8B   Sequence       ─ AAD  uint64, 단조 증가. Nonce 생성에도 사용.
 12      4B   PacketId       ← ENC  uint32. PACKET_ID enum 값.
 16      NB   Payload        ← ENC  IPacket::PacketToBuffer()로 직렬화된 데이터.
 16+N   16B   AuthTag        (후미)  AES-GCM 인증 태그. 암호화 성공 시 버퍼 끝에 추가.
─────────────────────────────────────────────────────────────────────

AAD (Additional Authenticated Data) = Offset 0 ~ 11 (12 bytes)
  → 암호화되지 않지만 AuthTag 계산에 포함 → 위변조 감지
ENC (Encrypted) = Offset 12 ~ 16+N-1 (PacketId + Payload)
  → AES-128-GCM으로 in-place 암호화
```

---

## 코어 패킷 (isCorePacket=true)

CONNECT, DISCONNECT, SEND_REPLY, HEARTBEAT, HEARTBEAT_REPLY 패킷.  
PacketId 필드가 없다.

```
Offset  Size  Field          암호화  설명
─────────────────────────────────────────────────────────────────────
  0      3B   Header         ─ AAD
  3      1B   PacketType     ─ AAD  CONNECT=0x01, DISCONNECT=0x02 등
  4      8B   Sequence       ─ AAD
 12     NB   Payload        ← ENC  패킷 종류별 페이로드 (아래 참조)
 12+N  16B   AuthTag        (후미)
─────────────────────────────────────────────────────────────────────
```

---

## 패킷 타입별 페이로드

### CONNECT_TYPE (0x01) — 클라이언트→서버

```
Offset(페이로드 내)  Size  Field
───────────────────────────────
  0                   8B   Sequence (= LOGIN_PACKET_SEQUENCE = 0)
  8                   2B   SessionId (서버에서 발급받은 값)
───────────────────────────────
```

### DISCONNECT_TYPE (0x02) — 클라이언트→서버

```
Payload 없음 (헤더 + PacketType + Sequence + AuthTag만 존재)
```

### SEND_TYPE (0x03) — 양방향 데이터

```
(위 일반 데이터 패킷 참조 — PacketId + Payload 포함)
```

### SEND_REPLY_TYPE (0x04) — ACK

```
Offset(페이로드 내)  Size  Field
───────────────────────────────
  0                   8B   AckedSequence (ACK할 시퀀스 번호)
  8                   1B   AdvertiseWindow (수신 윈도우 남은 공간)
───────────────────────────────
```

### HEARTBEAT_TYPE (0x05) — 서버→클라이언트

```
(SEND_TYPE과 동일 구조, isCorePacket=true이므로 PacketId 없음)
Payload: 없음 (Sequence만 있으면 충분)
```

### HEARTBEAT_REPLY_TYPE (0x06) — 클라이언트→서버

```
(SEND_REPLY_TYPE과 동일 구조로 처리)
```

---

## PACKET_TYPE 열거형 값 정리

```cpp
enum class PACKET_TYPE : unsigned char {
    INVALID_TYPE        = 0x00,
    CONNECT_TYPE        = 0x01,  // 연결 요청 (C→S)
    DISCONNECT_TYPE     = 0x02,  // 연결 해제 (C→S)
    SEND_TYPE           = 0x03,  // 데이터 전송 (양방향)
    SEND_REPLY_TYPE     = 0x04,  // ACK (양방향)
    HEARTBEAT_TYPE      = 0x05,  // 생존 확인 (S→C)
    HEARTBEAT_REPLY_TYPE= 0x06,  // 생존 확인 응답 (C→S)
};
```

---

## PACKET_DIRECTION 열거형

방향 정보는 Nonce 생성 시 상위 2비트로 인코딩된다.

```cpp
enum class PACKET_DIRECTION : uint8_t {
    CLIENT_TO_SERVER        = 0,  // 0b00 — 클라이언트 송신 데이터
    CLIENT_TO_SERVER_REPLY  = 1,  // 0b01 — 클라이언트 ACK
    SERVER_TO_CLIENT        = 2,  // 0b10 — 서버 송신 데이터
    SERVER_TO_CLIENT_REPLY  = 3,  // 0b11 — 서버 ACK
    INVALID                 = 255
};
```

---

## 오프셋 상수 (PacketCryptoHelper)

```cpp
// 패킷 구조를 이해하는 모든 오프셋은 여기에 집중
static const unsigned int bodyOffsetWithHeader =
    df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);
// = 3 + 1 + 8 + 4 = 16 (일반 패킷, 암호화 시작 위치, 헤더 포함)

static const unsigned int bodyOffsetWithHeaderForCorePacket =
    df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
// = 3 + 1 + 8 = 12 (코어 패킷, 암호화 시작 위치)

static const unsigned int bodyOffsetWithNotHeader =
    sizeof(PACKET_TYPE) + sizeof(PacketSequence) + sizeof(PacketId);
// = 1 + 8 + 4 = 13 (일반 패킷, 헤더 제외 m_iRead 기준)

static const unsigned int bodyOffsetWithNotHeaderForCorePacket =
    sizeof(PACKET_TYPE) + sizeof(PacketSequence);
// = 1 + 8 = 9 (코어 패킷, 헤더 제외)
```

---

## AAD 범위 정의

```cpp
// PacketCryptoHelper::EncodePacket / DecodePacket
constexpr size_t aadSize = df_HEADER_SIZE + sizeof(PACKET_TYPE) + sizeof(PacketSequence);
// = 3 + 1 + 8 = 12 bytes

const unsigned char* aad = reinterpret_cast<const unsigned char*>(packet.m_pSerializeBuffer);
// → 버퍼의 첫 12 바이트 = Header(3) + PacketType(1) + Sequence(8)
```

**AAD를 포함한 인증의 의미:**
- HeaderCode, PayloadLength, PacketType, Sequence는 평문이지만 위변조 불가
- 공격자가 Sequence를 바꾸면 AuthTag 검증 실패 → 재전송 공격 방지
- PacketType을 바꾸면 AuthTag 검증 실패 → 타입 위조 방지

---

## NetBuffer에서의 패킷 작성 순서

### 서버 측 (C++)

```cpp
// 1. 데이터 직렬화 (페이로드 부분만)
NetBuffer& buf = *NetBuffer::Alloc();
buf << packetType;       // WriteBuffer(1B)
buf << packetSequence;   // WriteBuffer(8B)
buf << packet.GetPacketId(); // WriteBuffer(4B)
packet.PacketToBuffer(buf);  // 콘텐츠 데이터

// 2. 암호화 + 헤더 완성
PacketCryptoHelper::EncodePacket(
    buf, packetSequence, direction,
    sessionSalt, SESSION_SALT_SIZE,
    sessionKeyHandle, isCorePacket);
// → SetHeader() 호출 (HeaderCode + PayloadLength 기록)
// → EncryptAESGCM() 호출 (bodyOffset ~ end 암호화)
// → WriteBuffer(authTag, 16) 호출 (버퍼 끝에 추가)
```

### 클라이언트 측 (C#)

```csharp
// NetBuffer.InsertXxx 메서드들이 역순 삽입 (Array.Copy로 앞에 끼워넣기)
buffer.InsertPacketType(PacketType.SendType);     // 앞에 1B 삽입
buffer.InsertPacketSequence(sequence);            // 앞에 8B 삽입
buffer.InsertPacketId(packetId);                  // 앞에 4B 삽입
NetBuffer.EncodePacket(aesGcm, buffer, sequence, direction, salt, false);
```

---

## 실제 패킷 바이트 예시

PING 패킷 (PingPacket, 페이로드 없음, PacketId=1):

```
암호화 전:
Offset  Hex   설명
  0     89    HeaderCode (PACKET_CODE=0x89)
  1     16    PayloadLength 하위 바이트 (22 = 0x16)
  2     00    PayloadLength 상위 바이트
  3     03    PacketType = SEND_TYPE
  4-11  01 00 00 00 00 00 00 00  Sequence = 1
 12-15  01 00 00 00  PacketId = 1 (PING)
(페이로드 없음)

암호화 후 (PacketId 이후 암호화):
  0     89
  1     26    PayloadLength = 22 + 16(AuthTag) = 38 = 0x26
  2     00
  3     03    (AAD - 평문)
  4-11  01 00 00 00 00 00 00 00  (AAD - 평문)
 12-15  XX XX XX XX  (암호화된 PacketId)
 16-31  XX...XX  (AuthTag 16B)

총 크기: 3 + 1 + 8 + 4 + 16 = 32 bytes
```

---

## 관련 문서
- [[CryptoSystem]] — Nonce 생성, AES-GCM 암호화
- [[PacketCryptoHelper]] — EncodePacket / DecodePacket 구현
- [[PacketGenerator]] — 패킷 클래스 자동 생성
- [[PacketProcessing]] — 수신 시 파싱 흐름
