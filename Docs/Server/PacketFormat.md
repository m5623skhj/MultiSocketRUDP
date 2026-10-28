# 패킷 포맷 (Packet Format)

> **MultiSocketRUDP에서 네트워크를 오가는 패킷의 완전한 바이트 레이아웃.**  
> 어느 오프셋에 어떤 필드가 있는지, 암호화는 어디서부터 어디까지인지,  
> 각 PacketType의 페이로드 구성이 어떻게 되는지를 정리한다.

---

## 목차

1. [NetBuffer 내부 구조](#1-netbuffer-내부-구조)
2. [헤더 구조 (5 bytes)](#2-헤더-구조-5-bytes)
3. [일반 데이터 패킷 전체 레이아웃](#3-일반-데이터-패킷-전체-레이아웃)
4. [코어 패킷 레이아웃](#4-코어-패킷-레이아웃)
5. [PacketType별 페이로드 정의](#5-packettype별-페이로드-정의)
6. [PACKET_TYPE 열거형](#6-packet_type-열거형)
7. [PACKET_DIRECTION 열거형](#7-packet_direction-열거형)
8. [오프셋 상수 전체](#8-오프셋-상수-전체)
9. [직렬화 (`NetBuffer << / >>`)](#9-직렬화-netbuffer)
10. [버퍼 크기 제한](#10-버퍼-크기-제한)

---

## 1. NetBuffer 내부 구조

패킷 데이터는 `NetBuffer`가 소유한 동적 버퍼에 저장된다. 기본 용량은 1024바이트이며 `Resize()`로 최대 16384바이트까지 확장할 수 있다.

```cpp
// 내부 배치 설명용 요약이다. 실제 NetBuffer 별칭의 해당 필드는 private이며 friend만 직접 접근한다.
class NetBuffer {
private:
    char* m_pSerializeBuffer;  // 기본 1024 bytes, BUFFFER_MAX=16384

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
초기:  m_iRead=5, m_iWrite=5 (헤더 공간을 건너뜀)
<<:    m_iWrite += sizeof(T)  (데이터 기록)
>>:    m_iRead  += sizeof(T)  (데이터 읽기)
SetHeader 후: m_iWriteLast = m_iWrite + AUTH_TAG_SIZE
EncodePacket 후: m_iWrite = m_iWriteLast  (AuthTag 포함)
```

---

## 2. 헤더 구조 (5 bytes)

```
오프셋  크기   필드         값
─────────────────────────────────────
  0     1B    HeaderCode   NetBuffer::m_byHeaderCode (옵션 파일 PACKET_CODE)
  1     2B    PayloadLen   (m_iWrite - HEADER_SIZE + AUTH_TAG_SIZE) as uint16_t LE
  3     2B    Reserved     레거시 RandCode/Checksum 위치
─────────────────────────────────────
합계: df_HEADER_SIZE = 5 bytes
```

**PayloadLen 계산:**
```
PayloadLen = 총 패킷 크기 - 헤더(5B)
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

`SEND_TYPE` (isCorePacket = false)

```
Byte
 0      ┌──────────────┐
        │ HeaderCode   │  1 B  (옵션 파일 설정값)
 1      ├──────────────┤
        │ PayloadLen   │  2 B  (uint16_t, little-endian)
 3      ├──────────────┤
        │ Reserved     │  2 B
 5      ├──────────────┤
        │ PacketType   │  1 B  (PACKET_TYPE enum)
 6      ├──────────────┤
        │              │
        │ PacketSeq    │  8 B  (uint64_t)
        │              │
14      ├──────────────┤  ← PacketId는 평문·AAD 제외
        │              │
        │ PacketId     │  4 B  (uint32_t)
        │              │
18      ├──────────────┤  ← 일반 패킷 AES-GCM 암호화 시작
        │              │
        │ Payload      │  N B  (콘텐츠 데이터)
        │              │
18+N    ├──────────────┤
        │              │
        │ AuthTag      │  16 B  (GCM 인증 태그)
        │              │
18+N+16 └──────────────┘

← AAD 범위: [0 .. 13] (Header 5B + PacketType 1B + Sequence 8B) →
←───────── AES-GCM 암호화 범위: [18 .. 18+N-1] ──────────────────→
```

---

## 4. 코어 패킷 레이아웃

`CONNECT_TYPE`, `DISCONNECT_TYPE`, `HEARTBEAT_TYPE`, `HEARTBEAT_REPLY_TYPE`  
(isCorePacket = true)

```
Byte
 0      ┌──────────────┐
        │ HeaderCode   │  1 B
 1      ├──────────────┤
        │ PayloadLen   │  2 B
 3      ├──────────────┤
        │ Reserved     │  2 B
 5      ├──────────────┤
        │ PacketType   │  1 B
 6      ├──────────────┤
        │ PacketSeq    │  8 B
14      ├──────────────┤  ← AES-GCM 암호화 시작
        │ Payload      │  N B  (PacketId 없음)
14+N    ├──────────────┤
        │ AuthTag      │  16 B
14+N+16 └──────────────┘

← AAD: [0 .. 13] →
← AES-GCM: [14 .. 14+N-1] →
```

---

## 5. PacketType별 페이로드 정의

### CONNECT_TYPE (C→S, isCorePacket=true)

```cpp
// RUDPClientCore::Start() 또는 BotTester ClientCore/RUDPSession.cs
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

### UNRELIABLE_SEND_TYPE (양방향, isCorePacket=false)

`SEND_TYPE`과 동일한 `PacketId(4B) + 콘텐츠 payload` 레이아웃을 사용한다. 차이는 채널 계약과 nonce 방향이다.

- C→S: `CLIENT_TO_SERVER_UNREL`
- S→C: `SERVER_TO_CLIENT_UNREL`
- ACK·재전송·advertise window·누락 번호 대기 없음

---

### SEND_REPLY_TYPE (양방향, isCorePacket=true)

공통 `PacketSequence` 8바이트가 ACK 대상 번호다. 서버→클라이언트에는 암호화 본문으로 `AdvertiseWindow` 1바이트가 추가되며, 클라이언트→서버에는 본문이 없다. 헤더와 인증 태그를 포함한 전체 크기는 각각 31바이트와 30바이트다.

```cpp
// RUDPSession::SendReplyToClient
buf << SEND_REPLY_TYPE << recvPacketSequence << advertiseWindow;

// 클라이언트 수신 시
recvPacket >> packetSequence;        // ACK 대상 시퀀스
recvPacket >> remoteAdvertisedWindow; // 서버 수신 윈도우

// 클라이언트→서버 ACK 작성: AdvertiseWindow 없음
buf << PACKET_TYPE::SEND_REPLY_TYPE << recvPacketSequence;
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

클라이언트가 HEARTBEAT 수신 시 동일 시퀀스로 응답한다. C++ 클라이언트는 `HEARTBEAT_REPLY_TYPE`, C# BotTester는 `SEND_REPLY_TYPE`을 사용하며 둘 다 `AdvertiseWindow`는 없다. 서버는 두 타입 모두 `OnSendReply()`로 처리한다.

---

## 6. PACKET_TYPE 열거형

```cpp
enum class PACKET_TYPE : BYTE {
    CONNECT_TYPE          = 0x01,   // C→S  RUDP 연결 요청
    DISCONNECT_TYPE       = 0x02,   // C→S  정상 연결 해제
    SEND_TYPE             = 0x03,   // 양방향 신뢰성 데이터 전송
    SEND_REPLY_TYPE       = 0x04,   // 양방향 ACK, 서버→클라이언트에만 advertiseWindow 추가
    HEARTBEAT_TYPE        = 0x05,   // S→C  생존 확인
    HEARTBEAT_REPLY_TYPE  = 0x06,   // C→S  하트비트 응답
    UNRELIABLE_SEND_TYPE  = 0x07,   // 양방향 비신뢰성 데이터 전송
};
```

**ProcessByPacketType에서 switch 분기:**
```cpp
switch (static_cast<PACKET_TYPE>(packetType)) {
case PACKET_TYPE::CONNECT_TYPE:         // DecodePacket(core=true, dir=C2S)
case PACKET_TYPE::DISCONNECT_TYPE:      // DecodePacket(core=true, dir=C2S)
case PACKET_TYPE::SEND_TYPE:            // DecodePacket(core=false, dir=C2S)
case PACKET_TYPE::UNRELIABLE_SEND_TYPE: // DecodePacket(core=false, dir=C2S_UNREL)
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
    CLIENT_TO_SERVER_UNREL = 4,  // 클라이언트 비신뢰성 데이터 → 서버
    SERVER_TO_CLIENT_UNREL = 5,  // 서버 비신뢰성 데이터 → 클라이언트
    INVALID                = 255,
};
```

**각 PacketType에 대응하는 direction:**

| 방향 | PacketType | 암호화 주체 | 복호화 주체 |
|------|-----------|------------|------------|
| `CLIENT_TO_SERVER` | CONNECT, DISCONNECT, SEND_TYPE | 클라이언트 | 서버 |
| `CLIENT_TO_SERVER_REPLY` | SEND_REPLY_TYPE, HEARTBEAT_REPLY_TYPE | 클라이언트 | 서버 |
| `SERVER_TO_CLIENT` | SEND_TYPE, HEARTBEAT_TYPE | 서버 | 클라이언트 |
| `SERVER_TO_CLIENT_REPLY` | SEND_REPLY_TYPE | 서버 | 클라이언트 |
| `CLIENT_TO_SERVER_UNREL` | UNRELIABLE_SEND_TYPE | 클라이언트 | 서버 |
| `SERVER_TO_CLIENT_UNREL` | UNRELIABLE_SEND_TYPE | 서버 | 클라이언트 |

---

## 8. 오프셋 상수 전체

```cpp
// Common/etc/CoreConstants.h / PacketCryptoHelper.h

// 개별 필드 크기
constexpr size_t df_HEADER_SIZE         = 5;   // HeaderCode(1) + PayloadLen(2) + Reserved(2)
constexpr size_t PACKET_TYPE_SIZE       = 1;   // sizeof(PACKET_TYPE)
constexpr size_t SEQUENCE_SIZE          = 8;   // sizeof(PacketSequence) = sizeof(uint64_t)
constexpr size_t PACKET_ID_SIZE         = 4;   // sizeof(PacketId) = sizeof(uint32_t)
constexpr size_t AUTH_TAG_SIZE          = 16;  // AES-GCM 인증 태그

// AAD 범위 (항상 14 bytes)
constexpr size_t AAD_SIZE = df_HEADER_SIZE + PACKET_TYPE_SIZE + SEQUENCE_SIZE; // = 14

// 코어 패킷 body 오프셋 (헤더 포함)
constexpr size_t bodyOffsetWithHeaderForCorePacket
    = df_HEADER_SIZE + PACKET_TYPE_SIZE + SEQUENCE_SIZE;  // = 14

// 일반 패킷 body 오프셋 (헤더 포함)
constexpr size_t bodyOffsetWithHeader
    = bodyOffsetWithHeaderForCorePacket + PACKET_ID_SIZE; // = 18

// 코어 패킷 body 오프셋 (m_iRead 기준, 헤더 제외)
constexpr size_t bodyOffsetWithNotHeaderForCorePacket
    = PACKET_TYPE_SIZE + SEQUENCE_SIZE;  // = 9

// 일반 패킷 body 오프셋 (m_iRead 기준)
constexpr size_t bodyOffsetWithNotHeader
    = bodyOffsetWithNotHeaderForCorePacket + PACKET_ID_SIZE; // = 13

// Crypto 시작 오프셋 (= df_HEADER_SIZE)
constexpr size_t CRYPTO_START_OFFSET = df_HEADER_SIZE; // = 5
```

---

## 9. 직렬화 (`NetBuffer`)

기본 숫자 타입은 기존 `WriteBuffer`/`ReadBuffer`로 고정 크기 값을 기록한다. 생성된 사용자 정의 구조체는 객체 메모리나 padding을 복사하지 않고 `NetBufferCodec<T>`가 YAML 필드 순서대로 재귀 처리한다.

```cpp
template<>
struct NetBufferCodec<Item>
{
    static void Write(NetBuffer& buffer, const Item& value)
    {
        buffer.WriteValue(value.dataId);
        buffer.WriteValue(value.itemId);
    }

    static void Read(NetBuffer& buffer, Item& value)
    {
        Item temporary{};
        buffer.ReadValue(temporary.dataId);
        buffer.ReadValue(temporary.itemId);
        value = std::move(temporary);
    }
};

void DeleteItem::PacketToBuffer(NetBuffer& buffer)
{
    buffer.WriteValue(target); // Item codec → dataId → itemId
    buffer.WriteValue(reason);
}
```

컨테이너도 동일한 `WriteValue`/`ReadValue` 경로를 사용한다.

| 타입 | wire 형식 | 순서 계약 |
|---|---|---|
| `vector<T>` | `uint32 count` + 각 원소 | vector 원소 순서 보존 |
| `list<T>` | `size_t count` + 각 원소 | list 원소 순서 보존 |
| `set<T>` | 정렬 방향 1B + `uint32 count` + 원소 | `less`/`greater` 방향 검사 |
| `map<K,V>` | 정렬 방향 1B + `uint32 count` + key/value | `less`/`greater` 방향 검사 |
| `unordered_set<T>` | `uint32 count` + 원소 | 삽입·순회·bucket 순서 비보장 |
| `unordered_map<K,V>` | `uint32 count` + key/value | 삽입·순회·bucket 순서 비보장 |

구조체와 컨테이너는 수신 임시 값에 전부 읽은 뒤 성공할 때 대상에 반영한다. 실패하면 대상 값은 유지되지만 버퍼 read cursor는 복구하지 않으므로 실패한 패킷을 폐기해야 한다. 비정렬 컨테이너는 같은 데이터를 복원해도 프로세스나 실행마다 직렬화 바이트 순서가 달라질 수 있다.

**주의**: 엔디안 변환 없음. 서버/클라이언트 모두 같은 바이트 순서(little-endian, x86/x64)를 가정.  
ARM 또는 big-endian 플랫폼에서 클라이언트를 구현할 때는 명시적 변환 필요.

---

## 10. 버퍼 크기 제한

```cpp
// NetBuffer: dfDEFAULTSIZE=1024 bytes, BUFFFER_MAX=16384 bytes

// RIO recv 버퍼 (세션당 고정)
constexpr DWORD RECV_BUFFER_SIZE     = 1024 * 16;   // 16KB

// RIO send 버퍼 (세션당 고정, 배치 전송용)
constexpr DWORD MAX_SEND_BUFFER_SIZE = 1024 * 32;   // 32KB
```

**최대 단일 패킷 크기:**
```
Header(5) + Type(1) + Seq(8) + PacketId(4) + Payload(N) + AuthTag(16) <= 1024 (MAX_WIRE_PACKET_SIZE)
→ N <= 1024 - 34 = 990 bytes
```

> 문자열 길이·컨테이너 개수 필드도 990바이트 본문에 포함한다. 초과 송신은 시퀀스 발급 전에 거절한다.
> 여러 프레임을 묶는 서버 UDP 데이터그램도 1024바이트 이하이며 32KB 송신 버퍼와 16KB 수신 버퍼는 할당 용량이다.
> 대용량 데이터는 애플리케이션 레이어에서 분할 전송해야 한다.

---

> **현재 구현의 보안 한계:** 일반 콘텐츠 패킷의 `PacketId` 4바이트는 암호화와 AAD 인증 범위에서 모두 제외된다. 본문 인증 성공만으로 패킷 ID의 무결성을 보장할 수 없다. C++ `PacketCryptoHelper.h`와 C# `NetBuffer.cs`가 같은 범위를 사용한다. 프로토콜 개선 시 양쪽 구현과 공용 벡터를 함께 검토해야 한다.

## 관련 문서
- [[CryptoSystem]] — 암호화 범위 상세 설명
- [[PacketCryptoHelper]] — SetHeader, EncodePacket, DecodePacket 구현
- [[PacketProcessing]] — 수신 패킷 파싱 흐름
- [[RUDPSession]] — SendPacket 직렬화 과정
- [[RUDPClientCore]] — 클라이언트 측 직렬화
- [[ContainerSerialization]] — 컨테이너 wire 형식과 실패 처리
- [[PacketGenerator]] — YAML 구조체·컨테이너 정의
