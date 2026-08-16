#include "pch.h"
#include "GameSessionManager.h"
#include "GameSession.h"
#include "Player.h"
#include "EconomyService.h"
#include "DatabaseManager.h"

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

bool GameSessionManager::TryBindAuthenticatedAccount(GameSessionRef session, uint64 accountId)
{
	if (session == nullptr || accountId == 0)
		return false;

	WRITE_LOCK;
	const uint64 currentAccountId = session->authenticatedAccountId.load();
	if (currentAccountId != 0 && currentAccountId != accountId)
		return false;

	for (const GameSessionRef& activeSession : _sessions)
	{
		if (activeSession != nullptr && activeSession != session &&
			activeSession->authenticatedAccountId.load() == accountId)
		{
			return false;
		}
	}

	session->authenticatedAccountId.store(accountId);
	return true;
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
		GDatabase.SavePlayerEconomyIfDue(player, nowMs);
	}
}
