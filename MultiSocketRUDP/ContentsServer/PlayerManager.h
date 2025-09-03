#pragma once
#include <unordered_map>
#include <shared_mutex>
#include "Player.h"

class PlayerManager
{
public:
	static PlayerManager& GetInst();

private:
	PlayerManager() = default;
	~PlayerManager() = default;

public:
	void AddPlayer(PlayerIdType playerId, Player* player);
	void ErasePlayer(PlayerIdType playerId);
	void ErasePlayerBySessionId(SessionIdType sessionId);
	Player* FindPlayer(PlayerIdType playerId);
	Player* FindPlayerBySessionId(SessionIdType sessionId);

private:
	std::unordered_map<PlayerIdType, Player*> playerMap;
	std::shared_mutex playersMutex;

	std::unordered_map<SessionIdType, Player*> sessionIdToPlayerMap;
	std::shared_mutex sessionIdToPlayerMapMutex;
};