# CanvasRenderer (캔버스 렌더링 시스템)

> WPF Canvas 위의 노드 배치, 연결선 렌더링, 드래그, 패닝, 포트 연결을 담당한다.

---

## 구성 클래스

```
NodeCanvasRenderer   ← 연결선 그리기, 포트 위치 업데이트
NodeInteractionHandler ← 드래그, 패닝, 포트 연결
```

---

## NodeCanvasRenderer

### 포트 위치 계산 (`UpdatePortPositions`)

```
노드 경계(left, top) 기준:
  InputPort  : left - 18,  top + height/2 - 18
  OutputPort : left + width - 18, top + height/2 - 18
  OutputPortTrue  : left + width - 18, top + height/3 - 18
  OutputPortFalse : left + width - 18, top + height*2/3 - 18
  DynamicPorts[i] : 균등 분할 (height / (count+1))
```

### 연결선 렌더링 (`RedrawConnections`)

1. 기존 `Line` 요소 전체 제거 (임시 라인 제외)
2. 모든 노드의 연결 (`Next`, `TrueChild`, `FalseChild`, `DynamicChildren`) 순회
3. 각 연결에 대해 출력 포트 → 입력 포트 중심점을 직선으로 연결
4. `Panel.ZIndex = -1`로 노드 뒤에 배치

---

## NodeInteractionHandler

### 드래그 (`EnableDrag`)

```
MouseLeftButtonDown → 드래그 시작, CaptureMouse
MouseMove → 위치 갱신, UpdatePortPositions(node)
MouseLeftButtonUp → 드래그 종료, ReleaseMouseCapture
```

### 캔버스 패닝

빈 캔버스 클릭 후 드래그:
```
MouseLeftButtonDown(캔버스 직접 클릭) → isPanning = true, CaptureMouse
MouseMove → ScrollViewer 오프셋 조정 (마우스 델타)
MouseLeftButtonUp → isPanning = false
```

### 포트 연결 프로토콜

```
StartConnection(port, type)
  → connectingFromNode = 포트 소유 노드
  → tempConnectionLine 생성 (점선, 포트 색상)
  → IsConnecting = true

MouseMove → tempConnectionLine X2/Y2 갱신

TryFinishConnection(port)
  → to = 입력 포트 소유 노드
  → CreatesCycle(from, to) 체크 (DFS)
  → Connect(from, to, type)
       type 분기:
         "true"/"continue" → TrueChild
         "false"/"exit"    → FalseChild
         "choice_N"        → DynamicChildren[N]
         default           → Next
  → CleanupConnection()
```

### 순환 참조 방지

```csharp
CreatesCycle(from, to)
  Stack<NodeVisual> stack = [to]
  visited = {}
  while stack:
    n = pop
    if n == from → true (순환)
    push: n.Next, n.TrueChild, n.FalseChild, n.DynamicChildren
  return false
```

---

## 노드 하이라이트

```csharp
NodeCanvasRenderer.Highlight(node)
  → BorderBrush = Yellow, BorderThickness = 4

NodeCanvasRenderer.Unhighlight(node)
  → BorderBrush = White, BorderThickness = 2
```

---

## 관련 문서
- [[BotActionGraphWindow]] — 렌더러 및 인터랙션 핸들러 사용
- [[ActionNodes]] — NodeVisual 구조
