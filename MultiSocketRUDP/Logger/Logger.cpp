#include "PreCompile.h"
#include "Logger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <limits>
#include <process.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    constexpr std::size_t MAX_QUEUED_LOGS=4096;
    constexpr std::size_t MAX_QUEUED_DIAGNOSTICS=256;
    constexpr std::size_t BATCH_SIZE=32;
    constexpr DWORD STOP_WAIT_MS=250,CANCEL_WAIT_MS=50;

    struct HandleCloser
    {
        void operator()(HANDLE inHandle) const noexcept
        {
            if(inHandle && inHandle!=INVALID_HANDLE_VALUE)CloseHandle(inHandle);
        }
    };
    using UniqueHandle=std::unique_ptr<void,HandleCloser>;

    bool WriteAll(HANDLE inHandle,const std::string& inText,const bool inAppend=false) noexcept
    {
        if(!inHandle || inHandle==INVALID_HANDLE_VALUE)return false;
        std::size_t offset{};
        while(offset<inText.size())
        {
            DWORD written{};
            const auto length=static_cast<DWORD>(std::min<std::size_t>(inText.size()-offset,MAXDWORD));
            // Synchronous writes complete before the stack OVERLAPPED is released.
            // EOF offsets preserve append semantics with GENERIC_WRITE required by FlushFileBuffers.
            OVERLAPPED position{};
            position.Offset=MAXDWORD;
            position.OffsetHigh=MAXDWORD;
            if(!WriteFile(inHandle,inText.data()+offset,length,&written,inAppend ? &position : nullptr) || !written)return false;
            offset+=written;
        }
        return true;
    }
}

struct Logger::State
{
    struct Entry
    {
        std::shared_ptr<LogBase> log;
        std::chrono::system_clock::time_point capturedAt{};
        bool diagnostic{};
    };

    explicit State(bool inPrintToConsole):printToConsole(inPrintToConsole)
    {
        wake.reset(CreateEventW(nullptr,FALSE,FALSE,nullptr));
        if(!wake)throw std::runtime_error("Unable to create logger wake event.");
        const auto now=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm utc{};
        if(gmtime_s(&utc,&now))throw std::runtime_error("Unable to determine logger UTC file name.");
        char name[80]{};
        std::strftime(name,sizeof(name),"log_%Y-%m-%d_%H.txt",&utc);
        path=std::filesystem::current_path()/"Log Folder"/name;
    }

    void Drop(bool inDiagnostic) noexcept
    {
        if(inDiagnostic)++diagnosticDropped;else ++normalDropped;
        SetEvent(wake.get());
    }
    Statistics Snapshot() const noexcept
    {
        return {normalDropped.load(),diagnosticDropped.load(),serializationErrors.load(),
            fileErrors.load(),consoleErrors.load(),stopTimeouts.load()};
    }

    UniqueHandle wake;
    std::filesystem::path path;
    const bool printToConsole;
    std::mutex queueMutex;
    std::deque<Entry> queue;
    std::size_t queuedDiagnostics{};
    std::atomic_bool accepting{true};
    std::atomic<DWORD> workerId{};
    std::atomic_uint64_t normalDropped{},diagnosticDropped{},serializationErrors{};
    std::atomic_uint64_t fileErrors{},consoleErrors{},stopTimeouts{};
};

Logger::~Logger()
{
    // A timed-out worker owns State, not Logger. Never close its file/event from this thread.
    isAlive.store(false,std::memory_order_release);
    try
    {
        std::scoped_lock lock(lifecycleMutex);
        currentConnectedClientCount=0;
        StopWorker();
        if(loggerThread)CloseHandle(loggerThread);
    }
    catch(...)
    {
        const auto state=activeState.load(std::memory_order_acquire);
        if(state){state->accepting=false;SetEvent(state->wake.get());}
    }
}

Logger& Logger::GetInstance()
{
    static Logger instance;
    return instance;
}

void Logger::RunLoggerThread(const bool inPrintToConsole)
{
    std::scoped_lock lock(lifecycleMutex);
    if(currentConnectedClientCount)
    {
        if(currentConnectedClientCount==UINT32_MAX)throw std::runtime_error("Too many logger owners.");
        ++currentConnectedClientCount;
        return;
    }
    if(loggerThread)
    {
        // Never reuse storage or open a second writer while an old blocked writer still owns it.
        if(WaitForSingleObject(loggerThread,0)!=WAIT_OBJECT_0)
            throw std::runtime_error("Previous logger worker has not exited; restart is unavailable.");
        CloseHandle(loggerThread);
        loggerThread=nullptr;
    }
    auto state=std::make_shared<State>(inPrintToConsole);
    auto context=std::make_unique<std::shared_ptr<State>>(state);
    activeState.store(state,std::memory_order_release);
    stopWaited=false;
    isAlive.store(true,std::memory_order_release);
    loggerThread=reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,WorkerEntry,context.get(),0,nullptr));
    if(!loggerThread)
    {
        state->accepting=false;
        isAlive.store(false,std::memory_order_release);
        throw std::runtime_error("Unable to create logger worker.");
    }
    (void)context.release(); // WorkerEntry owns this context after a successful start.
    currentConnectedClientCount=1;
}

unsigned __stdcall Logger::WorkerEntry(void* inContext) noexcept
{
    const std::unique_ptr<std::shared_ptr<State>> context(static_cast<std::shared_ptr<State>*>(inContext));
    const auto state=*context;
    state->workerId=GetCurrentThreadId();
    RunWorker(state);
    state->accepting=false;
    // Open/worker failures can leave already accepted records behind. Count/discard them off producers.
    try
    {
        for(;;)
        {
            std::array<State::Entry,BATCH_SIZE> abandoned{};
            std::size_t count{};
            {
                std::scoped_lock lock(state->queueMutex);
                while(count<abandoned.size() && !state->queue.empty())
                {
                    abandoned[count]=std::move(state->queue.front());
                    state->queue.pop_front();
                    if(abandoned[count].diagnostic)--state->queuedDiagnostics;
                    ++count;
                }
            }
            if(!count)break;
            for(std::size_t index=0;index<count;++index)state->Drop(abandoned[index].diagnostic);
        }
    }
    catch(...){++state->serializationErrors;}
    isAlive.store(false,std::memory_order_release);
    return 0;
}

void Logger::Worker()
{
    const auto state=activeState.load(std::memory_order_acquire);
    // External callers cannot accidentally start a second consumer on the same file.
    if(state && state->workerId==GetCurrentThreadId())RunWorker(state);
}

Logger::Statistics Logger::GetStatistics() const noexcept
{
    const auto state=activeState.load(std::memory_order_acquire);
    return state ? state->Snapshot() : Statistics{};
}

void Logger::RecordDroppedDiagnostic() noexcept
{
    const auto state=activeState.load(std::memory_order_acquire);
    if(state)state->Drop(true);
}

bool Logger::Enqueue(const std::shared_ptr<State>& inState,
    std::shared_ptr<LogBase> inLog,const bool inDiagnostic) noexcept
{
    if(!inState)return false;
    if(!inLog || !inState->accepting.load(std::memory_order_acquire))
    {
        inState->Drop(inDiagnostic);
        return false;
    }
    try
    {
        State::Entry entry{std::move(inLog),std::chrono::system_clock::now(),inDiagnostic};
        std::unique_lock lock(inState->queueMutex,std::defer_lock);
        if(inDiagnostic)
        {
            if(!lock.try_lock()){inState->Drop(true);return false;}
        }
        else lock.lock();
        if(!inState->accepting.load(std::memory_order_acquire)
            || inState->queue.size()>=MAX_QUEUED_LOGS
            || (inDiagnostic && inState->queuedDiagnostics>=MAX_QUEUED_DIAGNOSTICS))
        {
            inState->Drop(inDiagnostic);
            return false;
        }
        inState->queue.push_back(std::move(entry));
        if(inDiagnostic)++inState->queuedDiagnostics;
        lock.unlock();
        SetEvent(inState->wake.get());
        return true;
    }
    catch(...)
    {
        inState->Drop(inDiagnostic);
        return false;
    }
}

void Logger::WriteLog(std::shared_ptr<LogBase> inLog) noexcept
{
    (void)Enqueue(activeState.load(std::memory_order_acquire),std::move(inLog),false);
}

bool Logger::TryWriteLog(std::shared_ptr<LogBase> inLog) noexcept
{
    return Enqueue(activeState.load(std::memory_order_acquire),std::move(inLog),true);
}

void Logger::StopWorker() noexcept
{
    if(stopWaited)return;
    stopWaited=true;
    isAlive.store(false,std::memory_order_release);
    const auto state=activeState.load(std::memory_order_acquire);
    if(state){state->accepting.store(false,std::memory_order_release);SetEvent(state->wake.get());}
    if(!loggerThread)return;
    DWORD result=WaitForSingleObject(loggerThread,STOP_WAIT_MS);
    if(result!=WAIT_OBJECT_0)
    {
        if(state)++state->stopTimeouts;
        (void)CancelSynchronousIo(loggerThread);
        result=WaitForSingleObject(loggerThread,CANCEL_WAIT_MS);
    }
    if(result==WAIT_OBJECT_0)
    {
        CloseHandle(loggerThread);
        loggerThread=nullptr;
    }
    // On timeout retain the handle for restart checks. The worker's shared State retains all resources.
}

void Logger::StopLoggerThread() noexcept
{
    try
    {
        std::scoped_lock lock(lifecycleMutex);
        if(!currentConnectedClientCount)return;
        if(--currentConnectedClientCount)return;
        StopWorker();
    }
    catch(...)
    {
        isAlive.store(false,std::memory_order_release);
        const auto state=activeState.load(std::memory_order_acquire);
        if(state){state->accepting=false;SetEvent(state->wake.get());}
    }
}

void Logger::RunWorker(const std::shared_ptr<State>& inState) noexcept
{
    try
    {
        std::error_code error;
        std::filesystem::create_directories(inState->path.parent_path(),error);
        UniqueHandle file;
        if(!error)
            file.reset(CreateFileW(inState->path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(error || !file || file.get()==INVALID_HANDLE_VALUE)
        {
            ++inState->fileErrors;
            inState->accepting=false;
            // No producer waits for this failure notice; shutdown can cancel a blocked console write.
            if(inState->printToConsole)
            {
                const std::string notice="[Logger] event=file_open_failed; logging disabled.\n";
                (void)WriteAll(GetStdHandle(STD_ERROR_HANDLE),notice);
            }
            return;
        }
        Statistics reported{};
        bool fileHealthy=true,consoleHealthy=true;
        const auto reportCounters=[&]()
        {
            const auto counts=inState->Snapshot();
            if(counts==reported)return;
            try
            {
                const auto now=std::chrono::system_clock::now();
                // Same JSON envelope as ordinary logs. No recursive enqueue or console output.
                struct StatisticsLog final : LogBase
                {
                    Statistics counts;
                    std::int64_t utcMs{};
                    nlohmann::json ObjectToJson() override
                    {
                        return {{"logString","[Logger] event=logger_statistics"},{"event","logger_statistics"},
                            {"utc_ms",utcMs},{"normal_dropped",counts.normalDropped},
                            {"diagnostic_dropped",counts.diagnosticDropped},
                            {"serialization_errors",counts.serializationErrors},{"file_errors",counts.fileErrors},
                            {"console_errors",counts.consoleErrors},{"stop_timeouts",counts.stopTimeouts}};
                    }
                } report;
                report.counts=counts;
                report.utcMs=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                LogBase& base=report;
                base.SetLogTime(now);
                auto bytes=base.ObjectToJsonImpl().dump();bytes.push_back('\n');
                if(!WriteAll(file.get(),bytes,true))
                {++inState->fileErrors;inState->accepting=false;fileHealthy=false;SetEvent(inState->wake.get());}
            }
            catch(...){++inState->serializationErrors;}
            reported=counts;
        };
        for(;;)
        {
            if(WaitForSingleObject(inState->wake.get(),INFINITE)!=WAIT_OBJECT_0)break;
            bool done{};
            do
            {
                std::array<State::Entry,BATCH_SIZE> batch{};
                std::size_t count{};
                {
                    std::scoped_lock lock(inState->queueMutex);
                    while(count<batch.size() && !inState->queue.empty())
                    {
                        batch[count]=std::move(inState->queue.front());
                        inState->queue.pop_front();
                        if(batch[count].diagnostic)--inState->queuedDiagnostics;
                        ++count;
                    }
                    done=!inState->accepting.load(std::memory_order_acquire) && inState->queue.empty();
                }
                for(std::size_t index=0;index<count;++index)
                {
                    const auto& entry=batch[index];
                    if(!fileHealthy){inState->Drop(entry.diagnostic);continue;}
                    try
                    {
                        entry.log->SetLogTime(entry.capturedAt);
                        auto bytes=entry.log->ObjectToJsonImpl().dump();bytes.push_back('\n');
                        if(!WriteAll(file.get(),bytes,true))
                        {
                            ++inState->fileErrors;inState->accepting=false;fileHealthy=false;
                            inState->Drop(entry.diagnostic);
                        }
                        else if(!entry.diagnostic && inState->printToConsole && consoleHealthy
                            && inState->accepting.load(std::memory_order_acquire))
                        {
                            // Console backpressure is confined to this worker, never the queue lock.
                            if(!WriteAll(GetStdHandle(STD_OUTPUT_HANDLE),bytes))
                            {++inState->consoleErrors;consoleHealthy=false;}
                        }
                    }
                    catch(...)
                    {
                        ++inState->serializationErrors;
                        inState->Drop(entry.diagnostic);
                    }
                }
                if(fileHealthy)reportCounters();
                if(done)break;
                if(!count)break;
            } while(true);
            if(done)break;
        }
        if(fileHealthy)
        {
            reportCounters();
            if(!FlushFileBuffers(file.get()))++inState->fileErrors;
        }
    }
    catch(...)
    {
        ++inState->serializationErrors;
        inState->accepting=false;
    }
}
