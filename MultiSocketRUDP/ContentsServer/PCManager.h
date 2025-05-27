#pragma once
#include <unordered_map>
#include <CoreType.h>
#include <shared_mutex>

class PC;

class PCManager
{
private:
	PCManager() = default;
	~PCManager() = default;
	PCManager& operator=(const PCManager&) = delete;
	PCManager(PCManager&&) = delete;

public:
	static PCManager& GetInst();

public:
	std::shared_ptr<PC> FindPC(SessionIdType sessionId);
	bool InsertPC(std::shared_ptr<PC> session);
	bool DeletePC(SessionIdType sessionId);
	void ClearPCMap();

private:
	std::unordered_map<SessionIdType, std::shared_ptr<PC>> pcMap;
	std::shared_mutex pcMapLock;
};