#include "PreCompile.h"
#include "RUDPThreadManager.h"

#include <cassert>
#include <ranges>

#include "LogExtension.h"
#include "Logger.h"

RUDPThreadManager::~RUDPThreadManager()
{
	StopAllThreads();
}

void RUDPThreadManager::StartThreads(const THREAD_GROUP threadGroup, std::function<void(std::stop_token, unsigned char)> threadFunction, const uint8_t numOfThreads)
{
	if (threadGroups.contains(threadGroup))
	{
		LOG_ERROR(std::format("StartThreads : Duplicated thread group {}", static_cast<uint8_t>(threadGroup)));
		return;
	}

	std::vector<std::jthread> threads;
	threads.reserve(numOfThreads);
	for (unsigned char i = 0; i < numOfThreads; ++i)
	{
		threads.emplace_back([threadFunction, i](const std::stop_token& stopToken) { threadFunction(stopToken, i); });
	}
	threadGroups.emplace(threadGroup, std::move(threads));
}

void RUDPThreadManager::StopThreadGroup(const THREAD_GROUP threadGroup)
{
	const auto itor = threadGroups.find(threadGroup);
	if (itor == threadGroups.end())
	{
		return;
	}

	for (auto& thread : itor->second)
	{
		if (thread.joinable())
		{
			thread.request_stop();
		}
	}

	itor->second.clear();
	threadGroups.erase(itor);
}

void RUDPThreadManager::StopAllThreads()
{
	for (auto& threads : threadGroups | std::views::values)
	{
		for (auto& thread : threads)
		{
			if (thread.joinable())
			{
				thread.request_stop();
			}
		}

		threads.clear();
	}

	threadGroups.clear();
}