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
---

## 현재 코드 기준 함수 설명

### 공개 함수

#### `static Logger& GetInstance()`
- 전역 Logger 싱글톤을 반환한다.

#### `static bool IsAlive()`
- Logger 스레드가 살아 있는지 반환한다.

#### `void RunLoggerThread(const bool isAlsoPrintToConsole)`
- 로거 스레드를 시작하고 콘솔 출력 여부를 설정한다.

#### `void Worker()`
- 이벤트를 기다리다가 대기 큐를 비우고 실제 파일 기록을 수행한다.

#### `void StopLoggerThread()`
- Logger 종료 이벤트를 보내고 스레드를 정리한다.

#### `template<typename LogType> static std::shared_ptr<LogType> MakeLogObject()`
- 타입 안전한 로그 객체 생성을 돕는 헬퍼다.

#### `void WriteLog(std::shared_ptr<LogBase> logObject)`
- 로그 객체를 대기 큐에 넣고 작업 스레드를 깨운다.

### 내부 함수

#### `static void CreateFolderIfNotExists(const std::string& folderPath)`
- 로그 폴더가 없으면 생성한다.

#### `void WriteLogImpl(std::queue<std::shared_ptr<LogBase>>& copyLogWaitingQueue)`
- 복사된 큐를 순회하며 실제 출력 작업을 수행한다.

#### `void WriteLogToFile(const std::shared_ptr<LogBase>& logObject)`
- 단일 로그 객체를 JSON 라인 형식으로 파일에 기록한다.
