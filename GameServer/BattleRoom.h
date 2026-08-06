#pragma once
#include "JobQueue.h"
#include "Pawn.h"
#include "BattlePawn.h"
#include "BattleSkillResolver.h"
#include "BattleSkillExecutionService.h"
#include "BattleMovementService.h"
#include "BattleSpatialService.h"
#include "BattleDisplacementService.h"
#include "BattleTurnService.h"
#include "BattleZocService.h"

using BattleRoomRef = shared_ptr<class BattleRoom>;

class BattleRoom : public JobQueue
{
private:
	struct SkillSpec
	{
		string skillKey;
		string targetType;
		const BattleSkillTemplate* skillTemplate = nullptr;
		const BattlePawnClassTemplate* casterTemplate = nullptr;
		int32 damage = 0;
		int32 rangeMin = 0;
		int32 rangeMax = 0;
		bool isUltimate = false;
	};

	struct BattleTileState
	{
		Protocol::BattleTileType baseTileType = Protocol::BATTLE_TILE_TYPE_NORMAL;
		Protocol::BattleTileOverlayType overlayType = Protocol::BATTLE_TILE_OVERLAY_TYPE_NONE;
		string equipmentKey;
		uint64 equipmentOwnerPawnId = 0;
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
		unordered_map<uint64, BattleTileState> tileStates;
		vector<BattlePawnRef> alliedPawns;
		vector<BattlePawnRef> enemyPawns;
		vector<uint64> turnQueue;
		size_t turnQueueIndex = 0;
		uint64 currentTurnPawnId = 0;
		bool isFinished = false;
		uint64 stateVersion = 1;
		uint64 winnerOwnerId = 0;
		uint64 loserOwnerId = 0;
		bool ownerResultAcked = false;
		bool opponentResultAcked = false;
		vector<uint64> turnStartChangedPawnIds;
		vector<Protocol::BattleActionLog> turnStartLogs;
		vector<pair<uint64, uint64>> turnStartDeaths;
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
	BattleState& GetOrCreateBattle(PlayerRef ownerPlayer);
	BattleState CreateBattle(PlayerRef ownerPlayer);
	BattleState CreatePvpBattle(PlayerRef ownerPlayer, PlayerRef opponentPlayer);
	BattlePawnRef MakeBattlePawn(uint64 ownerId, Protocol::PawnClass pawnClass, int32 cellX, int32 cellY, int32 hp, int32 moveRange,
		int32 maxArmor, Protocol::BattlePawnRole role);
	BattlePawnRef MakeBattlePawnFromOwnedPawn(PawnRef sourcePawn, int32 cellX, int32 cellY);
	void AddOwnedBattlePawns(vector<BattlePawnRef>& dst, PlayerRef ownerPlayer, int32 cellX, int32 firstCellY);
	bool TryGetPawnTemplate(Protocol::PawnClass pawnClass, BattlePawnInitialStats& pawnTemplate);
	Protocol::AxialCoord MakeAxial(int32 q, int32 r);
	uint64 MakeTileKey(const Protocol::AxialCoord& axial) const;
	void InitializeBattleTiles(BattleState& battle);
	Protocol::BattleTileType GetBaseTileType(const BattleState& battle, const Protocol::AxialCoord& axial) const;
	Protocol::BattleTileOverlayType GetTileOverlayType(const BattleState& battle, const Protocol::AxialCoord& axial) const;
	void SetTileOverlayType(BattleState& battle, const Protocol::AxialCoord& axial, Protocol::BattleTileOverlayType overlayType);
	string GetTileEquipmentKey(const BattleState& battle, const Protocol::AxialCoord& axial) const;
	uint64 GetTileEquipmentOwnerPawnId(const BattleState& battle, const Protocol::AxialCoord& axial) const;
	void SetTileEquipment(BattleState& battle, const Protocol::AxialCoord& axial, const string& equipmentKey, uint64 ownerPawnId);
	void AppendBattleTileStates(const BattleState& battle, google::protobuf::RepeatedPtrField<Protocol::BattleTileInfo>* dst) const;
	vector<uint64> BuildUpcomingTurnPawnIds(const BattleState& battle, size_t count = 8) const;
	void AppendUpcomingTurnPawnIds(const BattleState& battle, google::protobuf::RepeatedField<uint64>* dst) const;

	void FillEnterBattlePacket(const BattleState& battle, uint64 viewerOwnerId, Protocol::S_ENTER_BATTLE& pkt);
	void CopyBattlePawn(const BattlePawn& src, Protocol::BattlePawnInfo* dst);
	void CopyBattlePawnDelta(const BattlePawn& src, Protocol::BattlePawnDelta* dst);

	BattlePawn* FindPawn(BattleState& battle, uint64 pawnId);
	BattlePawn* FindAlivePawnAt(BattleState& battle, const Protocol::AxialCoord& axial);
	BattlePawn* FindAdjacentAliveAlly(BattleState& battle, const BattlePawn& source, uint64 excludedPawnId);
	vector<BattlePawn*> FindAlliedPawns(BattleState& battle, const BattlePawn& source);
	BattlePawn* FindSingleTargetInterceptor(BattleState& battle, const BattlePawn& protectedPawn);
	bool IsTauntTargetRequired(BattleState& battle, const BattlePawn& attacker, const BattlePawn& selectedTarget);
	bool IsOccupied(const BattleState& battle, const Protocol::AxialCoord& coord, uint64 exceptPawnId);
	bool IsBattleWalkable(const BattleState& battle, const Protocol::AxialCoord& coord) const;
	uint64 GetNextAlliedTurnPawnId(const BattleState& battle, uint64 currentPawnId);
	void BuildTurnQueue(BattleState& battle);
	uint64 AdvanceTurn(BattleState& battle);
	bool TryGetSkillSpec(const BattlePawn& pawn, int32 skillSlot, SkillSpec& spec, string& reason);
	bool CanMove(const BattlePawn& pawn);
	bool CanRecoverArmor(const BattlePawn& pawn);
	bool IsAlive(const BattlePawn& pawn);
	bool HasAlivePawn(const vector<BattlePawnRef>& pawns);
	bool TryFinishBattle(BattleState& battle, uint64 fallbackWinnerOwnerId);
	void StartTurn(BattleState& battle, BattlePawn& pawn);
	void ExecutePassiveTrigger(BattlePawn& pawn, const string& trigger, BattlePawn* effectTarget = nullptr);
	void ExecuteAuraTurnStartEffects(BattleState& battle, BattlePawn& pawn);
	void ExecuteBattleStartEffects(BattleState& battle);
	bool AdvanceOwnerTurnEffects(BattlePawn& pawn);
	void ApplyDamage(BattlePawn& target, int32 damage);
	void ApplyFireTileLandingDamage(BattleState& battle, BattlePawn& pawn,
		vector<Protocol::BattleActionLog>& logs, vector<const BattlePawn*>& changedPawns,
		vector<BattlePawn*>& deathCandidates);
	bool RollEvade(const BattlePawn& attacker, BattlePawn& defender, const BattleSkillTemplate& skill);
	bool RollCritical(const BattlePawn& attacker, const BattleSkillTemplate& skill);
	BattlePushResult ResolvePush(BattleState& battle, BattlePawn& attacker, BattlePawn& target);
	bool TryExecuteZocAttack(BattleState& battle, BattlePawn& zocOwner, BattlePawn& movingPawn,
		vector<Protocol::BattleActionLog>& logs, vector<Protocol::BattleTileInfo>& tileDeltas,
		vector<const BattlePawn*>& extraChangedPawns, vector<BattlePawn*>& deathCandidates);
	void TryExecuteAllyAttackZocReactions(BattleState& battle, BattlePawn& attacker,
		const vector<BattlePawn*>& attackedPawns, vector<Protocol::BattleActionLog>& logs,
		vector<Protocol::BattleTileInfo>& tileDeltas, vector<const BattlePawn*>& extraChangedPawns,
		vector<BattlePawn*>& deathCandidates);
	bool TryExecuteCounterattack(BattleState& battle, BattlePawn& defender, BattlePawn& attacker,
		vector<Protocol::BattleActionLog>& logs, vector<Protocol::BattleTileInfo>& tileDeltas,
		vector<const BattlePawn*>& extraChangedPawns, vector<BattlePawn*>& deathCandidates, int32 counterChainDepth = 0);
	void AddActionLog(google::protobuf::RepeatedPtrField<Protocol::BattleActionLog>* logs,
		uint64 attackerPawnId, uint64 defenderPawnId, int32 skillSlot, const string& actionType,
		int32 damage, const BattlePawn& defender, bool isCounter = false);

	void SendEnterBattle(GameSessionRef session, Protocol::S_ENTER_BATTLE& pkt);
	void SendBattleMoveResult(GameSessionRef session, bool success, uint64 battleId, uint64 pawnId,
	const Protocol::AxialCoord& start, const Protocol::AxialCoord& target, uint64 nextTurnPawnId,
		Protocol::BattleMoveResult result, const string& reason, const BattlePawn* pawn = nullptr,
		const vector<Protocol::BattleActionLog>& logs = {}, const vector<const BattlePawn*>& extraPawns = {}, bool turnQueueResynced = false,
		const vector<Protocol::AxialCoord>& path = {});
	void SendBattleSkillResult(GameSessionRef session, bool success, uint64 battleId, uint64 casterPawnId,
		int32 skillSlot, uint64 targetPawnId, const Protocol::AxialCoord& targetAxial,
	int32 damage, int32 targetHp, int32 targetArmor, uint64 nextTurnPawnId, const string& reason,
		const BattlePawn* caster = nullptr, const BattlePawn* target = nullptr,
		const vector<Protocol::BattleActionLog>& logs = {}, const vector<Protocol::BattleTileInfo>& tileDeltas = {},
		const vector<const BattlePawn*>& extraPawns = {}, bool turnQueueResynced = false);
	void SendBattleEndTurnResult(GameSessionRef session, bool success, uint64 battleId, uint64 pawnId,
		uint64 nextTurnPawnId, const string& reason, const BattlePawn* pawn = nullptr, const BattlePawn* nextPawn = nullptr,
		const vector<Protocol::BattleTileInfo>& tileDeltas = {}, const vector<const BattlePawn*>& extraPawns = {},
		const vector<Protocol::BattleActionLog>& logs = {}, bool turnQueueResynced = false);
	void SendBattlePawnDead(GameSessionRef session, uint64 battleId, uint64 pawnId, uint64 killerPawnId);
	void SendBattleResult(GameSessionRef session, const BattleState& battle, uint64 viewerOwnerId);
	void SendBattleResultAck(GameSessionRef session, bool success, uint64 battleId, const string& reason);

private:
	uint64 _battleIdGenerator = 1;
	uint64 _battlePawnIdGenerator = 1;
	uint64 _barrierIdGenerator = 1;
	BattleSpatialService _spatialService;
	BattleDisplacementService _displacementService{ _spatialService };
	BattleTurnService _turnService;
	BattleSkillResolver _skillResolver;
	BattleMovementService _movementService{ _skillResolver, _spatialService };
	BattleSkillExecutionService _skillExecutionService{ _skillResolver };
	BattleZocService _zocService{ _spatialService };
	unordered_map<uint64, BattleState> _battles;
	unordered_map<uint64, uint64> _battleByOwnerId;
};

extern BattleRoomRef GBattleRoom;
