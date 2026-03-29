# Logger

> 이벤트 기반 비동기 JSON 파일 로거. 별도 스레드에서 큐를 처리하여 게임 스레드 지연 최소화.

---

## 구조

```
Logger (싱글톤)
 ├── logWaitingQueue    ← std::queue<shared_ptr<LogBase>>
 ├── loggerThread       ← std::jthread (Worker)
 ├── logFileStream      ← 시간 기반 파일명 (log_YYYY-MM-DD_HH.txt)
 └── loggerEventHandles[2]
      [0] AutoResetEvent  ← 새 로그 추가 시 Set
      [1] ManualResetEvent ← 종료 시 Set
```

---

## 사용 방법

### 매크로

```cpp
LOG_ERROR("연결 실패: 에러 코드 " + std::to_string(code));
LOG_DEBUG("디버그 정보");   // Release에서는 no-op
```

### 커스텀 로그 타입

```cpp
class ServerLog : public LogBase {
    OBJECT_TO_JSON_LOG(
        SET_LOG_ITEM(logString);
    );
public:
    std::string logString;
};
```

### 직접 사용

```cpp
auto log = Logger::MakeLogObject<ServerLog>();
log->logString = "상세 메시지";
Logger::GetInstance().WriteLog(log);
```

---

## 비동기 처리 흐름

```
WriteLog(logObject)
  ├─ logObject->SetLogTime()   ← UTC 타임스탬프 기록
  ├─ logWaitingQueue.push()
  └─ SetEvent(loggerEventHandles[0])

Worker()
  WaitForMultipleObjects(2 handles, INFINITE)
  case 0 (LOG_HANDLE):
    큐를 copyQueue로 swap
    WriteLogImpl(copyQueue)   ← JSON 직렬화 → 파일 쓰기
  case 1 (STOP_HANDLE):
    Sleep(10초)               ← 남은 로그 수집 대기
    WriteLogImpl(copyQueue)
    return
```

---

## 로그 파일 형식

파일명: `Log Folder/log_2025-01-15_14.txt`

```json
{"LogTime":"2025-01-15 14:30:45.123","GetLastErrorCode":0,"Log":{"logString":"Server started"}}
{"LogTime":"2025-01-15 14:30:45.456","GetLastErrorCode":0,"Log":{"logString":"Client connected"}}
```

---

## 다중 컴포넌트 공유

```
RunLoggerThread()
  if currentConnectedClientCount++ > 0 → 이미 실행 중, 건너뜀

StopLoggerThread()
  if --currentConnectedClientCount != 1 → 아직 사용자 있음, 건너뜀
  SetEvent(stopHandle) → Worker 종료
```

---

## LogBase 확장 매크로

```cpp
#define OBJECT_TO_JSON_LOG(...)
// ObjectToJson() 가상 함수 자동 생성

#define SET_LOG_ITEM(logObject)
// jsonObject[#logObject] = logObject  (변수명 = JSON 키)
```

---

## 로그 압축 도구

`Tool/LogCompress.bat`:
- 당일 `.txt` 로그 → `Archive/` 폴더에 `.tar.gz` 압축
- 무결성 검사 실패 시 원본 유지, 손상 아카이브 삭제
- 이전 날 아카이브 자동 삭제

---

## 관련 문서
- [[MultiSocketRUDPCore]] — RunLoggerThread / StopLoggerThread 호출
- [[RUDPClientCore]] — 클라이언트 측 Logger 사용
