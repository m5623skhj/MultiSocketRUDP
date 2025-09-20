#include "PreCompile.h"
#include "MemoryTracer.h"

#include <fstream>
#include <ranges>
#include <vector>

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

void MemoryTracer::UntrackObject(void* ptr,
	const std::string& file,
	const int line)
{
	if (enabled.load() == false || ptr == nullptr)
	{
		return;
	}

	std::scoped_lock lock(tracerMutex);
	if (const auto it = allocations.find(ptr); it != allocations.end())
	{
		it->second.isFreed = true;
		it->second.freeLocation = file + ":" + std::to_string(line);
		it->second.freeTime = std::chrono::steady_clock::now();
		it->second.freeThread = std::this_thread::get_id();
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

void MemoryTracer::GenerateReport()
{
	std::scoped_lock lock(tracerMutex);

	std::cout << "\n=== Memory Leak Report ===" << '\n';
	std::cout << "Total tracked objects: " << allocations.size() << '\n';

	int leakCount = 0;
	for (const auto& pair : allocations)
	{
		if (const auto& info = pair.second; not info.isFreed)
		{
			leakCount++;
			std::cout << "\n[LEAK #" << leakCount << "]" << '\n';
			std::cout << "Address: " << pair.first << '\n';
			std::cout << "Object: " << info.objectName << '\n';
			std::cout << "Tracked at: " << info.allocLocation << '\n';
			std::cout << "Thread: " << info.allocThread << '\n';

			if (not info.userNote.empty())
			{
				std::cout << "Note: " << info.userNote << '\n';
			}

			auto now = std::chrono::steady_clock::now();
			const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.allocTime).count();
			std::cout << "Alive for: " << duration << " ms" << '\n';

			std::cout << "Stack trace:\n" << info.stackTrace << '\n';
		}
	}

	std::cout << "Active objects: " << leakCount << '\n';
	std::cout << "=========================\n" << '\n';
}

void MemoryTracer::GetObjectHistory(void* ptr)
{
	std::scoped_lock lock(tracerMutex);

	if (const auto it = allocations.find(ptr); it != allocations.end())
	{
		const auto& info = it->second;
		std::cout << "\n=== Object History ===" << '\n';
		std::cout << "Address: " << ptr << '\n';
		std::cout << "Object: " << info.objectName << '\n';
		std::cout << "Tracked at: " << info.allocLocation << '\n';
		std::cout << "Thread: " << info.allocThread << '\n';

		if (not info.userNote.empty())
		{
			std::cout << "Note: " << info.userNote << '\n';
		}

		std::cout << "Stack trace:\n" << info.stackTrace << '\n';

		if (info.isFreed)
		{
			std::cout << "Untracked at: " << info.freeLocation << '\n';
			std::cout << "Untrack thread: " << info.freeThread << '\n';

			const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(info.freeTime - info.allocTime).count();
			std::cout << "Lifetime: " << duration << " ms" << '\n';
		}
		else
		{
			std::cout << "Status: ACTIVE (not untracked)" << '\n';
		}
		std::cout << "=====================\n" << '\n';
	}
	else
	{
		std::cout << "Object not found in tracker" << '\n';
	}
}

void MemoryTracer::GetThreadStatistics()
{
	std::unordered_map<std::thread::id, int> threadStats;

	std::scoped_lock lock(tracerMutex);
	for (const auto& pair : allocations)
	{
		if (not pair.second.isFreed)
		{
			threadStats[pair.second.allocThread]++;
		}
	}

	std::cout << "\n=== Thread Statistics ===" << '\n';
	for (const auto& stat : threadStats)
	{
		std::cout << "Thread " << stat.first << ": " << stat.second << " active objects" << '\n';
	}
	std::cout << "========================\n" << '\n';
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

void MemoryTracer::GenerateReportToFile(const std::string& filename)
{
	std::scoped_lock lock(tracerMutex);

	std::stringstream report;
	report << "\n=== Memory Leak Report ===" << '\n';
	report << "Generated at: " << GetCurrentTimestamp() << '\n';
	report << "Total tracked objects: " << allocations.size() << '\n';

	int leakCount = 0;
	for (const auto& pair : allocations)
	{
		if (const auto& info = pair.second; not info.isFreed)
		{
			leakCount++;
			report << "\n[LEAK #" << leakCount << "]" << '\n';
			report << "Address: " << pair.first << '\n';
			report << "Object: " << info.objectName << '\n';
			report << "Tracked at: " << info.allocLocation << '\n';
			report << "Thread: " << info.allocThread << '\n';

			if (not info.userNote.empty())
			{
				report << "Note: " << info.userNote << '\n';
			}

			auto now = std::chrono::steady_clock::now();
			const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.allocTime).count();
			report << "Alive for: " << duration << " ms" << '\n';
			report << "Stack trace:\n" << info.stackTrace << '\n';
		}
	}

	report << "Active objects: " << leakCount << '\n';
	report << "=========================\n" << '\n';

	if (not filename.empty())
	{
		if (std::ofstream file(filename, std::ios::app); file.is_open())
		{
			file << report.str();
			file.close();
		}
		else
		{
			std::cout << report.str();
		}
	}
	else
	{
		WriteToOutput(report.str());
	}
}

void MemoryTracer::GetObjectHistoryToFile(void* ptr, const std::string& filename)
{
	std::stringstream history;
	std::scoped_lock lock(tracerMutex);
	if (const auto it = allocations.find(ptr); it != allocations.end())
	{
		const auto& info = it->second;
		history << "\n=== Object History ===" << '\n';
		history << "Timestamp: " << GetCurrentTimestamp() << '\n';
		history << "Address: " << ptr << '\n';
		history << "Object: " << info.objectName << '\n';
		history << "Tracked at: " << info.allocLocation << '\n';
		history << "Thread: " << info.allocThread << '\n';

		if (not info.userNote.empty())
		{
			history << "Note: " << info.userNote << '\n';
		}
		history << "Stack trace:\n" << info.stackTrace << '\n';

		if (info.isFreed)
		{
			history << "Untracked at: " << info.freeLocation << '\n';
			history << "Untrack thread: " << info.freeThread << '\n';

			const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(info.freeTime - info.allocTime).count();
			history << "Lifetime: " << duration << " ms" << '\n';
		}
		else
		{
			history << "Status: ACTIVE (not untracked)" << '\n';
		}
		history << "=====================\n" << '\n';
	}
	else
	{
		history << "Object not found in tracker at " << GetCurrentTimestamp() << '\n';
	}

	if (not filename.empty())
	{
		if (std::ofstream file(filename, std::ios::app); file.is_open())
		{
			file << history.str();
			file.close();
		}
		else
		{
			std::cout << history.str();
		}
	}
	else
	{
		WriteToOutput(history.str());
	}
}

void MemoryTracer::GetThreadStatisticsToFile(const std::string& filename)
{
	std::unordered_map<std::thread::id, int> threadStats;
	std::scoped_lock lock(tracerMutex);
	for (const auto& pair : allocations)
	{
		if (not pair.second.isFreed)
		{
			threadStats[pair.second.allocThread]++;
		}

		std::stringstream stats;
		stats << "\n=== Thread Statistics ===" << '\n';
		stats << "Generated at: " << GetCurrentTimestamp() << '\n';
		for (const auto& stat : threadStats)
		{
			stats << "Thread " << stat.first << ": " << stat.second << " active objects" << '\n';
		}
		stats << "========================\n" << '\n';
		if (not filename.empty())
		{
			if (std::ofstream file(filename, std::ios::app); file.is_open())
			{
				file << stats.str();
				file.close();
			}
			else
			{
				std::cout << stats.str();
			}
		}
		else
		{
			WriteToOutput(stats.str());
		}
	}
}