#include "PreCompile.h"
#include "MemoryTracer.h"

#include <fstream>
#include <ranges>

std::unordered_map<void*, MemoryTracer::AllocationInfo> MemoryTracer::allocations;
std::mutex MemoryTracer::tracerMutex;
std::atomic<bool> MemoryTracer::enabled{ true };
std::atomic<size_t> MemoryTracer::nextId{ 1 };
std::string MemoryTracer::outputFilename = "MemoryTracer.log";

void MemoryTracer::Enable()
{
	enabled.store(true);
}

void MemoryTracer::Disable()
{
	enabled.store(false);
}

bool MemoryTracer::IsEnabled()
{
	return enabled.load();
}

std::string MemoryTracer::GetStackTrace()
{
	std::string trace;

	void* stack[15];
	HANDLE process = GetCurrentProcess();

	static std::once_flag initFlag;
	std::call_once(initFlag, [process]()
		{
			SymInitialize(process, nullptr, TRUE);
		});

	const WORD frames = CaptureStackBackTrace(2, 15, stack, nullptr);
	for (int i = 0; i < frames; ++i)
	{
		const DWORD64 address = reinterpret_cast<DWORD64>(stack[i]);
		char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
		const auto symbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = MAX_SYM_NAME;

		if (SymFromAddr(process, address, nullptr, symbol))
		{
			trace += "    " + std::string(symbol->Name) + "\n";
		}
	}

	return trace;
}

void MemoryTracer::TrackObject(void* ptr,
	const std::string& objectName,
	const std::string& file,
	const int line,
	const std::string& note)
{
	if (enabled.load() == false || ptr == nullptr)
	{
		return;
	}

	std::scoped_lock lock(tracerMutex);
	AllocationInfo info;
	info.objectName = objectName;
	info.allocLocation = file + ":" + std::to_string(line);
	info.stackTrace = GetStackTrace();
	info.allocTime = std::chrono::steady_clock::now();
	info.allocThread = std::this_thread::get_id();
	info.userNote = note;

	allocations[ptr] = std::move(info);
}

void MemoryTracer::UntrackObject(void* ptr, const std::string& file, const int line)
{
	if (enabled.load() == false || ptr == nullptr)
	{
		return;
	}

	std::scoped_lock lock(tracerMutex);
	if (const auto itor = allocations.find(ptr); itor != allocations.end())
	{
		itor->second.isFreed = true;
		itor->second.freeLocation = file + ":" + std::to_string(line);
		itor->second.freeTime = std::chrono::steady_clock::now();
		itor->second.freeThread = std::this_thread::get_id();
	}
}

void MemoryTracer::AddNote(void* ptr, const std::string& note)
{
	if (enabled.load() == false || ptr == nullptr)
	{
		return;
	}

	std::scoped_lock lock(tracerMutex);

	if (const auto it = allocations.find(ptr); it != allocations.end())
	{
		if (not it->second.userNote.empty())
		{
			it->second.userNote += " | ";
		}

		it->second.userNote += note;
	}
}

size_t MemoryTracer::GetActiveObjectCount()
{
	std::scoped_lock lock(tracerMutex);

	size_t count = 0;
	for (const auto& allocationInfo : allocations | std::views::values)
	{
		if (not allocationInfo.isFreed)
		{
			count++;
		}
	}
	return count;
}

void MemoryTracer::WriteLeakReport(std::ostream& output, const bool includeTimestamp)
{
	output << "\n=== Memory Leak Report ===" << '\n';
	if (includeTimestamp)
	{
		output << "Generated at: " << GetCurrentTimestamp() << '\n';
	}
	output << "Total tracked objects: " << allocations.size() << '\n';

	int leakCount = 0;
	for (const auto& [address, allocationInfo] : allocations)
	{
		if (const auto& info = allocationInfo; not info.isFreed)
		{
			leakCount++;
			output << "\n[LEAK #" << leakCount << "]" << '\n';
			output << "Address: " << address << '\n';
			output << "Object: " << info.objectName << '\n';
			output << "Tracked at: " << info.allocLocation << '\n';
			output << "Thread: " << info.allocThread << '\n';

			if (not info.userNote.empty())
			{
				output << "Note: " << info.userNote << '\n';
			}

			auto now = std::chrono::steady_clock::now();
			const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.allocTime).count();
			output << "Alive for: " << duration << " ms" << '\n';
			output << "Stack trace:\n" << info.stackTrace << '\n';
		}
	}

	output << "Active objects: " << leakCount << '\n';
	output << "=========================\n" << '\n';
}

void MemoryTracer::WriteObjectHistory(std::ostream& output, void* ptr, const bool includeTimestamp)
{
	if (const auto it = allocations.find(ptr); it != allocations.end())
	{
		const auto& [objectName, allocLocation, stackTrace, allocTime, allocThread, isFreed, freeLocation, freeTime, freeThread, userNote] = it->second;
		output << "\n=== Object History ===" << '\n';
		if (includeTimestamp)
		{
			output << "Timestamp: " << GetCurrentTimestamp() << '\n';
		}
		output << "Address: " << ptr << '\n';
		output << "Object: " << objectName << '\n';
		output << "Tracked at: " << allocLocation << '\n';
		output << "Thread: " << allocThread << '\n';

		if (not userNote.empty())
		{
			output << "Note: " << userNote << '\n';
		}

		output << "Stack trace:\n" << stackTrace << '\n';

		if (isFreed)
		{
			output << "Untracked at: " << freeLocation << '\n';
			output << "Untrack thread: " << freeThread << '\n';

			const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(freeTime - allocTime).count();
			output << "Lifetime: " << duration << " ms" << '\n';
		}
		else
		{
			output << "Status: ACTIVE (not untracked)" << '\n';
		}
		output << "=====================\n" << '\n';
	}
	else
	{
		output << "Object not found in tracker";
		if (includeTimestamp)
		{
			output << " at " << GetCurrentTimestamp();
		}
		output << '\n';
	}
}

void MemoryTracer::WriteThreadStatistics(std::ostream& output, const bool includeTimestamp)
{
	std::unordered_map<std::thread::id, int> threadStats;

	for (const auto& allocation : allocations | std::views::values)
	{
		if (not allocation.isFreed)
		{
			threadStats[allocation.allocThread]++;
		}
	}

	output << "\n=== Thread Statistics ===" << '\n';
	if (includeTimestamp)
	{
		output << "Generated at: " << GetCurrentTimestamp() << '\n';
	}
	for (const auto& [threadId, status] : threadStats)
	{
		output << "Thread " << threadId << ": " << status << " active objects" << '\n';
	}
	output << "========================\n" << '\n';
}

void MemoryTracer::GenerateReport()
{
	std::scoped_lock lock(tracerMutex);
	WriteLeakReport(std::cout, false);
}

void MemoryTracer::GetObjectHistory(void* ptr)
{
	std::scoped_lock lock(tracerMutex);
	WriteObjectHistory(std::cout, ptr, false);
}

void MemoryTracer::GetThreadStatistics()
{
	std::scoped_lock lock(tracerMutex);
	WriteThreadStatistics(std::cout, false);
}

void MemoryTracer::Clear()
{
	std::scoped_lock lock(tracerMutex);
	allocations.clear();
}

void MemoryTracer::SetOutputFile(const std::string& filename)
{
	outputFilename = filename;
	std::scoped_lock lock(tracerMutex);
	if (not filename.empty())
	{
		if (std::ofstream file(filename, std::ios::trunc); file.is_open())
		{
			file << "=== Memory Tracer Log Started ===" << '\n';
			file << "Timestamp: " << GetCurrentTimestamp() << '\n';
			file << "=====================================" << '\n' << '\n';
			file.close();
		}
	}
}

void MemoryTracer::CloseOutputFile()
{
	std::scoped_lock lock(tracerMutex);
	if (not outputFilename.empty())
	{
		std::ofstream file(outputFilename, std::ios::app);
		if (file.is_open())
		{
			file << '\n' << "=== Memory Tracer Log Ended ===" << '\n';
			file << "Timestamp: " << GetCurrentTimestamp() << '\n';
			file << "===================================" << '\n';
			file.close();
		}

		outputFilename.clear();
	}
}

std::string MemoryTracer::GetCurrentTimestamp()
{
	const auto now = std::chrono::system_clock::now();
	const auto time = std::chrono::system_clock::to_time_t(now);
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

	std::tm localTime{};
	if (localtime_s(&localTime, &time) != 0)
	{
		return "";
	}

	std::stringstream ss;
	ss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
	ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
	return ss.str();
}

void MemoryTracer::WriteToOutput(const std::string& message, const bool forceConsole)
{
	if (forceConsole || outputFilename.empty())
	{
		std::cout << message;
	}
	else
	{
		if (std::ofstream file(outputFilename, std::ios::app); file.is_open())
		{
			file << message;
			file.close();
		}
		else
		{
			std::cout << message;
		}
	}
}

void MemoryTracer::WriteReport(const std::string& filename, const std::string& report)
{
	if (filename.empty())
	{
		WriteToOutput(report);
		return;
	}

	if (std::ofstream file(filename, std::ios::app); file.is_open())
	{
		file << report;
	}
	else
	{
		std::cout << report;
	}
}

void MemoryTracer::GenerateReportToFile(const std::string& filename)
{
	std::scoped_lock lock(tracerMutex);
	std::stringstream report;
	WriteLeakReport(report, true);
	WriteReport(filename, report.str());
}

void MemoryTracer::GetObjectHistoryToFile(void* ptr, const std::string& filename)
{
	std::scoped_lock lock(tracerMutex);
	std::stringstream history;
	WriteObjectHistory(history, ptr, true);
	WriteReport(filename, history.str());
}

void MemoryTracer::GetThreadStatisticsToFile(const std::string& filename)
{
	std::scoped_lock lock(tracerMutex);
	std::stringstream statistics;
	WriteThreadStatistics(statistics, true);
	WriteReport(filename, statistics.str());
}
