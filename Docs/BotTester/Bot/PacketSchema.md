# PacketSchema

> **BotTester 그래프에서 패킷별 입력 필드의 이름·타입·기본값을 정의한다.**  
> `SendPacketConfigPanel`과 node builder가 같은 schema를 사용해 UI 입력과 직렬화 구성을 맞춘다.

---

## 목차

1. [필드 타입](#필드-타입)
2. [기본 schema](#기본-schema)
3. [공개 함수](#공개-함수)
4. [동시성 및 소유권](#동시성-및-소유권)

---

## 필드 타입

`FieldType`은 아래 값을 제공한다.

| 값 | 의미 |
|----|------|
| `Byte` | 8-bit unsigned 값 |
| `Ushort` | 16-bit unsigned 값 |
| `Int` | 32-bit signed 값 |
| `Uint` | 32-bit unsigned 값 |
| `Ulong` | 64-bit unsigned 값 |
| `String` | 문자열 값 |

`PacketFieldDef`는 필수 `Name`, 필수 `Type`, 선택 `DefaultValue`를 가진다.

---

## 기본 schema

| `PacketId` | 필드 |
|------------|------|
| `Ping`, `Pong` | 없음 |
| `TestStringPacketReq` | `testString: String` |
| `TestStringPacketRes` | `echoString: String` |
| `TestPacketReq`, `TestPacketRes` | `order: Int` |

---

## 공개 함수

### `Get`

```csharp
static PacketFieldDef[]? Get(PacketId id);
```

등록된 필드 배열을 반환한다. 알 수 없는 `PacketId`이면 `null`을 반환한다.

### `Register`

```csharp
static void Register(PacketId id, PacketFieldDef[] fields);
```

새 schema를 등록하거나 같은 `PacketId`의 기존 schema를 교체한다.

```csharp
PacketSchema.Register(
    PacketId.TestPacketReq,
    [new PacketFieldDef { Name = "order", Type = FieldType.Int, DefaultValue = 0 }]);
```

---

## 동시성 및 소유권

- 내부 저장소는 일반 `Dictionary`이며 별도 lock이 없다.
- `Register()`는 그래프 편집이나 실행을 시작하기 전 초기화 단계에서 호출해야 한다.
- `Get()`은 내부 배열 참조를 그대로 반환한다. 호출 측에서 배열을 수정하면 전역 schema가 바뀔 수 있으므로 읽기 전용으로 취급해야 한다.

---

## 관련 문서

- [[ActionNodes]] - `SendPacketNode` 동작
- [[NodeConfigPanels]] - schema 기반 입력 UI
- [[BotActionGraphWindow]] - node builder를 통한 실행 노드 생성
- [[Testing]] - 기본 schema와 override 테스트
