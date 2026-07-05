#pragma once
#include "JobQueue.h"

class Room : public JobQueue
{
public:
	Room();
	virtual ~Room();

public:
	bool EnterRoom(ObjectRef object, bool randPos = true);
	bool LeaveRoom(ObjectRef object);

	bool HandleEnterPlayer(PlayerRef player);
	bool HandleEnterPlayerFromBattle(PlayerRef player, uint64 battleId);
	bool HandleLeavePlayer(GameSessionRef session);
	void HandleMove(GameSessionRef session, Protocol::C_MOVE pkt);
	void HandleBattleInvite(GameSessionRef session, Protocol::C_BATTLE_INVITE pkt);
	void HandleBattleInviteResponse(GameSessionRef session, Protocol::C_BATTLE_INVITE_RESPONSE pkt);

public:
	void UpdateTick();

	RoomRef GetRoomRef();

private:
	bool AddObject(ObjectRef object);
	bool RemoveObject(uint64 objectId);
	PlayerRef GetPlayerInRoom(GameSessionRef session);
	void SendEnterGame(PlayerRef player, bool success);
	void SendSpawn(ObjectRef object, uint64 exceptId = 0);
	void SendExistingPlayers(PlayerRef player);
	void SendBattleInviteRequest(GameSessionRef session, bool success, uint64 requesterId, uint64 targetId, const string& reason);
	void SendBattleInviteReceived(GameSessionRef session, uint64 requesterId);
	void SendBattleInviteResult(GameSessionRef session, bool accepted, uint64 requesterId, uint64 targetId, const string& reason);
	void SendBattleResultAck(GameSessionRef session, bool success, uint64 battleId, const string& reason);
	void RemovePlayersFromFieldForBattle(const vector<PlayerRef>& players);
	void CancelBattleInvitesForPlayer(uint64 playerId, const string& reason);

private:
	void Broadcast(SendBufferRef sendBuffer, uint64 exceptId = 0);

private:
	struct PendingBattleInvite
	{
		uint64 requesterId = 0;
		uint64 targetId = 0;
		weak_ptr<GameSession> requesterSession;
		weak_ptr<GameSession> targetSession;
	};

	unordered_map<uint64, ObjectRef> _objects;
	unordered_map<uint64, PendingBattleInvite> _battleInvitesByTargetId;
	unordered_map<uint64, uint64> _battleInviteTargetByRequesterId;
};

extern RoomRef GRoom;
