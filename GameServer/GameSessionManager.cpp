#include "pch.h"
#include "GameSessionManager.h"
#include "GameSession.h"
#include "Player.h"
#include "EconomyService.h"

GameSessionManager GSessionManager;

void GameSessionManager::Add(GameSessionRef session)
{
	WRITE_LOCK;
	_sessions.insert(session);
}

void GameSessionManager::Remove(GameSessionRef session)
{
	WRITE_LOCK;
	_sessions.erase(session);
}

void GameSessionManager::Broadcast(SendBufferRef sendBuffer)
{
	WRITE_LOCK;
	for (GameSessionRef session : _sessions)
	{
		session->Send(sendBuffer);
	}
}

void GameSessionManager::UpdateEconomy(uint64 nowMs)
{
	WRITE_LOCK;
	for (const GameSessionRef& session : _sessions)
	{
		PlayerRef player = session != nullptr ? session->player.load() : nullptr;
		if (player == nullptr)
			continue;

		vector<string> autoConsumedItemIds;
		vector<string> expiredItemIds;
		if (player->AdvanceEconomy(nowMs, autoConsumedItemIds, expiredItemIds))
			GEconomyService.SendExpeditionState(player, autoConsumedItemIds, expiredItemIds);
	}
}
