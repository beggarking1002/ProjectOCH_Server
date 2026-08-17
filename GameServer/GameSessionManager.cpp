#include "pch.h"
#include "GameSessionManager.h"
#include "GameSession.h"
#include "Player.h"
#include "EconomyService.h"
#include "DatabaseManager.h"
#include "QuestService.h"

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
	for (const GameSessionRef& session : SnapshotSessions())
	{
		PlayerRef player = session != nullptr ? session->player.load() : nullptr;
		if (player == nullptr)
			continue;
		lock_guard economyLock(player->EconomyMutex());

		vector<string> autoConsumedItemIds;
		vector<string> expiredItemIds;
		if (player->AdvanceEconomy(nowMs, autoConsumedItemIds, expiredItemIds))
		{
			GQuestService.ReevaluateInventoryObjectives(player);
			GEconomyService.SendExpeditionState(player, autoConsumedItemIds, expiredItemIds);
		}
		GDatabase.SavePlayerEconomyIfDue(player, nowMs);
	}
}

void GameSessionManager::SaveAllEconomies(uint64 nowMs)
{
	for (const GameSessionRef& session : SnapshotSessions())
	{
		PlayerRef player = session != nullptr ? session->player.load() : nullptr;
		if (player != nullptr)
		{
			lock_guard economyLock(player->EconomyMutex());
			GDatabase.SavePlayerEconomyOnDisconnect(player, nowMs);
		}
	}
}

vector<GameSessionRef> GameSessionManager::SnapshotSessions()
{
	WRITE_LOCK;
	return vector<GameSessionRef>(_sessions.begin(), _sessions.end());
}
