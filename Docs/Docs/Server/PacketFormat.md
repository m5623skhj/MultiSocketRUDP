# 패킷 포맷 (Packet Format)

> **MultiSocketRUDP에서 네트워크를 오가는 패킷의 완전한 바이트 레이아웃.**  
> 어느 오프셋에 어떤 필드가 있는지, 암호화는 어디서부터 어디까지인지,  
> 각 PacketType의 페이로드 구성이 어떻게 되는지를 정리한다.

---

## 목차

1. [NetBuffer 내부 구조](#1-netbuffer-내부-구조)
2. [헤더 구조 (3 bytes)](#2-헤더-구조-3-bytes)
3. [일반 데이터 패킷 전체 레이아웃](#3-일반-데이터-패킷-전체-레이아웃)
4. [코어 패킷 레이아웃](#4-코어-패킷-레이아웃)
5. [PacketType별 페이로드 정의](#5-packettype별-페이로드-정의)
6. [PACKET_TYPE 열거형](#6-packet_type-열거형)
7. [PACKET_DIRECTION 열거형](#7-packet_direction-열거형)
8. [오프셋 상수 전체](#8-오프셋-상수-전체)
9. [직렬화 (`NetBuffer << / >>`)](#9-직렬화-netbuffer--)
10. [버퍼 크기 제한](#10-버퍼-크기-제한)

---

## 1. NetBuffer 내부 구조

패킷 데이터는 `NetBuffer` 객체 내부의 고정 배열에 저장된다.

```cpp
class NetBuffer {
public:
    char m_pSerializeBuffer[BUFFER_DEFAULT_SIZE];  // 최대 8192 bytes

    WORD m_iRead;       // 읽기 커서 (Serialize >> 연산이 여기서 읽음)
    WORD m_iWrite;      // 쓰기 커서 (Serialize << 연산이 여기에 씀)
    WORD m_iWriteLast;  // AuthTag 포함 실제 끝 위치
    bool m_bIsEncoded;  // EncodePacket 호출 여부 (중복 인코딩 방지)

    static BYTE m_byHeaderCode;  // 옵션 파일 PACKET_CODE
    static BYTE m_byXORCode;     // 옵션 파일 PACKET_KEY (사용 안 함)
};
```

**버퍼 커서 흐름:**

```
초기:  m_iRead=0, m_iWrite=0
<<:    m_iWrite += sizeof(T)  (데이터 기록)
>>:    m_iRead  += sizeof(T)  (데이터 읽기)
SetHeader 후: m_iWriteLast = m_iWrite + AUTH_TAG_SIZE
EncodePacket 후: m_iWrite = m_iWriteLast  (AuthTag 포함)
```

---

## 2. 헤더 구조 (3 bytes)

```
오프셋  크기   필드         값
─────────────────────────────────────
  0     1B    HeaderCode   NetBuffer::m_byHeaderCode (옵션 파일 PACKET_CODE)
  1     2B    PayloadLen   (m_iWrite - HEADER_SIZE + AUTH_TAG_SIZE) as uint16_t LE
─────────────────────────────────────
합계: df_HEADER_SIZE = 3 bytes
```

**PayloadLen 계산:**
```
PayloadLen = 총 패킷 크기 - 헤더(3B)
           = Type(1) + Sequence(8) + [PacketId(4)] + Payload(N) + AuthTag(16)
```

**SetHeader 구현:**
```cpp
static void PacketCryptoHelper::SetHeader(OUT NetBuffer& buf, int extraSize = 0) {
    buf.m_pSerializeBuffer[0] = NetBuffer::m_byHeaderCode;
    *reinterpret_cast<uint16_t*>(&buf.m_pSerializeBuffer[1])
        = static_cast<uint16_t>(buf.m_iWrite - df_HEADER_SIZE + extraSize);
    buf.m_iRead = 0;
    buf.m_iWriteLast = buf.m_iWrite + extraSize;
}
```

---

## 3. 일반 데이터 패킷 전체 레이아웃

`SEND_TYPE`, `SEND_REPLY_TYPE` (isCorePacket = false)

```
Byte
 0    ┌──────────────┐
      │ HeaderCode   │  1 B  (옵션 파일 설정값)
 1    ├──────────────┤
      │ PayloadLen   │  2 B  (uint16_t, little-endian)
 3    ├──────────────┤
      │ PacketType   │  1 B  (PACKET_TYPE enum)
 4    ├──────────────┤
      │              │
      │ PacketSeq    │  8 B  (uint64_t)
      │              │
12    ├──────────────┤  ← AES-GCM 암호화 시작
      │              │
      │ PacketId     │  4 B  (uint32_t)
      │              │
16    ├──────────────┤
      │              │
      │ Payload      │  N B  (콘텐츠 데이터)
      │              │
16+N  ├──────────────┤
      │              │
      │ AuthTag      │  16 B  (GCM 인증 태그)
      │              │
16+N+16 └──────────────┘

← AAD 범위: [0 .. 11] (Header 3B + PacketType 1B + Sequence 8B) →
←───────── AES-GCM 암호화 범위: [12 .. 16+N-1] ──────────────────→
```

---

## 4. 코어 패킷 레이아웃

`CONNECT_TYPE`, `DISCONNECT_TYPE`, `HEARTBEAT_TYPE`, `HEARTBEAT_REPLY_TYPE`  
(isCorePacket = true)

```
Byte
 0    ┌──────────────┐
      │ HeaderCode   │  1 B
 1    ├──────────────┤
      │ PayloadLen   │  2 B
 3    ├──────────────┤
      │ PacketType   │  1 B
 4    ├──────────────┤
      │ PacketSeq    │  8 B
12    ├──────────────┤  ← AES-GCM 암호화 시작
      │ Payload      │  N B  (PacketId 없음)
12+N  ├──────────────┤
      │ AuthTag      │  16 B
12+N+16 └──────────────┘

← AAD: [0 .. 11] →
← AES-GCM: [12 .. 12+N-1] →
```

---

## 5. PacketType별 페이로드 정의

### CONNECT_TYPE (C→S, isCorePacket=true)

```cpp
// RUDPClientCore::Start() 또는 RudpSession_CS.cs
NetBuffer buf;
auto connectType = PACKET_TYPE::CONNECT_TYPE;
PacketSequence seq = LOGIN_PACKET_SEQUENCE;  // 항상 0
buf << connectType << seq << sessionId;
// 직렬화: Type(1) + Seq(8) + SessionId(2) → 암호화 후 전송
```

```
페이로드:
  PacketSequence = 0    (항상 0, 최초 연결 시그널)
  SessionId      = (SessionBroker에서 수신한 값)
```

**서버 처리 (TryConnect):**
```cpp
recvPacket >> packetSequence;  // 0이어야 함
recvPacket >> recvSessionId;   // session.sessionId와 일치해야 함
```

---

### DISCONNECT_TYPE (C→S, isCorePacket=true)

```
페이로드: 없음 (PacketType + Sequence만)
```

---

### SEND_TYPE (양방향, isCorePacket=false)

```
페이로드: PacketId(4B) + 콘텐츠 패킷 직렬화 결과 (N bytes)
```

```cpp
// RUDPSession::SendPacket(IPacket&)
buf << SEND_TYPE << packetSequence << packet.GetPacketId();
packet.PacketToBuffer(buf);  // 콘텐츠 직렬화
// EncodePacket() → 전송
```

---

### SEND_REPLY_TYPE (양방향, isCorePacket=true)

```
페이로드:
  PacketSequence  (8B) = 확인하는 패킷 번호
  AdvertiseWindow (1B) = 수신 윈도우 여유 공간
```

```cpp
// RUDPSession::SendReplyToClient
buf << SEND_REPLY_TYPE << recvPacketSequence << advertiseWindow;

// 클라이언트 수신 시
recvPacket >> packetSequence;        // ACK 대상 시퀀스
recvPacket >> remoteAdvertisedWindow; // 서버 수신 윈도우
```

---

### HEARTBEAT_TYPE (S→C, isCorePacket=true)

```
페이로드: 없음 (PacketType + Sequence만)
```

서버가 보내는 시퀀스는 일반 데이터 패킷과 동일한 `lastSendPacketSequence`를 사용.

---

### HEARTBEAT_REPLY_TYPE (C→S, isCorePacket=true)

```
페이로드: 없음
```

클라이언트가 HEARTBEAT 수신 시 동일 시퀀스로 응답. 서버는 이를 `OnSendReply()`로 처리.

---

## 6. PACKET_TYPE 열거형

```cpp
enum class PACKET_TYPE : BYTE {
    CONNECT_TYPE          = 0x01,   // C→S  RUDP 연결 요청
    DISCONNECT_TYPE       = 0x02,   // C→S  정상 연결 해제
    SEND_TYPE             = 0x03,   // 양방향 데이터 전송 (현재 서버→클라이언트만 사용)
    SEND_REPLY_TYPE       = 0x04,   // 양방향 ACK + advertiseWindow
    HEARTBEAT_TYPE        = 0x05,   // S→C  생존 확인
    HEARTBEAT_REPLY_TYPE  = 0x06,   // C→S  하트비트 응답
};
```

**ProcessByPacketType에서 switch 분기:**
```cpp
switch (static_cast<PACKET_TYPE>(packetType)) {
case PACKET_TYPE::CONNECT_TYPE:         // DecodePacket(core=true, dir=C2S)
case PACKET_TYPE::DISCONNECT_TYPE:      // DecodePacket(core=true, dir=C2S)
case PACKET_TYPE::SEND_TYPE:            // DecodePacket(core=false, dir=C2S)
case PACKET_TYPE::SEND_REPLY_TYPE:      // DecodePacket(core=true, dir=C2S_REPLY)
case PACKET_TYPE::HEARTBEAT_REPLY_TYPE: // DecodePacket(core=true, dir=C2S_REPLY)
default: LOG_ERROR("Unknown type");
}
```

---

## 7. PACKET_DIRECTION 열거형

```cpp
enum class PACKET_DIRECTION : BYTE {
    CLIENT_TO_SERVER       = 0,  // 클라이언트 데이터 → 서버
    CLIENT_TO_SERVER_REPLY = 1,  // 클라이언트 ACK → 서버
    SERVER_TO_CLIENT       = 2,  // 서버 데이터 → 클라이언트
    SERVER_TO_CLIENT_REPLY = 3,  // 서버 ACK → 클라이언트
};
```

**각 PacketType에 대응하는 direction:**

| 방향 | PacketType | 암호화 주체 | 복호화 주체 |
|------|-----------|------------|------------|
| `CLIENT_TO_SERVER` | CONNECT, DISCONNECT, SEND_TYPE | 클라이언트 | 서버 |
| `CLIENT_TO_SERVER_REPLY` | SEND_REPLY_TYPE, HEARTBEAT_REPLY_TYPE | 클라이언트 | 서버 |
| `SERVER_TO_CLIENT` | SEND_TYPE, HEARTBEAT_TYPE | 서버 | 클라이언트 |
| `SERVER_TO_CLIENT_REPLY` | SEND_REPLY_TYPE | 서버 | 클라이언트 |

---

## 8. 오프셋 상수 전체

```cpp
// CryptoType.h / PacketCryptoHelper.h

// 개별 필드 크기
constexpr size_t df_HEADER_SIZE         = 3;   // HeaderCode(1) + PayloadLen(2)
constexpr size_t PACKET_TYPE_SIZE       = 1;   // sizeof(PACKET_TYPE)
constexpr size_t SEQUENCE_SIZE          = 8;   // sizeof(PacketSequence) = sizeof(uint64_t)
constexpr size_t PACKET_ID_SIZE         = 4;   // sizeof(PacketId) = sizeof(uint32_t)
constexpr size_t AUTH_TAG_SIZE          = 16;  // AES-GCM 인증 태그

// AAD 범위 (항상 12 bytes)
constexpr size_t AAD_SIZE = df_HEADER_SIZE + PACKET_TYPE_SIZE + SEQUENCE_SIZE; // = 12

// 코어 패킷 body 오프셋 (헤더 포함)
constexpr size_t bodyOffsetWithHeaderForCorePacket
    = df_HEADER_SIZE + PACKET_TYPE_SIZE + SEQUENCE_SIZE;  // = 12

// 일반 패킷 body 오프셋 (헤더 포함)
constexpr size_t bodyOffsetWithHeader
    = bodyOffsetWithHeaderForCorePacket + PACKET_ID_SIZE; // = 16

// 코어 패킷 body 오프셋 (m_iRead 기준, 헤더 제외)
constexpr size_t bodyOffsetWithNotHeaderForCorePacket
    = PACKET_TYPE_SIZE + SEQUENCE_SIZE;  // = 9

// 일반 패킷 body 오프셋 (m_iRead 기준)
constexpr size_t bodyOffsetWithNotHeader
    = bodyOffsetWithNotHeaderForCorePacket + PACKET_ID_SIZE; // = 13

// Crypto 시작 오프셋 (= df_HEADER_SIZE)
constexpr size_t CRYPTO_START_OFFSET = df_HEADER_SIZE; // = 3
```

---

## 9. 직렬화 (`NetBuffer << / >>`)

```cpp
// 쓰기 (직렬화)
template<typename T>
NetBuffer& operator<<(T value) {
    memcpy(&m_pSerializeBuffer[m_iWrite], &value, sizeof(T));
    m_iWrite += sizeof(T);
    return *this;
}

// std::string 특수화
NetBuffer& operator<<(const std::string& str) {
    uint16_t len = static_cast<uint16_t>(str.size());
    *this << len;
    memcpy(&m_pSerializeBuffer[m_iWrite], str.c_str(), len);
    m_iWrite += len;
    return *this;
}

// 읽기 (역직렬화)
template<typename T>
NetBuffer& operator>>(T& value) {
    memcpy(&value, &m_pSerializeBuffer[m_iRead], sizeof(T));
    m_iRead += sizeof(T);
    return *this;
}
```

**주의**: 엔디안 변환 없음. 서버/클라이언트 모두 같은 바이트 순서(little-endian, x86/x64)를 가정.  
ARM 또는 big-endian 플랫폼에서 클라이언트를 구현할 때는 명시적 변환 필요.

---

## 10. 버퍼 크기 제한

```cpp
// NetBuffer::BUFFER_DEFAULT_SIZE = 8192 bytes (내부 배열 크기)

// RIO recv 버퍼 (세션당 고정)
constexpr DWORD RECV_BUFFER_SIZE     = 1024 * 16;   // 16KB

// RIO send 버퍼 (세션당 고정, 배치 전송용)
constexpr DWORD MAX_SEND_BUFFER_SIZE = 1024 * 32;   // 32KB
```

**최대 단일 패킷 크기:**
```
Header(3) + Type(1) + Seq(8) + PacketId(4) + Payload(N) + AuthTag(16) <= 16384 (RECV_BUFFER_SIZE)
→ N <= 16384 - 32 = 16352 bytes
```

> 단일 패킷 페이로드가 16352 bytes를 초과하면 `RecvIOCompleted`의 `memcpy_s`에서 잘린다.  
> 대용량 데이터는 애플리케이션 레이어에서 분할 전송해야 한다.

---

## 관련 문서
- [[CryptoSystem]] — 암호화 범위 상세 설명
- [[PacketCryptoHelper]] — SetHeader, EncodePacket, DecodePacket 구현
- [[PacketProcessing]] — 수신 패킷 파싱 흐름
- [[RUDPSession]] — SendPacket 직렬화 과정
- [[RUDPClientCore]] — 클라이언트 측 직렬화
