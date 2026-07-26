# MemoryTracer

> **서버 프로세스의 객체 할당·해제 이력, stack trace, 스레드별 활성 객체 수를 기록한다.**
> 현재 구현에는 Debug 전용 compile guard가 없으며 Release 구성에도 포함된다. 초기 `enabled` 값은 `true`다.

---

## 목차

1. [활성화와 상태](#1-활성화와-상태)
2. [추적 API](#2-추적-api)
3. [보고서 API](#3-보고서-api)
4. [파일 출력](#4-파일-출력)
5. [스레드 안전과 성능](#5-스레드-안전과-성능)

---

## 1. 활성화와 상태

```cpp
static void Enable();
static void Disable();
static bool IsEnabled();
static void Clear();
```

- 프로세스 시작 시 추적은 활성화되어 있다.
- `Disable()`은 이후 `TrackObject()`, `UntrackObject()`, `AddNote()`를 건너뛰게 한다. 기존 이력은 지우지 않는다.
- `Clear()`는 활성화 상태와 관계없이 저장된 이력을 모두 제거한다.

---

## 2. 추적 API

```cpp
static std::string GetStackTrace();

static void TrackObject(
    void* ptr,
    const std::string& objectName,
    const std::string& file,
    int line,
    const std::string& note = "");

static void UntrackObject(void* ptr, const std::string& file, int line);
static void AddNote(void* ptr, const std::string& note);
static size_t GetActiveObjectCount();
```

- `TrackObject()`는 `nullptr`과 비활성 상태를 무시하고, 객체 이름·위치·stack trace·시각·스레드·메모를 기록한다.
- `UntrackObject()`는 항목을 삭제하지 않고 해제 위치·시각·스레드를 표시한다.
- `AddNote()`는 기존 메모 뒤에 ` | ` 구분자로 내용을 추가한다.
- `GetStackTrace()`는 최대 15개 frame을 수집하며 호출부 위의 두 frame을 건너뛴다.
- `GetActiveObjectCount()`는 아직 해제 표시가 없는 항목 수를 반환한다.

---

## 3. 보고서 API

```cpp
static void GenerateReport();
static void GetObjectHistory(void* ptr);
static void GetThreadStatistics();

static void GenerateReportToFile(const std::string& filename = "");
static void GetObjectHistoryToFile(void* ptr, const std::string& filename = "");
static void GetThreadStatisticsToFile(const std::string& filename = "");
```

접미사 `ToFile`이 없는 함수는 `std::cout`에 출력한다. `ToFile` 함수는 시각을 포함한 보고서를 만들고, `filename`이 비어 있으면 현재 `outputFilename`을 사용한다.

---

## 4. 파일 출력

```cpp
static void SetOutputFile(const std::string& filename);
static void CloseOutputFile();
```

기본 `outputFilename`은 `MemoryTracer.log`다.

- `SetOutputFile(nonEmpty)`은 대상 파일을 truncate하고 시작 header를 기록한다.
- 보고서 파일 기록은 append 방식이다. 명시한 파일을 열지 못하면 `std::cout`으로 fallback한다.
- `CloseOutputFile()`은 현재 파일에 종료 footer를 append한 뒤 `outputFilename`을 비운다. 이후 빈 파일명 보고서는 콘솔로 출력된다.

---

## 5. 스레드 안전과 성능

- 할당 map과 대부분의 보고서·파일 상태 접근은 `tracerMutex`로 직렬화한다.
- `enabled`는 `std::atomic<bool>`이다.
- `SetOutputFile()`은 현재 `outputFilename`을 mutex 획득 전에 대입한다. 다른 스레드가 동시에 보고서를 출력하거나 파일을 닫지 않도록 초기화·종료 단계에서만 호출해야 한다.
- 공개 `GetStackTrace()`는 자체적으로 `tracerMutex`를 획득하지 않는다. 직접 병렬 호출하지 않는 것이 안전하다.
- stack trace와 symbol 조회는 비용이 크며 추적 함수는 전역 mutex를 점유한다. Release 구성에서도 자동 제외되지 않으므로 성능이 중요한 운영 환경에서는 명시적으로 `Disable()`하거나 호출 자체를 제외해야 한다.

---

## 관련 문서

- [[Troubleshooting]] - 메모리 증가 점검
