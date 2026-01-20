#pragma once

#include <thread>
#include <map>
#include <vector>
#include <functional>

enum class THREAD_GROUP : uint8_t;

class RUDPThreadManager
{
public:
	RUDPThreadManager() = default;
	~RUDPThreadManager();

public:
	void StartThreads(const THREAD_GROUP threadGroup, std::function<void(std::stop_token, unsigned char)> threadFunction, const uint8_t numOfThreads);
	void StopThreadGroup(const THREAD_GROUP threadGroup);
	void StopAllThreads();

private:
	std::map<THREAD_GROUP, std::vector<std::jthread>> threadGroups;
};
