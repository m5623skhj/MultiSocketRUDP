# NodeConfigPanels (노드 설정 다이얼로그)

> 노드 더블클릭 시 표시되는 설정 다이얼로그 패널.  
> `INodeConfigPanel` 인터페이스로 노드 타입별 UI를 분리해 구현한다.

---

## 패널 등록 (`NodeConfigPanelRegistry`)

```csharp
// 순서대로 CanConfigure(node) 검사 → 첫 번째 매칭 패널 반환
new NodeConfigPanelRegistry(createDynamicPorts)
  → panels: [SendPacketConfigPanel, DelayConfigPanel, ...]
```

---

## 패널 목록

| 패널 클래스 | 대상 노드 | 설정 항목 |
|------------|-----------|-----------|
| `SendPacketConfigPanel` | SendPacketNode | PacketId ComboBox |
| `DelayConfigPanel` | DelayNode | Delay (ms) TextBox |
| `RandomDelayConfigPanel` | RandomDelayNode | Min / Max Delay TextBox |
| `LogConfigPanel` | LogNode | 메시지 TextBox (멀티라인) + 플레이스홀더 힌트 |
| `DisconnectConfigPanel` | DisconnectNode | 종료 이유 TextBox |
| `WaitForPacketConfigPanel` | WaitForPacketNode | PacketId ComboBox + Timeout (ms) |
| `SetVariableConfigPanel` | SetVariableNode | 변수명 + 타입 ComboBox + 값 |
| `GetVariableConfigPanel` | GetVariableNode | 변수명 TextBox |
| `LoopConfigPanel` | LoopNode | Loop Count TextBox |
| `RepeatTimerConfigPanel` | RepeatTimerNode | Repeat Count + Interval (ms) |
| `AssertConfigPanel` | AssertNode | 오류 메시지 + StopOnFailure CheckBox |
| `RetryConfigPanel` | RetryNode | MaxRetries + RetryDelay + ExponentialBackoff CheckBox |
| `PacketParserConfigPanel` | PacketParserNode | Setter Method ComboBox (BotVariables 목록) |
| `RandomChoiceConfigPanel` | RandomChoiceNode | Choice 수 → 동적 포트 재생성 |
| `ConditionalConfigPanel` | ConditionalNode | Left Type/Value + Operator + Right Type/Value |

---

## ConditionalConfigPanel 상세

3단 구성 (Left · Op · Right):

```
[Type ComboBox: "Constant" | "Getter Function"]
  └─ Constant  → TextBox (직접 값 입력)
  └─ Getter Function → ComboBox (BotVariables Getter 목록)

[Operator ComboBox: >, <, ==, >=, <=, !=]

[Type ComboBox: "Constant" | "Getter Function"]
  └─ ...동일...
```

저장 시 `NodeConfiguration.Properties`에 기록:
- `LeftType`, `Left`, `Op`, `RightType`, `Right`

`ConditionEvaluator.Evaluate(ctx, leftType, left, op, rightType, right)` 호출로 평가.

---

## ConfigUi 헬퍼

```csharp
ConfigUi.Header("타이틀")             // Bold TextBlock
ConfigUi.Hint("힌트 텍스트")          // Gray 소형 TextBlock
ConfigUi.LabeledBox(stack, "라벨:", "기본값")  // 가로 배치 Label+TextBox
ConfigUi.SaveButton(onClick)          // Save 버튼
```

---

## 관련 문서
- [[BotActionGraphWindow]] — 다이얼로그 호출
- [[ActionNodes]] — 노드별 설정값 의미
