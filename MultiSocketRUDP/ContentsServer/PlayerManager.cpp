#include "PreCompile.h"
#include "PlayerManager.h"

PlayerManager& PlayerManager::GetInst()
{
	static PlayerManager instance;
	return instance;
}

void PlayerManager::AddPlayer(const PlayerIdType playerId, Player* player)
{
	{
		std::unique_lock lock(playersMutex);
		playerMap.insert({ playerId, player });
	}
	{
		std::unique_lock lock(sessionIdToPlayerMapMutex);
		sessionIdToPlayerMap.insert({ player->GetSessionId(), player });
	}
}

void PlayerManager::ErasePlayer(const PlayerIdType playerId)
{
	SessionIdType sessionId;
	{
		const auto itor = playerMap.find(playerId);
		if (itor == playerMap.end())
		{
			return;
		}

		sessionId = itor->second->GetSessionId();
	}

	if (playerId != InvalidPlayerId)
	{
		std::unique_lock lock(playersMutex);
		playerMap.erase(playerId);
	}

	if (sessionId != INVALID_SESSION_ID)
	{
		std::unique_lock lock(sessionIdToPlayerMapMutex);
		sessionIdToPlayerMap.erase(sessionId);
	}
}

void PlayerManager::ErasePlayerBySessionId(const SessionIdType sessionId)
{
	PlayerIdType playerId;
	{
		const auto itor = sessionIdToPlayerMap.find(sessionId);
		if (itor == sessionIdToPlayerMap.end())
		{
			return;
		}
		playerId = itor->second->GetPlayerId();
	}

	if (playerId != InvalidPlayerId)
	{
		std::unique_lock lock(playersMutex);
		playerMap.erase(playerId);
	}

	if (sessionId != INVALID_SESSION_ID)
	{
		std::unique_lock lock(sessionIdToPlayerMapMutex);
		sessionIdToPlayerMap.erase(sessionId);
	}
}

Player* PlayerManager::FindPlayer(const PlayerIdType playerId)
{
	std::shared_lock lock(playersMutex);
	const auto itor = playerMap.find(playerId);
	if (itor == playerMap.end())
	{
		return nullptr;
	}

	return itor->second;
}

Player* PlayerManager::FindPlayerBySessionId(const SessionIdType sessionId)
{
	std::shared_lock lock(sessionIdToPlayerMapMutex);
	const auto itor = sessionIdToPlayerMap.find(sessionId);
	if (itor == sessionIdToPlayerMap.end())
	{
		return nullptr;
	}
	return itor->second;
}