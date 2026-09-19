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

### 사용자 정의 데이터 구조체

`Structs`에 데이터 타입을 선언하고 패킷의 `Items.Type`에서 이름으로 참조한다.
YAML 선언 순서는 자유롭다. 아래에서는 `UserData`가 자신이 참조하는 `Position`보다 먼저 나온다.

```yaml
Structs:
  - Name: UserData
    Items:
      - Type: std::uint64_t
        Name: userId
      - Type: Position
        Name: position
      - Type: std::vector<int>
        Name: items
  - Name: Position
    Items:
      - Type: float
        Name: x
      - Type: float
        Name: y

Packet:
  - Type: ReplyPacket
    PacketName: UserListRes
    Items:
      - Type: std::vector<UserData>
        Name: users
```

생성기는 전체 이름과 필드 타입을 먼저 검증하고, 의존 그래프를 위상 정렬하여
`Position`, `UserData` 순서로 완전한 구조체 정의를 출력한다. 이어서 모든
`NetBufferCodec<T>` 선언과 구현을 출력하고 마지막으로 패킷 클래스를 생성한다.
데이터 구조체는 값 멤버만 가지며 `IPacket` 상속, 패킷 ID, 핸들러를 생성하지 않는다.
패킷 ID의 순서는 기존처럼 `Packet` 목록 순서를 따른다.

- 필드는 YAML 순서로 재귀 직렬화한다. 이름이나 타입 메타데이터는 전송하지 않는다.
- `vector`, `list`, `set`, `map`, `unordered_set`, `unordered_map`과 중첩 컨테이너를 지원한다. 원소 또는 map 값으로 생성 구조체를 사용할 수 있다.
- `vector`와 `list`는 원소 순서를 보존한다. `unordered_set`과 `unordered_map`은 데이터만 복원하며 삽입 순서, 순회 순서, bucket 상태를 보존하지 않는다. 같은 데이터라도 직렬화 바이트 순서는 달라질 수 있다.
- `map/set`의 키/원소는 기본 타입 또는 문자열만 허용한다. 정렬 비교자는
  `std::less<Key>` 또는 `std::greater<Key>`를 선택할 수 있다.
- 포인터, 참조, 임의 C++ 타입, 패킷 타입을 데이터 필드로 사용하는 것은 거부한다.
- 미정의 타입, 중복 이름, 직접 순환 및 컨테이너를 통한 순환은 생성 전에 거부한다.
  예: `Struct dependency cycle: A -> B -> A`.
- 빈 구조체와 빈 패킷을 지원한다. 빈 구조체는 전송 바이트가 없다.
- 구조체와 패킷은 임시 값으로 역직렬화한 뒤 성공하면 반영한다. 실패한 버퍼는 폐기한다.
- 송수신 양쪽은 같은 스키마를 사용해야 한다. 필드 추가·삭제·순서·타입 변경은 전송 형식 변경이다.
- 직렬화 중 원본 구조체/컨테이너 변경 및 같은 버퍼의 동시 접근은 호출자가 방지해야 한다.

생성 영역은 `// BEGIN GENERATED PACKET TYPES`부터 `// END GENERATED PACKET TYPES`까지다.
기존 `#pragma pack(push, 1)` 영역은 첫 재생성 시 이 마커로 교체한다. 생성 타입은
정상 정렬을 사용하며 객체 패딩을 전송하지 않는다. 객체의 `sizeof`/정렬은 바뀔 수 있다.
**생성 영역 안의 수동 작성 클래스는 재생성 시 보존되지 않으므로 먼저 YAML로 옮기거나
별도 파일로 분리해야 한다.** 현재 서버의 `ChannelEchoReq/Res`가 이에 해당한다.

스키마 오류는 템플릿 생성이나 `_new` 복사 전에 검출하며 기존 생성 파일을 수정하지 않는다.
데이터 구조체 코드는 `Protocol.h`에 포함되므로 기존 서버→클라이언트 복사 흐름을 그대로 사용한다.

검증 예제는 `Tool/PacketGenerator/tests/Structs.yml`이고 생성 결과는
`CoreTest/GeneratedPacketSchema`에 있다. PyYAML 설치 후 저장소 루트에서 실행한다.

```powershell
python MultiSocketRUDP/Tool/PacketGenerator/tests/test_packet_schema.py -v
# 예제 스키마나 생성 코드를 바꾼 경우 검증용 C++ 파일 갱신
python MultiSocketRUDP/Tool/PacketGenerator/tests/test_packet_schema.py --write-fixture
```

`CoreTest`의 `GeneratedPacketSchemaTest.*`는 이 생성 결과를 실제 컴파일하여
구조체/패킷 왕복, 컨테이너 연동, 빈 구조체와 실패 시 값 보존을 검증한다.

컨테이너별 count 타입, 정렬 방향 byte, 실패 처리 계약은 [[ContainerSerialization]]을 참고한다.

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
- [[Common/PacketFormat]] — 패킷 구조
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
- 집합 기반 중복 검사를 수행하는 이전 검증 helper다.
- 현재 전체 생성 경로의 스키마 검증은 `PacketSchema.ValidateSchema()`가 담당한다.

#### `DuplicateCheckPacketItems(items, packetName)`
- 한 패킷 내부 필드 이름 중복을 검사하는 이전 검증 helper다.

#### `IsValidPacketTypeInYaml(yamlData)`
- 이전 형식의 패킷 목록만 검증하는 호환 helper다. 현재 생성 파이프라인에서는 호출하지 않는다.

#### `PacketSchema.ParseType(text)`
- 중첩 컨테이너를 포함한 허용 범위의 C++ 타입 문자열을 구문 분석한다.

#### `PacketSchema.ValidateSchema(data)`
- `Structs`와 `Packet` 전체를 파일 변경 전에 검증한다.
- 타입 참조를 확인하고 사용자 정의 구조체를 의존성 순서로 정렬한다. 순환 참조는 오류로 거부한다.

#### `PacketSchema.MakeDataStructs(structs)`
- 사용자 정의 `struct`와 각 타입의 `NetBufferCodec` 특수화를 생성한다.
- codec은 필드를 YAML 순서대로 재귀 직렬화하고, 역직렬화 완료 후 임시 객체를 결과에 반영한다.

#### `GeneratePacketType(packetList)`
- `PACKET_ID` enum 헤더를 생성한다.

#### `MakePacketClasss(packetList)`
- `Protocol.h`에 들어갈 패킷 클래스 선언 코드를 만든다.

#### `GenerateProtocolHeader(packetList, structList)`
- `Protocol.h`의 생성 영역을 의존성 순서의 데이터 구조체, codec, 패킷 class로 교체한다.
- 이전 `#pragma pack` 생성 영역은 새 marker 기반 영역으로 한 번 마이그레이션한다.

#### `GenerateInitInPacketHandlerCpp(packetList, originCode)`
- 패킷 핸들러 등록 함수 `Init()`에 필요한 등록 코드를 만든다.

#### `GenerateProtocolCpp(packetList)`
- `GetPacketId`, `BufferToPacket`, `PacketToBuffer` 구현을 생성한다.
- 각 필드는 `NetBuffer::ReadValue`/`WriteValue`를 통해 처리되므로 구조체와 컨테이너도 같은 재귀 codec 경로를 사용한다.

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
- YAML 전체 스키마 검증이 성공한 뒤에만 원본 백업, 코드 생성, 파일 교체, 클라이언트 복사를 실행한다.
- 검증 실패 시 기존 생성 파일은 유지된다. 반면 비어 있는 `Packet:`/`Structs:` 목록은 유효한 정의이므로 대응 생성 결과가 비워질 수 있다.
