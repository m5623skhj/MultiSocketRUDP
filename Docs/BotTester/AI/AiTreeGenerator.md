# AI 트리 자동 생성 (AiTreeGenerator)

> Gemini AI에게 자연어로 테스트 시나리오를 설명하면,  
> 행동 트리 JSON을 생성하고 캔버스에 적용한다.

---

## 흐름 다이어그램

![[AiTreeFlow.svg]]

---

## 전체 흐름

```
사용자: "Ping을 5번 보내고 응답을 기다리다가 타임아웃되면 연결 종료"
        ↓
[1] HandleGenerate()
  └─ Gemini API (gemini-2.5-flash)
       System Prompt: 노드 명세 + 행동 규칙
       User Prompt: 사용자 입력 → JSON 반환 요청
        ↓
[2] AiTreeService.Parse(aiResponse)
  ├─ 마크다운 펜스 제거 (`StripMarkdownFence`)
  ├─ JSON 추출 (`{...}` 범위)
  └─ 루트 노드 / description 파싱
        ↓
[3] AiTreeService.Validate(treeResponse)
  └─ 노드 타입 유효성 재귀 검사
        ↓
[4] HandleApply() → AiNodeFactory.CreateFromJson()
  └─ JSON → NodeVisual 트리 생성 → 캔버스 배치
```

---

## 함수 설명

### `BotActionGraphWindow` 내부 AI 생성 진입점

#### `ShowAiTreeGenerator()`
- AI 트리 생성 보조 창을 열거나, 이미 열려 있으면 기존 창을 활성화한다.
- 생성 버튼과 적용 버튼 이벤트를 연결하고, 마지막 생성 결과를 창 생명주기 동안 보관한다.

#### `HandleGenerate(string userInput, TextBox outputBox, Action<AiTreeResponse?> setLastTree)`
- 사용자 자연어 입력을 Gemini 요청용 프롬프트로 감싸서 전송한다.
- 응답 텍스트를 `AiTreeService.Parse()`로 파싱하고 `Validate()`로 검증한 뒤, 성공/실패 결과를 출력 박스와 apply 상태에 반영한다.

#### `HandleApply(AiTreeResponse? lastTree, Window window)`
- 마지막으로 성공한 AI 응답을 캔버스의 `NodeVisual` 그래프로 변환해 적용한다.
- 기존 노드는 루트만 남기고 정리한 뒤 `AiNodeFactory.CreateFromJson()`으로 새 노드 트리를 만든다.

#### `ClearAllNodesExceptRoot()`
- 현재 캔버스에서 루트를 제외한 모든 노드와 포트, 컨텍스트 메뉴를 제거한다.
- AI 결과 적용 직전 초기화 단계에서 사용된다.

#### `AITreeGenerator_Click(object sender, RoutedEventArgs e)`
- 메뉴/버튼에서 AI 생성 창을 여는 UI 이벤트 핸들러다.

### `AiTreeService`

#### `Parse(string aiResponse)`
- AI 응답에서 마크다운 펜스 제거, JSON 본문 추출, 에러 응답 해석, `description/tree` 추출을 수행한다.

#### `Validate(AiTreeResponse response)`
- 루트 JSON을 재귀적으로 순회하며 지원 노드 타입과 필수 속성 구조를 검증한다.

#### `FormatJson(string json)`
- 출력 창에 보기 좋게 표시하기 위해 JSON을 pretty-print 한다.

### `AiNodeFactory`

#### `CreateFromJson(JsonElement jsonNode, double x, double y, Dictionary<string, NodeVisual> createdNodes, string nodePath)`
- JSON 노드 하나를 `NodeVisual`로 만들고, 자식 분기까지 재귀적으로 생성한다.

#### `TryCreateChild(...)`
- 특정 자식 프로퍼티가 있을 때 자식 노드를 생성하고 연결 위치를 갱신한다.

#### `Configure(NodeVisual visual, JsonElement jsonNode)`
- 노드 타입별 JSON 값을 `NodeConfiguration`으로 옮긴다.

---

## AiTreeService

### Parse
```
StripMarkdownFence()  ← ```json ... ``` 제거
JSON 추출 ({...})
JsonDocument.Parse()
error 프로퍼티 있으면 → AiTreeResponse.Fail(reason, details)
description + tree 추출 → AiTreeResponse 반환
```

### Validate
```
ValidateNode(node, result, path)
  ├─ "type" 프로퍼티 존재 여부
  ├─ KnownNodeTypes 목록에 포함 여부
  └─ 재귀: next, true_branch, false_branch, loop_body, repeat_body, timeout_nodes
```

알려진 노드 타입 (`KnownNodeTypes`):
`SendPacketNode`, `DelayNode`, `RandomDelayNode`, `LogNode`, `DisconnectNode`, `ConditionalNode`, `LoopNode`, `RepeatTimerNode`, `WaitForPacketNode`, `SetVariableNode`, `GetVariableNode`, `CustomActionNode`, `AssertNode`, `RetryNode`, `PacketParserNode`

---

## AiNodeFactory

JSON → NodeVisual 변환:

```
CreateFromJson(jsonNode, x, y, createdNodes, nodePath)
  ├─ type → C# Type.GetType() → NodeVisual 생성
  ├─ Configure(visual, jsonNode) → NodeConfiguration 설정
  └─ TryCreateChild(jsonNode, propertyName, ...)
       대상 프로퍼티: next, true_branch, false_branch,
                      loop_body, exit_nodes, repeat_body,
                      retry_body, timeout_nodes, failure_nodes
```

자식 노드는 `childX = parentX + 200`, `childY += 150`으로 자동 배치.

### Configure 파라미터 매핑

| 노드 타입 | JSON 키 → Configuration 필드 |
|-----------|------------------------------|
| SendPacketNode | `packet_id` → `PacketId` |
| DelayNode | `delay_ms` → `IntValue` |
| RandomDelayNode | `min_delay_ms`, `max_delay_ms` → `Properties` |
| LogNode | `message` → `StringValue` |
| DisconnectNode | `reason` → `StringValue` |
| ConditionalNode | `condition` → `Properties["Left"]` (단순 조건) |
| LoopNode | `max_iterations` → `Properties["LoopCount"]` |
| RepeatTimerNode | `repeat_count`, `interval_ms` |
| WaitForPacketNode | `expected_packet_id`, `timeout_ms` |
| SetVariableNode | `variable_name`, `value_type`, `value` |
| AssertNode | `error_message`, `stop_on_failure` |
| RetryNode | `max_retries`, `retry_delay_ms`, `exponential_backoff` |

---

## GeminiClient 설정

`WithGeminiClient/GeminiClientConfiguration.json`:

```json
{
  "GeminiSettings": {
    "ApiKey": "YOUR_API_KEY",
    "ModelName": "gemini-2.5-flash",
    "SystemPrompt": "...(노드 규칙 및 제약)...",
    "NodeSpecs": "...(모든 노드 타입 JSON 스키마)..."
  }
}
```

파일은 빌드 출력 디렉터리에 `PreserveNewest`로 복사됨.

> API 키 설정 없으면 `GeminiClient` 초기화 실패 → AI Tree Generator 비활성화 (경고 팝업)

---

## AI가 생성한 오류 응답 처리

AI가 표현 불가능한 요구사항이라고 판단할 경우:
```json
{
  "error": {
    "reason": "표현 불가능한 요구사항",
    "details": "요구사항을 만족하기 위해 필요한 행동이 명세에 정의되어 있지 않습니다."
  }
}
```
→ `AiTreeResponse.IsError = true`, 출력 박스에 이유 표시

---

## 관련 문서
- [[GeminiClient]] — Gemini API 클라이언트
- [[ActionNodes]] — 생성 가능한 노드 타입
- [[BotActionGraphWindow]] — AI Generator 진입점
