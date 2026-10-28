# PacketSchema

> BotTester 송신 노드가 사용하는 패킷 필드 이름·타입·기본값과 중첩 구조를 정의한다.
> `Bot/PacketSchema.cs`와 `Generated/PacketSchema.g.cs`가 선언과 생성 스키마를 구성한다.

## 목차

1. [필드 타입](#필드-타입)
2. [생성 스키마](#생성-스키마)
3. [조회와 등록](#조회와-등록)
4. [입력과 동시성](#입력과-동시성)

## 필드 타입

| `FieldType` | 의미 |
|---|---|
| `Byte`, `Ushort`, `Uint`, `Ulong` | 부호 없는 8·16·32·64비트 정수 |
| `Sbyte`, `Short`, `Int`, `Long` | 부호 있는 8·16·32·64비트 정수 |
| `Float`, `Double`, `Bool` | 실수와 불리언 |
| `String`, `Wstring` | UTF-8·UTF-16LE 문자열 |
| `Struct` | 생성 구조체 |
| `Vector`, `List`, `Set`, `UnorderedSet` | 원소 타입을 가진 컨테이너 |
| `Map`, `UnorderedMap` | 키와 값 타입을 가진 컨테이너 |

`PacketFieldDef`의 `Name`과 `Type`은 필수이며 `DefaultValue`는 선택 사항이다. `Members`는 구조체 필드, `Element`는 원소 또는 map 키, `Value`는 map 값, `Descending`은 정렬 컨테이너의 내림차순 여부를 표현한다.

## 생성 스키마

`MultiSocketRUDP/Tool/PacketDefine.yml`에서 `PacketGenerator`가 `CreateGeneratedSchemas()`를 생성한다. 현재 기본 패킷은 다음과 같다.

| 패킷 | 필드 |
|---|---|
| `Ping`, `Pong` | 없음 |
| `TestStringPacketReq` | `testString: String` |
| `TestStringPacketRes` | `echoString: String` |
| `TestPacketReq`, `TestPacketRes` | `order: Int` |
| `ChannelEchoReq`, `ChannelEchoRes` | `requestId: Ulong`, `unreliable: Byte` |

새 패킷·구조체는 YAML을 수정하고 생성기를 실행한다. 생성 파일을 직접 편집해 YAML과 어긋나게 만들지 않는다.

## 조회와 등록

```csharp
public static PacketFieldDef[]? Get(PacketId id);
public static void Register(PacketId id, PacketFieldDef[] fields);
```

`Get()`은 내부 배열을 반환하며 미등록 ID는 `null`이다. `Register()`는 새 ID를 등록하거나 기존 배열을 교체한다. 테스트·초기화 단계에서 명시적으로 재정의할 때 사용한다.

```csharp
PacketSchema.Register(PacketId.TestPacketReq,
    [new PacketFieldDef { Name = "order", Type = FieldType.Int, DefaultValue = 0 }]);
```

## 입력과 동시성

`SendPacketConfigPanel`과 노드 빌더가 같은 스키마를 사용한다. 구조체는 JSON 객체, 컨테이너는 JSON 배열, map은 `[[키,값], ...]`로 입력한다. `PacketFieldCodec`이 중첩 구조와 타입·개수·중복을 검사한다. 자세한 전송 형식과 크기 한계는 [[PacketGenerator]]와 [[ContainerSerialization]]을 따른다.

사전은 일반 `Dictionary`이며 잠금이 없다. `Register()`는 그래프 편집·실행 전 초기화 단계에서만 직렬 호출한다. `Get()`의 배열과 중첩 디스크립터는 복사본이 아니므로 읽기 전용으로 취급한다. 개별 송신의 JSON 값과 직렬화 상태는 호출별로 관리한다.

## 관련 문서

- [[ActionNodes]] — 송신 노드
- [[NodeConfigPanels]] — 입력 UI
- [[PacketGenerator]] — 스키마 생성
- [[ContainerSerialization]] — 전송 형식
- [[Testing]] — 직렬화 회귀 검증
