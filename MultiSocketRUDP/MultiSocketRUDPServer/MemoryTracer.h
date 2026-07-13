#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <ostream>

class MemoryTracer
{
private:
    struct AllocationInfo
	{
        std::string objectName;
    	std::string allocLocation;
        std::string stackTrace;
    	std::chrono::time_point<std::chrono::steady_clock> allocTime;
        std::thread::id allocThread;
        bool isFreed = false;
        std::string freeLocation;
        std::chrono::time_point<std::chrono::steady_clock> freeTime;
        std::thread::id freeThread;
        std::string userNote;
	};

    static std::unordered_map<void*, AllocationInfo> allocations;
    static std::mutex tracerMutex;
    static std::atomic<bool> enabled;
    static std::atomic<size_t> nextId;

public:
    static void Enable();
    static void Disable();
    static bool IsEnabled();

    static void Clear();

    static std::string GetStackTrace();

    static void TrackObject(void* ptr,
        const std::string& objectName,
        const std::string& file,
        int line,
        const std::string& note = "");
    static void UntrackObject(void* ptr, const std::string& file, int line);

	static void AddNote(void* ptr, const std::string& note);

    static size_t GetActiveObjectCount();
    /**
    * @brief `tracerMutex`를 잠금으로써 스레드 안전하게 현재 메모리 누수 보고서를 `std::cout`에 생성합니다.
    *
    * 내부적으로 `WriteLeakReport` 함수를 호출하여 메모리 누수 보고서를 표준 출력 스트림으로 보냅니다.
    * 이 보고서에는 타임스탬프가 포함되지 않습니다.
    *
    * @sideeffect `std::cout`에 메모리 누수 보고서를 출력합니다.
    * @statechange `tracerMutex`가 함수 실행 동안 잠금 상태로 유지됩니다.
    * @failurecondition `WriteLeakReport`의 실패 조건과 동일합니다. 즉, `allocations` 맵이 비어 있으면 내용이 없거나 누수가 없다고 표시됩니다.
    */
    static void GenerateReport();
    /**
    * @brief `tracerMutex`를 잠금으로써 스레드 안전하게 특정 객체의 메모리 이력을 `std::cout`에 생성합니다.
    *
    * 내부적으로 `WriteObjectHistory` 함수를 호출하여 주어진 `ptr`에 해당하는 객체의 이력을 표준 출력 스트림으로 보냅니다.
    * 이 이력에는 타임스탬프가 포함되지 않습니다.
    *
    * @param ptr 이력을 조회할 객체의 메모리 주소입니다.
    *
    * @sideeffect `std::cout`에 객체 이력을 출력합니다.
    * @statechange `tracerMutex`가 함수 실행 동안 잠금 상태로 유지됩니다.
    * @failurecondition `ptr`이 추적기에서 발견되지 않으면 `std::cout`에 "Object not found in tracker" 메시지를 출력합니다.
    */
    static void GetObjectHistory(void* ptr);
    /**
    * @brief `tracerMutex`를 잠금으로써 스레드 안전하게 스레드별 메모리 할당 통계를 `std::cout`에 생성합니다.
    *
    * 내부적으로 `WriteThreadStatistics` 함수를 호출하여 활성 객체에 대한 스레드별 통계를 표준 출력 스트림으로 보냅니다.
    * 이 통계에는 타임스탬프가 포함되지 않습니다.
    *
    * @sideeffect `std::cout`에 스레드 통계를 출력합니다.
    * @statechange `tracerMutex`가 함수 실행 동안 잠금 상태로 유지됩니다.
    * @failurecondition 활성 객체가 없으면 `std::cout`에 통계 내용이 없거나 0개의 활성 객체로 표시됩니다.
    */
    static void GetThreadStatistics();

    static void SetOutputFile(const std::string& filename);
    static void CloseOutputFile();
    static void GenerateReportToFile(const std::string& filename = "");
    static void GetObjectHistoryToFile(void* ptr, const std::string& filename = "");
    static void GetThreadStatisticsToFile(const std::string& filename = "");

private:
    static std::string outputFilename;
    /**
    * @brief 현재 추적 중인 메모리 할당 중 해제되지 않은 객체(메모리 누수)에 대한 보고서를 `output` 스트림에 작성합니다.
    *
    * `allocations` 맵을 순회하며 `isFreed`가 `false`인 각 할당에 대해 상세 정보를 출력합니다.
    * 각 누수에 대해 주소, 객체 이름, 할당 위치, 할당 스레드, 사용자 정의 노트(있는 경우), 생존 시간 및 스택 트레이스를 기록합니다.
    * 보고서에는 추적된 총 객체 수와 활성(누수) 객체 수가 포함됩니다.
    *
    * @param output 보고서를 작성할 출력 스트림입니다. (예: `std::cout`, `std::stringstream`)
    * @param includeTimestamp `true`인 경우 보고서 시작 부분에 생성 시간을 포함합니다.
    *
    * @sideeffect `output` 스트림에 데이터를 작성합니다.
    * @failurecondition `allocations` 맵이 비어 있거나 모든 객체가 해제된 경우, 누수 보고서 섹션에는 내용이 없거나 누수가 없다고 표시됩니다.
    */
    static void WriteLeakReport(std::ostream& output, bool includeTimestamp);
    /**
    * @brief 특정 메모리 주소(`ptr`)에 해당하는 객체의 할당 및 해제 이력을 `output` 스트림에 작성합니다.
    *
    * `allocations` 맵에서 `ptr`을 찾아 해당 객체의 전체 이력(객체 이름, 할당 위치, 스택 트레이스, 할당 시간, 스레드, 사용자 노트)을 출력합니다.
    * 객체가 이미 해제된 경우(즉, `isFreed`가 `true`인 경우), 해제 위치, 해제 스레드 및 객체의 총 수명도 함께 출력합니다.
    * 객체가 아직 해제되지 않은 경우 "Status: ACTIVE (not untracked)"를 표시합니다.
    *
    * @param output 이력을 작성할 출력 스트림입니다. (예: `std::cout`, `std::stringstream`)
    * @param ptr 이력을 조회할 객체의 메모리 주소입니다.
    * @param includeTimestamp `true`인 경우 이력 시작 부분에 타임스탬프를 포함합니다.
    *
    * @sideeffect `output` 스트림에 데이터를 작성합니다.
    * @failurecondition `ptr`이 `allocations` 맵에서 발견되지 않으면 "Object not found in tracker" 메시지를 `output` 스트림에 작성합니다.
    */
    static void WriteObjectHistory(std::ostream& output, void* ptr, bool includeTimestamp);
    /**
    * @brief 현재 추적 중인 활성 메모리 객체들에 대한 스레드별 통계를 `output` 스트림에 작성합니다.
    *
    * `allocations` 맵을 순회하며 `isFreed`가 `false`인 각 객체에 대해 할당을 수행한 스레드의 ID를 기준으로 활성 객체 수를 집계합니다.
    * 각 스레드 ID와 해당 스레드가 할당한 활성 객체 수를 출력합니다.
    *
    * @param output 통계를 작성할 출력 스트림입니다. (예: `std::cout`, `std::stringstream`)
    * @param includeTimestamp `true`인 경우 통계 시작 부분에 생성 시간을 포함합니다.
    *
    * @sideeffect `output` 스트림에 데이터를 작성합니다.
    * @failurecondition 현재 활성 객체가 없으면 통계 보고서 섹션에는 내용이 없거나 0개의 활성 객체로 표시됩니다.
    */
    static void WriteThreadStatistics(std::ostream& output, bool includeTimestamp);
    static void WriteToOutput(const std::string& message, bool forceConsole = false);
    static void WriteReport(const std::string& filename, const std::string& report);
    static std::string GetCurrentTimestamp();
};
