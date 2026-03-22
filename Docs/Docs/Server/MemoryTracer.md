# MemoryTracer

> **디버그 빌드 전용 메모리 추적 도구.**  
> 특정 객체의 할당·해제 시점, 스택 트레이스, 스레드 정보를 기록하고  
> 미해제 객체(누수 후보)를 보고서로 출력한다.

---

## 목차

1. [활성화 제어](#1-활성화-제어)
2. [추적 등록/해제](#2-추적-등록해제)
3. [메모 추가](#3-메모-추가)
4. [보고서 생성](#4-보고서-생성)
5. [스택 트레이스 수집](#5-스택-트레이스-수집)
6. [스레드 통계](#6-스레드-통계)
7. [사용 예시](#7-사용-예시)
8. [성능 주의사항](#8-성능-주의사항)

---

## 1. 활성화 제어

```cpp
static void MemoryTracer::Enable();    // 추적 활성화 (기본값)
static void MemoryTracer::Disable();   // 추적 비활성화 (성능 모드)
static bool MemoryTracer::IsEnabled(); // 현재 활성화 여부 확인
```

> **운영 빌드에서는 반드시 Disable하거나 조건부 컴파일로 제거한다.**  
> 스택 트레이스 캡처는 `CaptureStackBackTrace` + `SymFromAddr`를 사용하므로  
> 패킷당 수백 μs의 오버헤드가 발생한다.

---

## 2. 추적 등록/해제

```cpp
// 할당 시 등록
static void MemoryTracer::TrackObject(
    void* ptr,
    const std::string& typeName,
    const char* file,          // __FILE__
    int line,                  // __LINE__
    const std::string& note    // 추가 설명 (선택)
);

// 해제 시 해제 기록
static void MemoryTracer::UntrackObject(
    void* ptr,
    const char* file,
    int line
);
```

**내부 저장 구조:**

```cpp
struct TrackEntry {
    void*       ptr;            // 객체 포인터
    std::string typeName;       // 타입 이름 ("RUDPSession" 등)
    std::string file;           // 할당 파일
    int         line;           // 할당 라인
    std::string stackTrace;     // 스택 트레이스 문자열
    std::thread::id threadId;   // 할당 스레드 ID
    uint64_t    timestamp;      // GetTickCount64() 기준
    bool        isFreed;        // true = UntrackObject 호출됨
    std::string freeFile;       // 해제 파일
    int         freeLine;       // 해제 라인
    std::vector<std::string> notes;  // AddNote로 추가된 메모들
};

// 전역 맵 (ptr → TrackEntry)
static std::unordered_map<void*, TrackEntry> trackMap;
static std::mutex trackMutex;
```

---

## 3. 메모 추가

```cpp
static void MemoryTracer::AddNote(
    void* ptr,
    const std::string& note
);
```

**사용 예시:**

```cpp
// 세션이 특정 상태에 진입할 때 메모 추가
MemoryTracer::TrackObject(session, "RUDPSession", __FILE__, __LINE__,
    std::format("SessionId={}", session->GetSessionId()));

session->OnConnected();
MemoryTracer::AddNote(session, std::format("CONNECTED at {}", GetTickCount64()));

session->DoDisconnect();
MemoryTracer::AddNote(session, std::format("DISCONNECT by {}", reason));
```

보고서에서 `ptr`의 전체 이력을 시간순으로 확인 가능.

---

## 4. 보고서 생성

```cpp
// 콘솔 출력
static void MemoryTracer::GenerateReport();

// 파일 출력
static void MemoryTracer::GenerateReportToFile(
    const std::string& filename
);

// 특정 포인터 이력
static void MemoryTracer::GetObjectHistory(
    void* ptr,
    OUT std::vector<std::string>& history
);

// 미해제 객체 수
static size_t MemoryTracer::GetActiveObjectCount();
```

**보고서 형식 예시:**

```
=== MemoryTracer Report ===
Active (not freed) objects: 2

[1] ptr=0x1234ABCD  type=RUDPSession
    Allocated: Server.cpp:245  Thread: 0x1234  Time: 123456
    Stack:
      RUDPSessionManager::Initialize + 0x45
      MultiSocketRUDPCore::StartServer + 0x123
      main + 0x89
    Notes:
      [123460] SessionId=5
      [123470] CONNECTED at 123470
    Status: NOT FREED

[2] ptr=0xABCD1234  type=NetBuffer
    Allocated: RUDPSession.cpp:312  Thread: 0x5678  Time: 123500
    Status: NOT FREED

=== End of Report ===
```

---

## 5. 스택 트레이스 수집

```cpp
std::string MemoryTracer::GetStackTrace()
{
    constexpr int MAX_FRAMES = 15;
    void* stack[MAX_FRAMES];

    // ① 스택 프레임 캡처
    USHORT frames = CaptureStackBackTrace(
        2,          // 건너뛸 프레임 수 (MemoryTracer 내부 프레임 제외)
        MAX_FRAMES,
        stack,
        nullptr
    );

    // ② 심볼 초기화 (once_flag로 1회만)
    static std::once_flag symInitFlag;
    std::call_once(symInitFlag, []() {
        SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    });

    // ③ 각 프레임을 함수명으로 변환
    std::string result;
    char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    symbol->MaxNameLen = MAX_SYM_NAME;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    for (USHORT i = 0; i < frames; ++i) {
        if (SymFromAddr(GetCurrentProcess(),
                        reinterpret_cast<DWORD64>(stack[i]),
                        0, symbol)) {
            result += std::string(symbol->Name) + "\n";
        } else {
            result += std::format("0x{:016X}\n",
                reinterpret_cast<uintptr_t>(stack[i]));
        }
    }

    return result;
}
```

**디버그 심볼 요구사항:**

```
SymFromAddr가 함수명을 반환하려면 PDB 파일 필요.
Debug 빌드: 자동으로 PDB 생성 → 함수명 표시
Release 빌드: PDB 없으면 주소만 표시

SymInitialize 파라미터:
  TRUE: 현재 프로세스의 모든 로드된 모듈 심볼 자동 검색
  False: 수동으로 SymLoadModule 필요
```

---

## 6. 스레드 통계

```cpp
static void MemoryTracer::GetThreadStatistics(
    OUT std::unordered_map<std::thread::id, size_t>& activeByThread
)
{
    std::scoped_lock lock(trackMutex);
    activeByThread.clear();

    for (auto& [ptr, entry] : trackMap) {
        if (!entry.isFreed) {
            ++activeByThread[entry.threadId];
        }
    }
}
```

**사용 예시:**

```cpp
std::unordered_map<std::thread::id, size_t> stats;
MemoryTracer::GetThreadStatistics(stats);

for (auto& [tid, count] : stats) {
    // 특정 스레드에서 할당했지만 해제 안 된 객체가 많으면
    // 해당 스레드의 Free 로직 확인 필요
    std::cout << "Thread " << tid << ": " << count << " active\n";
}
```

---

## 7. 사용 예시

### NetBuffer 누수 추적

```cpp
// NetBuffer::Alloc 래퍼에 추적 추가
NetBuffer* AllocTracked(const char* file, int line) {
    auto* buf = NetBuffer::Alloc();
    if (MemoryTracer::IsEnabled()) {
        MemoryTracer::TrackObject(buf, "NetBuffer", file, line);
    }
    return buf;
}

void FreeTracked(NetBuffer* buf, const char* file, int line) {
    if (MemoryTracer::IsEnabled()) {
        MemoryTracer::UntrackObject(buf, file, line);
    }
    NetBuffer::Free(buf);
}

#define NetBuffer_Alloc() AllocTracked(__FILE__, __LINE__)
#define NetBuffer_Free(buf) FreeTracked(buf, __FILE__, __LINE__)
```

### 서버 종료 후 보고서 생성

```cpp
core.StopServer();

// 종료 후 미해제 객체 확인
if (MemoryTracer::IsEnabled()) {
    size_t active = MemoryTracer::GetActiveObjectCount();
    if (active > 0) {
        LOG_ERROR(std::format("Memory leak detected: {} objects", active));
        MemoryTracer::GenerateReportToFile("memory_leak_report.txt");
    } else {
        LOG_DEBUG("No memory leaks detected");
    }
}
```

### SendPacketInfo 풀 추적

```cpp
// CTLSMemoryPool<SendPacketInfo>::Alloc 래퍼
SendPacketInfo* AllocSendPacketInfo() {
    auto* info = sendPacketInfoPool->Alloc();
    MemoryTracer::TrackObject(info, "SendPacketInfo", __FILE__, __LINE__);
    return info;
}

void FreeSendPacketInfo(SendPacketInfo* info) {
    MemoryTracer::UntrackObject(info, __FILE__, __LINE__);
    sendPacketInfoPool->Free(info);
}
```

---

## 8. 성능 주의사항

| 연산 | 비용 | 이유 |
|------|------|------|
| `TrackObject()` | ~수백 μs | `CaptureStackBackTrace` + `SymFromAddr` |
| `UntrackObject()` | ~수십 μs | map 조회 + mutex |
| `GenerateReport()` | 전체 map 순회 | 보고서 생성 시에만 |
| `GetActiveObjectCount()` | O(N) | map 순회 |

**운영 환경에서 완전 제거 방법:**

```cpp
// 조건부 컴파일
#ifdef _DEBUG
#define TRACK(ptr, type) MemoryTracer::TrackObject(ptr, type, __FILE__, __LINE__)
#define UNTRACK(ptr)     MemoryTracer::UntrackObject(ptr, __FILE__, __LINE__)
#else
#define TRACK(ptr, type) ((void)0)
#define UNTRACK(ptr)     ((void)0)
#endif
```

---

## 관련 문서
- [[MultiSocketRUDPCore]] — 서버 종료 후 누수 보고서 생성
- [[TroubleShooting]] — 메모리 사용량 증가 디버깅
