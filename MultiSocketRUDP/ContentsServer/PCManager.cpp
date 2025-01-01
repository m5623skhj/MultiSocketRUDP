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
	
	auto itor = pcMap.find(sessionId);
	if (itor == pcMap.end())
	{
		return nullptr;
	}

	return itor->second;
}
bool PCManager::InsertPC(std::shared_ptr<PC> session)
{
	std::unique_lock lock(pcMapLock);

	return pcMap.insert({ session->GetSessionId(), session }).second;
}

bool PCManager::DeletePC(SessionIdType sessionid)
{
	std::unique_lock lock(pcMapLock);

	return pcMap.erase(sessionid) == 0;
}

void PCManager::ClearPCMap()
{
	std::unique_lock lock(pcMapLock);
	pcMap.clear();
}