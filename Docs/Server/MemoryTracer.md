# MemoryTracer

> 디버그 빌드에서 메모리 추적과 보고서를 남기는 도구다. 이 문서는 현재 헤더 시그니처 기준만 남긴다.

---

## 현재 핵심 API

```cpp
static void Enable();
static void Disable();
static bool IsEnabled();

static void TrackObject(void* ptr, const std::string& typeName, const char* file, int line, const std::string& note = "");
static void UntrackObject(void* ptr, const char* file, int line);
static void AddNote(void* ptr, const std::string& note);

static void GenerateReport();
static void GenerateReportToFile(const std::string& filename = "");
static size_t GetActiveObjectCount();

static void GetObjectHistory(void* ptr);
static void GetThreadStatistics();

static void GetObjectHistoryToFile(void* ptr, const std::string& filename = "");
static void GetThreadStatisticsToFile(const std::string& filename = "");
```

---

## 현재 시그니처 기준

아래 형태의 out parameter 기반 시그니처는 현재 헤더와 맞지 않는다.

```cpp
GetObjectHistory(void* ptr, OUT std::vector<std::string>& history)
GetThreadStatistics(OUT std::unordered_map<std::thread::id, size_t>& activeByThread)
```

현재는 둘 다 내부 출력형 API다.

---

## 사용 예시

```cpp
MemoryTracer::Enable();
MemoryTracer::TrackObject(ptr, "NetBuffer", __FILE__, __LINE__);
MemoryTracer::AddNote(ptr, "allocated in recv path");

MemoryTracer::GenerateReport();
MemoryTracer::GenerateReportToFile("memory_report.txt");
MemoryTracer::GetObjectHistory(ptr);
MemoryTracer::GetThreadStatistics();
```

파일이 필요하면 `GetObjectHistoryToFile(...)`, `GetThreadStatisticsToFile(...)`를 사용한다.

---

## 주의사항

- 디버그 전용으로 사용하는 편이 맞다.
- stack trace 수집은 비용이 크다.
- 운영 빌드에서는 비활성화하거나 제거하는 것이 안전하다.

---

## 관련 문서

- [[Troubleshooting]] - 메모리 증가 점검
