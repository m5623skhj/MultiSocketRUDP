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
