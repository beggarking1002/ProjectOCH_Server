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
	void HandleEnterVillage(GameSessionRef session, Protocol::C_ENTER_VILLAGE pkt);
	void HandleVillageShopOpen(GameSessionRef session, Protocol::C_VILLAGE_SHOP_OPEN pkt);
	void HandleVillageShopBuy(GameSessionRef session, Protocol::C_VILLAGE_SHOP_BUY pkt);
	void HandleVillageShopSell(GameSessionRef session, Protocol::C_VILLAGE_SHOP_SELL pkt);
	void HandleBattleInvite(GameSessionRef session, Protocol::C_BATTLE_INVITE pkt);
	void HandleBattleInviteResponse(GameSessionRef session, Protocol::C_BATTLE_INVITE_RESPONSE pkt);
	void HandleDebugBattleSelectionStart(GameSessionRef session);
	void HandleBattleClassSelection(GameSessionRef session, Protocol::C_BATTLE_CLASS_SELECTION pkt);

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
	void SendBattleClassSelectionStart(GameSessionRef session, uint64 requesterId, uint64 targetId);
	void SendBattleClassSelectionResult(GameSessionRef session, bool success, bool waitingForOpponent,
		uint64 requesterId, uint64 targetId, const string& reason);
	void SendBattleResultAck(GameSessionRef session, bool success, uint64 battleId, const string& reason);
	void RemovePlayersFromFieldForBattle(const vector<PlayerRef>& players);
	void CancelBattleInvitesForPlayer(uint64 playerId, const string& reason);
	void CancelBattleClassSelectionForPlayer(uint64 playerId, const string& reason);
	void CancelDebugBattleClassSelection(uint64 playerId);
	bool TryBuildBattleClassSelection(const Protocol::C_BATTLE_CLASS_SELECTION& pkt,
		vector<Protocol::PawnClass>& selectedClasses, string& reason) const;

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

	struct PendingBattleClassSelection
	{
		uint64 requesterId = 0;
		uint64 targetId = 0;
		weak_ptr<GameSession> requesterSession;
		weak_ptr<GameSession> targetSession;
		bool requesterSelected = false;
		bool targetSelected = false;
		vector<Protocol::PawnClass> requesterClasses;
		vector<Protocol::PawnClass> targetClasses;
	};

	unordered_map<uint64, ObjectRef> _objects;
	unordered_map<uint64, PendingBattleInvite> _battleInvitesByTargetId;
	unordered_map<uint64, uint64> _battleInviteTargetByRequesterId;
	unordered_map<uint64, PendingBattleClassSelection> _battleClassSelectionsByRequesterId;
	unordered_map<uint64, uint64> _battleClassSelectionRequesterByPlayerId;
	unordered_map<uint64, weak_ptr<GameSession>> _debugBattleClassSelectionsByPlayerId;
};

extern RoomRef GRoom;
