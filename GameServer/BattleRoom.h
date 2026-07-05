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
		int32 armor = 0;
		int32 maxArmor = 0;
		int32 currentAp = 0;
		bool hasMovedThisTurn = false;
		bool usedSubActionThisTurn = false;
		bool usedUltimate = false;
		bool isShieldUnit = false;
		bool isMelee = true;
		bool isDead = false;
	};

	struct SkillSpec
	{
		int32 apCost = 0;
		int32 damage = 0;
		int32 range = 0;
		bool isUltimate = false;
	};

	struct BattleState
	{
		uint64 battleId = 0;
		uint64 ownerId = 0;
		uint64 opponentOwnerId = 0;
		bool isPvp = false;
		weak_ptr<GameSession> ownerSession;
		weak_ptr<GameSession> opponentSession;
		string mapId;
		vector<BattlePawnState> alliedPawns;
		vector<BattlePawnState> enemyPawns;
		vector<uint64> turnQueue;
		size_t turnQueueIndex = 0;
		uint64 currentTurnPawnId = 0;
		bool isFinished = false;
		uint64 winnerOwnerId = 0;
		uint64 loserOwnerId = 0;
		bool ownerResultAcked = false;
		bool opponentResultAcked = false;
	};

public:
	BattleRoom();
	virtual ~BattleRoom();

public:
	void HandleEnterBattle(GameSessionRef session);
	void HandleLeaveBattle(uint64 ownerId, string reason);
	void HandleBattleMove(GameSessionRef session, Protocol::C_BATTLE_MOVE pkt);
	void HandleBattleSkill(GameSessionRef session, Protocol::C_BATTLE_SKILL pkt);
	void HandleBattleEndTurn(GameSessionRef session, Protocol::C_BATTLE_END_TURN pkt);
	void HandleEnterPvpBattle(GameSessionRef requesterSession, GameSessionRef targetSession);
	void HandleBattleResultAck(GameSessionRef session, Protocol::C_BATTLE_RESULT_ACK pkt);

private:
	BattleState& GetOrCreateBattle(uint64 ownerId);
	BattleState CreateBattle(uint64 ownerId);
	BattleState CreatePvpBattle(uint64 ownerId, uint64 opponentOwnerId);
	BattlePawnState MakeBattlePawn(uint64 ownerId, Protocol::PawnClass pawnClass, int32 q, int32 r, int32 hp, int32 moveRange,
		int32 maxArmor, bool isShieldUnit, bool isMelee);
	Protocol::AxialCoord MakeAxial(int32 q, int32 r);

	void FillEnterBattlePacket(const BattleState& battle, uint64 viewerOwnerId, Protocol::S_ENTER_BATTLE& pkt);
	void CopyBattlePawn(const BattlePawnState& src, Protocol::BattlePawnInfo* dst);
	void CopyBattlePawnDelta(const BattlePawnState& src, Protocol::BattlePawnDelta* dst);

	BattlePawnState* FindPawn(BattleState& battle, uint64 pawnId);
	bool IsOccupied(const BattleState& battle, const Protocol::AxialCoord& coord, uint64 exceptPawnId);
	bool IsBattleWalkable(const Protocol::AxialCoord& coord);
	int32 AxialDistance(const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs);
	uint64 GetNextAlliedTurnPawnId(const BattleState& battle, uint64 currentPawnId);
	void BuildTurnQueue(BattleState& battle);
	uint64 AdvanceTurn(BattleState& battle);
	bool TryGetSkillSpec(int32 skillSlot, SkillSpec& spec, string& reason);
	bool CanMove(const BattlePawnState& pawn);
	bool IsAlive(const BattlePawnState& pawn);
	bool HasAlivePawn(const vector<BattlePawnState>& pawns);
	bool TryFinishBattle(BattleState& battle, uint64 fallbackWinnerOwnerId);
	void StartTurn(BattlePawnState& pawn);
	void ApplyDamage(BattlePawnState& target, int32 damage);
	void AddActionLog(google::protobuf::RepeatedPtrField<Protocol::BattleActionLog>* logs,
		uint64 attackerPawnId, uint64 defenderPawnId, int32 skillSlot, const string& actionType,
		int32 damage, const BattlePawnState& defender, bool isCounter = false);

	void SendEnterBattle(GameSessionRef session, Protocol::S_ENTER_BATTLE& pkt);
	void SendBattleMoveResult(GameSessionRef session, bool success, uint64 battleId, uint64 pawnId,
		const Protocol::AxialCoord& start, const Protocol::AxialCoord& target, uint64 nextTurnPawnId,
		Protocol::BattleMoveResult result, const string& reason, const BattlePawnState* pawn = nullptr);
	void SendBattleSkillResult(GameSessionRef session, bool success, uint64 battleId, uint64 casterPawnId,
		int32 skillSlot, uint64 targetPawnId, const Protocol::AxialCoord& targetAxial,
		int32 damage, int32 targetHp, int32 targetArmor, uint64 nextTurnPawnId, const string& reason,
		const BattlePawnState* caster = nullptr, const BattlePawnState* target = nullptr,
		const vector<Protocol::BattleActionLog>& logs = {});
	void SendBattleEndTurnResult(GameSessionRef session, bool success, uint64 battleId, uint64 pawnId,
		uint64 nextTurnPawnId, const string& reason, const BattlePawnState* pawn = nullptr, const BattlePawnState* nextPawn = nullptr);
	void SendBattlePawnDead(GameSessionRef session, uint64 battleId, uint64 pawnId, uint64 killerPawnId);
	void SendBattleResult(GameSessionRef session, const BattleState& battle, uint64 viewerOwnerId);
	void SendBattleResultAck(GameSessionRef session, bool success, uint64 battleId, const string& reason);

private:
	uint64 _battleIdGenerator = 1;
	uint64 _battlePawnIdGenerator = 1;
	unordered_map<uint64, BattleState> _battles;
	unordered_map<uint64, uint64> _battleByOwnerId;
};

extern BattleRoomRef GBattleRoom;
