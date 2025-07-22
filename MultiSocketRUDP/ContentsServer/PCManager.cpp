#include "PreCompile.h"
#include "PCManager.h"
#include "PC.h"

PCManager& PCManager::GetInst()
{
	static PCManager instance;
	return instance;
}

std::shared_ptr<PC> PCManager::FindPC(SessionIdType sessionId)
{
	std::shared_lock lock(pcMapLock);

	const auto itor = pcMap.find(sessionId);
	if (itor == pcMap.end())
	{
		return nullptr;
	}

	if (itor->second == nullptr)
	{
		pcMap.erase(itor);
		return nullptr;
	}

	if (not itor->second->IsConnected())
	{
		return nullptr;
	}

	return itor->second;
}

bool PCManager::InsertPC(const std::shared_ptr<PC>& session)
{
	std::unique_lock lock(pcMapLock);

	return pcMap.insert({ session->GetSessionId(), session }).second;
}

bool PCManager::DeletePC(const SessionIdType sessionId)
{
	std::unique_lock lock(pcMapLock);

	return pcMap.erase(sessionId) == 0;
}

void PCManager::ClearPCMap()
{
	std::unique_lock lock(pcMapLock);
	pcMap.clear();
}