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
---

## 현재 코드 기준 함수 설명

문서명은 Packet Generator지만 실제 구현은 `Tool/PacketGenerator/PacketGenerator.py`에 있다.

#### `ToEnumName(name)`
- PascalCase 패킷 이름을 `PACKET_ID`용 UPPER_SNAKE_CASE로 변환한다.

#### `CopyPacketFiles()`
- 생성 대상 원본 파일을 `_new` 비교용 사본으로 복사한다.

#### `ReplacePacketFiled()`
- `_new` 파일들을 실제 결과 파일로 반영한다.

#### `ReplaceFile(originFile, newFile)`
- 내용이 달라진 경우에만 실제 파일을 교체한다.

#### `CopyServerGeneratedFileToClientPath()`
- 서버 쪽에서 생성된 `PacketIdType`, `Protocol.*`를 클라이언트 경로로 복사한다.

#### `CopyServerFileToClientFile(serverFilePath, clientFilePath)`
- 서버 생성 파일 하나를 클라이언트 대응 파일로 복사한다.

#### `DuplicateCheckAndAdd(packetDuplicateCheckerContainer, checkTarget)`
- 집합 기반 중복 검사를 수행한다.

#### `DuplicateCheckPacketItems(items, packetName)`
- 한 패킷 내부 필드 이름 중복을 검사한다.

#### `IsValidPacketTypeInYaml(yamlData)`
- YAML 정의에서 패킷 타입, 패킷 이름, 필드 이름 중복을 검증한다.

#### `GeneratePacketType(packetList)`
- `PACKET_ID` enum 헤더를 생성한다.

#### `MakePacketClasss(packetList)`
- `Protocol.h`에 들어갈 패킷 클래스 선언 코드를 만든다.

#### `GenerateProtocolHeader(packetList)`
- `Protocol.h`의 packet class 구간을 새 코드로 교체한다.

#### `GenerateInitInPacketHandlerCpp(packetList, originCode)`
- 패킷 핸들러 등록 함수 `Init()`에 필요한 등록 코드를 만든다.

#### `GenerateProtocolCpp(packetList)`
- `GetPacketId`, `BufferToPacket`, `PacketToBuffer` 구현을 생성한다.

#### `GeneratePacketHandlerCpp(packetList)`
- `PlayerPacketHandlerRegister.cpp`의 `Init()` 등록 코드를 갱신한다.

#### `ExtractExistingPlayerHandlers(player_cpp_path)`
- 기존 `Player.cpp`에 이미 구현된 `OnPacketName` 핸들러 이름을 추출한다.
- 현재 스크립트에는 동일 이름 함수가 두 번 정의되어 있지만, 최종 동작은 마지막 정의가 덮어쓴다.

#### `ExtractExistingPlayerHandlerDeclarations(player_header_path)`
- 기존 `Player.h`에 선언된 `OnPacketName(const Packet& packet)` 시그니처를 추출한다.

#### `GetReplyPacketName(request_name, packets)`
- 요청 패킷 이름으로부터 대응 reply 이름을 추론한다.
- 현재 스크립트에서는 일반 규칙과 `Ping`→`Pong` 예외를 처리한다.

#### `GeneratePlayerHandlerCode(packet, packets)`
- 새 request packet에 대한 빈 `Player::OnPacketName(...)` 구현 코드를 생성한다.

#### `GeneratePlayerPacketHandlerDeclarations(packetList)`
- `Player.h`의 `#pragma region Packet Handler` 구간에 새 핸들러 선언을 추가한다.

#### `GeneratePlayerPacketHandlers(packetList)`
- `Player.cpp`의 `#pragma region Packet Handler` 구간에 새 핸들러 구현을 추가한다.

#### `ProcessPacketGenerate()`
- YAML 로드, 검증, 원본 백업, 코드 생성, 파일 교체, 클라이언트 복사까지 전체 생성 파이프라인을 실행한다.
