#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>

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
    static void GenerateReport();
    static void GetObjectHistory(void* ptr);
    static void GetThreadStatistics();

    static void SetOutputFile(const std::string& filename);
    static void CloseOutputFile();
    static void GenerateReportToFile(const std::string& filename = "");
    static void GetObjectHistoryToFile(void* ptr, const std::string& filename = "");
    static void GetThreadStatisticsToFile(const std::string& filename = "");

private:
    static std::string outputFilename;
    static void WriteToOutput(const std::string& message, bool forceConsole = false);
    static std::string GetCurrentTimestamp();
};