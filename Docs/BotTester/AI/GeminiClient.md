# GeminiClient

> Google Gemini API를 사용해 행동 트리 JSON을 생성하는 AI 클라이언트.

---

## 초기화

```csharp
// BotActionGraphWindow 생성 시 호출
geminiClient = new GeminiClient(geminiApiConfig);
```

설정 파일 탐색 순서:
1. `{BaseDirectory}/GeminiClientConfiguration.json`
2. `{BaseDirectory}/WithGeminiClient/GeminiClientConfiguration.json`

---

## AskAsync

```csharp
public async Task<string> AskAsync(string userMessage)
  → client.Models.GenerateContentAsync(
       modelName,
       [user content],
       config: { SystemInstruction: systemPrompt + "\n\n" + nodeSpecs }
     )
  → response.GetText()  // 모든 파트의 텍스트 연결
```

예외 발생 시 `"[에러 발생]: {message}"` 반환 (예외 전파 안 함)

---

## 함수 설명

#### `GeminiExtensions.GetText(this GenerateContentResponse response)`
- Gemini 응답의 첫 번째 candidate에서 텍스트 파트만 이어붙여 최종 문자열을 만든다.
- 문서상의 단순 파싱 helper가 아니라, 실제로 `AskAsync()`의 최종 반환 텍스트 추출에 사용된다.

#### `GeminiClient(IConfiguration configuration)`
- `GeminiSettings:ApiKey`, `ModelName`, `SystemPrompt`, `NodeSpecs`를 읽어 클라이언트를 초기화한다.
- 필수 설정이 빠지면 즉시 예외를 발생시켜 잘못된 구성 상태를 조기에 드러낸다.

#### `Task<string> AskAsync(string userMessage)`
- 사용자 메시지를 Gemini 모델에 전달하고, `SystemPrompt`와 `NodeSpecs`를 `SystemInstruction`으로 함께 제공한다.
- 성공 시 생성 텍스트를 반환하고, 실패 시 예외를 던지지 않고 오류 문자열을 반환한다.

---

## System Prompt 역할

`GeminiClientConfiguration.json`의 `SystemPrompt`:
- 행동 트리 생성 규칙 (오직 정의된 노드만 사용)
- 출력 형식 강제 (JSON만, 마크다운 없음)
- 표현 불가능한 요구 시 오류 JSON 반환 규칙

`NodeSpecs`:
- 모든 노드 타입의 JSON 스키마 및 설명
- Context 접근 규칙 및 예시

---

## 응답 파싱 (`GeminiExtensions.GetText`)

```csharp
response.Candidates
    ?.FirstOrDefault()
    ?.Content?.Parts
    ?.Where(p => !string.IsNullOrEmpty(p.Text))
    .Select(p => p.Text)
    .Aggregate("", (a, b) => a + b)
```

---

## 관련 문서
- [[AiTreeGenerator]] — GeminiClient 사용 흐름
