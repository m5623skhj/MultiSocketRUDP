#pragma once
#include <WinSock2.h>
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include "LogClass.h"

class Logger
{
private:
    Logger() = default;
    ~Logger();

public:
    static Logger& GetInstance();
    static bool IsAlive() noexcept { return isAlive.load(std::memory_order_acquire); }

    void RunLoggerThread(bool inPrintToConsole);
    void Worker(); // Internal worker entry retained for source compatibility.
    void StopLoggerThread() noexcept;

    template<typename LogType>
    requires std::is_base_of_v<LogBase, LogType>
    static std::shared_ptr<LogType> MakeLogObject()
    {
        return std::make_shared<LogType>();
    }

    struct Statistics
    {
        std::uint64_t normalDropped{},diagnosticDropped{},serializationErrors{};
        std::uint64_t fileErrors{},consoleErrors{},stopTimeouts{};
        bool operator==(const Statistics&) const = default;
    };
    [[nodiscard]] Statistics GetStatistics() const noexcept;

    // Ordinary logs retain FIFO/mirroring. Admission is bounded and failure never escapes.
    void WriteLog(std::shared_ptr<LogBase> inLog) noexcept;
    // Diagnostics never wait for the queue mutex and never mirror to the console.
    [[nodiscard]] bool TryWriteLog(std::shared_ptr<LogBase> inLog) noexcept;
    void RecordDroppedDiagnostic() noexcept;

private:
    struct State;
    static unsigned __stdcall WorkerEntry(void* inContext) noexcept;
    static void RunWorker(const std::shared_ptr<State>& inState) noexcept;
    static bool Enqueue(const std::shared_ptr<State>& inState,
        std::shared_ptr<LogBase> inLog, bool inDiagnostic) noexcept;
    void StopWorker() noexcept; // lifecycleMutex held.

    std::mutex lifecycleMutex;
    std::atomic<std::shared_ptr<State>> activeState{};
    HANDLE loggerThread{};
    std::uint32_t currentConnectedClientCount{};
    bool stopWaited{};
    static inline std::atomic_bool isAlive{};
};

namespace LogHelper
{
    template<typename T>
    std::shared_ptr<T> MakeLogObject()
    requires std::is_base_of_v<LogBase, T>
    {
        return std::make_shared<T>();
    }
}
