#pragma once

class GameSession;

using GameSessionRef = shared_ptr<GameSession>;

class GameSessionManager
{
public:
	void Add(GameSessionRef session);
	void Remove(GameSessionRef session);
	bool TryBindAuthenticatedAccount(GameSessionRef session, uint64 accountId);
	void Broadcast(SendBufferRef sendBuffer);
	void UpdateEconomy(uint64 nowMs);

private:
	USE_LOCK;
	set<GameSessionRef> _sessions;
};

extern GameSessionManager GSessionManager;
