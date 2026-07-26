# GraphFileStorage

> **비주얼 봇 그래프를 `.botgraph.json` 파일로 저장하고 복원한다.**  
> 파일 I/O와 JSON 직렬화를 에디터 윈도우에서 분리한 internal persistence 컴포넌트다.

---

## 목차

1. [파일 규칙](#파일-규칙)
2. [파일 모델](#파일-모델)
3. [내부 API](#내부-api)
4. [복원 흐름](#복원-흐름)

---

## 파일 규칙

```text
기본 파일명: BotActionGraph.botgraph.json
기본 확장자: .botgraph.json
파일 필터:   *.botgraph.json, *.json
```

JSON은 들여쓰기를 적용하고 enum을 문자열로 기록한다.

---

## 파일 모델

```text
GraphFileModel
 ├─ Name
 └─ Nodes[]
      └─ NodeVisualFileModel
           ├─ Id, IsRoot, NodeTypeName, Category
           ├─ Left, Top
           ├─ Configuration
           ├─ NextPortType, TruePortType, FalsePortType
           ├─ NextNodeId, TrueChildId, FalseChildId
           └─ DynamicPortTypes[], DynamicChildIds[]
```

`NodeConfigurationFileModel`은 `PacketId`, `StringValue`, `IntValue`, 추가 `Properties`를 저장한다. 노드 연결은 객체 참조 대신 파일 안의 node ID로 표현한다.

---

## 내부 API

### `Save`

```csharp
static void Save(string path, GraphFileModel graphFile);
```

모델을 JSON으로 직렬화해 지정 경로에 기록한다. 기존 파일이 있으면 덮어쓴다.

> 저장은 동기식이며 임시 파일을 이용한 atomic 교체를 수행하지 않는다. UI 이벤트 핸들러가 예외를 받아 상태와 메시지를 표시한다.

### `Load`

```csharp
static GraphFileModel Load(string path);
```

파일을 읽어 `GraphFileModel`로 역직렬화한다. 결과가 `null`이거나 `Nodes`가 비어 있으면 `InvalidOperationException`을 발생시킨다. 파일 읽기·JSON 변환 오류는 호출 측으로 전달한다.

---

## 복원 흐름

```text
LoadGraph_Click
  → GraphFileStorage.Load(path)
  → ClearCurrentGraph()
  → RestoreGraphFromFile(graphFile)
  → BuiltGraph = null
  → 사용자가 다시 Build / 필요 시 Validate / Apply
```

파일 로드는 실행용 `ActionGraph`를 즉시 만들지 않는다. 화면 상태를 복원한 뒤 `Build Graph`의 내부 검증을 통과하고 적용해야 한다. `Validate Graph`는 빌드된 그래프가 있을 때만 별도로 실행할 수 있다.

> **현재 제한:** 빌드 중 검증 오류가 발생하거나 경고 대화상자에서 중단해도 `BuiltGraph` 참조가 남을 수 있다. `Apply to BotTester`는 재검증하지 않으므로 빌드 성공을 확인한 그래프만 적용해야 한다.

---

## 관련 문서

- [[BotActionGraphWindow]] - 저장/로드 UI와 파일 모델 변환
- [[BotActionGraph]] - 빌드 후 실행되는 그래프
- [[GraphValidator]] - 로드 후 정합성 검증
- [[NodeConfigPanels]] - `Configuration` 필드 편집
