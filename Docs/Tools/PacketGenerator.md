# 패킷 코드 자동 생성기 (Packet Generator)

> YAML 정의 파일로부터 C++ 패킷 클래스, 핸들러 등록 코드, Player 핸들러 스텁을 자동 생성한다.

---

## 흐름 개요

```
PacketDefine.yml
      │
      ▼
PacketGenerator.py
      │
      ├──► PacketIdType.h          ← PACKET_ID enum
      ├──► Protocol.h              ← 패킷 클래스 정의
      ├──► Protocol.cpp            ← GetPacketId() / BufferToPacket() 등
      ├──► PlayerPacketHandlerRegister.cpp  ← Init() 등록 코드
      ├──► Player.h                ← 핸들러 선언 (증분 추가)
      └──► Player.cpp              ← 핸들러 스텁 (증분 추가)
                │
                └──► 클라이언트 측 복사 (PacketIdType.h, Protocol.*)
```

---

## PacketDefine.yml 문법

```yaml
Packet:
  - Type: RequestPacket
    PacketName: Ping
    Desc: 클라이언트→서버 핑

  - Type: ReplyPacket
    PacketName: Pong
    Desc: 서버→클라이언트 퐁

  - Type: RequestPacket
    PacketName: TestPacketReq
    Items:
      - Type: int
        Name: order
      - Type: std::string
        Name: message
```

| 필드 | 설명 |
|------|------|
| `Type` | `RequestPacket` (C→S) 또는 `ReplyPacket` (S→C) |
| `PacketName` | 클래스명 (PascalCase) |
| `Items` | 직렬화 필드 목록 (없으면 생략) |

---

## 생성 결과 예시

### PacketIdType.h
```cpp
enum class PACKET_ID : unsigned int {
    INVALID_PACKET_ID = 0
    , PING
    , PONG
    , TEST_PACKET_REQ
};
```

### Protocol.h (패킷 클래스)
```cpp
class TestPacketReq final : public IPacket {
public:
    [[nodiscard]] PacketId GetPacketId() const override;
    void BufferToPacket(NetBuffer& buffer) override;
    void PacketToBuffer(NetBuffer& buffer) override;
public:
    int order;
    std::string message;
};
```

### Player.h (핸들러 선언 자동 추가)
```cpp
#pragma region Packet Handler
public:
    void OnPing(const Ping& packet);
    void OnTestPacketReq(const TestPacketReq& packet);
#pragma endregion Packet Handler
```

---

## 증분 업데이트 방식

- 파일을 `_new` 사본으로 수정 후 `filecmp`로 비교, 변경 없으면 교체 안 함
- `Player.cpp` / `Player.h`는 **기존 핸들러를 보존하고 신규 패킷만 추가**
- `#pragma region Packet Handler` 블록 사이에 삽입

---

## 실행 방법

```batch
Tool\PacketGenerate.bat             # 코드 생성만
Tool\PacketGenerateAndUploader.bat  # 생성 + Google Sheets 업로드
```

---

## Google Sheets 업로드

`Tool/PacketUploader/config.json`:
```json
{
    "spreadsheet_id": "구글_시트_ID",
    "sheet_name": "PacketDefine",
    "yaml_file": "..\\PacketDefine.yml",
    "auth_file": "credentials.json"
}
```

---

## 관련 문서
- [[PacketProcessing]] — 생성된 패킷이 처리되는 방식
- [[RUDPSession]] — RegisterPacketHandler 사용
- [[PacketFormat]] — 패킷 구조
