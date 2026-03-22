# BotActionGraphWindow (비주얼 그래프 에디터)

> WPF 기반 노드 그래프 에디터. 노드를 캔버스에 배치하고, 포트를 연결해 행동 트리를 시각적으로 구성한다.

---

## 화면 구성

![[GraphEditor_Layout.svg]]

```
┌──────────────────────────────────────────────────────────┐
│ Left Panel (250px)     │ Right Panel                     │
│ ┌──────────────────┐   │ ┌────────────────────────────┐  │
│ │ ActionNodeListBox│   │ │      GraphCanvas (5000×5000)│  │
│ │  (노드 타입 목록) │   │ │   (ScrollViewer 내부)      │  │
│ └──────────────────┘   │ └────────────────────────────┘  │
│ ┌──────────────────┐   │ ┌────────────────────────────┐  │
│ │ AI Tree Generator│   │ │    LogListBox (하단 200px)  │  │
│ │ Add Node         │   │ └────────────────────────────┘  │
│ │ Validate Graph   │   │                                 │
│ │ Build Graph      │   │                                 │
│ │ Apply to BotTester│  │                                 │
│ │ View Statistics  │   │                                 │
│ └──────────────────┘   │                                 │
└──────────────────────────────────────────────────────────┘
```

---

## 노드 조작

| 동작 | 방법 |
|------|------|
| 노드 추가 | 좌측 목록 선택 → Add Node |
| 노드 이동 | 노드 드래그 |
| 포트 연결 | 출력 포트 클릭 드래그 → 입력 포트에 놓기 |
| 노드 설정 | 노드 더블클릭 → 설정 다이얼로그 |
| 노드 삭제 | 노드 선택 → Delete 키, 또는 우클릭 컨텍스트 메뉴 |
| 캔버스 패닝 | 캔버스 빈 공간 드래그 |

---

## 포트 색상

| 색상 | 의미 |
|------|------|
| LightGreen | 입력 포트 (`input`) |
| LightBlue | `default` 출력 (Action 노드) |
| LightGreen | `true` / `continue` 출력 |
| IndianRed | `false` / `exit` 출력 |
| 가중치별 색 | `choice_0`, `choice_1` ... (RandomChoiceNode) |

---

## 주요 버튼 동작

### Build Graph
```
BuildActionGraph()
  ├─ foreach NodeVisual:
  │    NodeBuilderRegistry.TryBuild(visual) → ActionNodeBase
  │
  ├─ ConnectNext() / ConnectBranches() 연결
  │
  └─ GraphValidator.ValidateGraph()
       ├─ Error → ValidationWindow 표시, 빌드 중단
       └─ Warning → 사용자 확인 후 계속/중단
```

### Apply to BotTester
```
BotTesterCore.Instance.SetBotActionGraph(builtGraph)
BotTesterCore.Instance.SaveGraphVisuals(allNodes)
```
→ 이후 `StartBotTest` 클릭 시 이 그래프로 봇 실행

### AI Tree Generator
→ [[AiTreeGenerator]] 참조

---

## 그래프 저장/복원

`SaveGraphVisuals(allNodes)` → `BotTesterCore`에 `NodeVisual` 리스트 저장  
창 재오픈 시 `GetSavedGraphVisuals()`로 복원 → `RestoreSavedGraph()`

복원 시 처리:
1. `NodeVisual` 복제 (경계 + 포트 재생성)
2. 포트 위치 계산 및 캔버스 배치
3. 노드 간 참조 복원 (매핑 딕셔너리 사용)
4. `RedrawConnections()` 호출

---

## NodeBuilderRegistry 빌더 체계

```
NodeBuilderRegistry
 ├─ SendPacketNodeBuilder
 ├─ DelayNodeBuilder
 ├─ RandomDelayNodeBuilder
 ├─ DisconnectNodeBuilder
 ├─ WaitForPacketNodeBuilder
 ├─ SetVariableNodeBuilder
 ├─ GetVariableNodeBuilder
 ├─ LogNodeBuilder
 ├─ CustomActionNodeBuilder
 ├─ ConditionalNodeBuilder   ← conditionEvaluator 주입
 ├─ LoopNodeBuilder
 ├─ RepeatTimerNodeBuilder
 ├─ PacketParserNodeBuilder
 ├─ RandomChoiceNodeBuilder
 ├─ AssertNodeBuilder
 └─ RetryNodeBuilder
```

미매칭 시 `CustomActionNode`로 폴백(Fallback).

---

## 관련 문서
- [[ActionNodes]] — 노드 타입별 설명
- [[CanvasRenderer]] — 렌더링 시스템
- [[NodeConfigPanels]] — 노드 설정 다이얼로그
- [[AiTreeGenerator]] — AI 트리 생성
- [[GraphValidator]] — 그래프 검증
