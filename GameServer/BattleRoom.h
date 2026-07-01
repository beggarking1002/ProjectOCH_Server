#pragma once
#include "JobQueue.h"

using BattleRoomRef = shared_ptr<class BattleRoom>;

class BattleRoom : public JobQueue
{
private:
	struct BattlePawnState
	{
		uint64 pawnId = 0;
		uint64 ownerId = 0;
		Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
		Protocol::AxialCoord axial;
		int32 hp = 0;
		int32 maxHp = 0;
		int32 moveRange = 0;
	};

	struct BattleState
	{
		uint64 battleId = 0;
		uint64 ownerId = 0;
		string mapId;
		vector<BattlePawnState> alliedPawns;
		vector<BattlePawnState> enemyPawns;
		uint64 currentTurnPawnId = 0;
	};

public:
	BattleRoom();
	virtual ~BattleRoom();

public:
	void HandleEnterBattle(GameSessionRef session);
	void HandleLeaveBattle(uint64 ownerId, string reason);
	void HandleBattleMove(GameSessionRef session, Protocol::C_BATTLE_MOVE pkt);
	void HandleBattleSkill(GameSessionRef session, Protocol::C_BATTLE_SKILL pkt);

private:
	BattleState& GetOrCreateBattle(uint64 ownerId);
	BattleState CreateBattle(uint64 ownerId);
	BattlePawnState MakeBattlePawn(uint64 ownerId, Protocol::PawnClass pawnClass, int32 q, int32 r, int32 hp, int32 moveRange);
	Protocol::AxialCoord MakeAxial(int32 q, int32 r);

	void FillEnterBattlePacket(const BattleState& battle, Protocol::S_ENTER_BATTLE& pkt);
	void CopyBattlePawn(const BattlePawnState& src, Protocol::BattlePawnInfo* dst);

	BattlePawnState* FindPawn(BattleState& battle, uint64 pawnId);
	bool IsOccupied(const BattleState& battle, const Protocol::AxialCoord& coord, uint64 exceptPawnId);
	bool IsBattleWalkable(const Protocol::AxialCoord& coord);
	int32 AxialDistance(const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs);
	uint64 GetNextAlliedTurnPawnId(const BattleState& battle, uint64 currentPawnId);
	bool TryGetSkillSpec(int32 skillSlot, int32& damage, int32& range, string& reason);

	void SendEnterBattle(GameSessionRef session, Protocol::S_ENTER_BATTLE& pkt);
	void SendBattleMoveResult(GameSessionRef session, bool success, uint64 battleId, uint64 pawnId,
		const Protocol::AxialCoord& start, const Protocol::AxialCoord& target, uint64 nextTurnPawnId,
		Protocol::BattleMoveResult result, const string& reason);
	void SendBattleSkillResult(GameSessionRef session, bool success, uint64 battleId, uint64 casterPawnId,
		int32 skillSlot, uint64 targetPawnId, const Protocol::AxialCoord& targetAxial,
		int32 damage, int32 targetHp, uint64 nextTurnPawnId, const string& reason);

private:
	uint64 _battleIdGenerator = 1;
	uint64 _battlePawnIdGenerator = 1;
	unordered_map<uint64, BattleState> _battles;
	unordered_map<uint64, uint64> _battleByOwnerId;
};

extern BattleRoomRef GBattleRoom;
