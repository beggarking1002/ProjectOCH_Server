#pragma once

#include "BattleSkillResolver.h"
#include "BattleDisplacementService.h"

struct BattleSkillActionRequest
{
	const BattleSkillTemplate* skill = nullptr;
	const BattlePawnClassTemplate* casterTemplate = nullptr;
	BattlePawn* caster = nullptr;
	BattlePawn* target = nullptr;
	int32 skillSlot = 0;
	bool isUltimate = false;
	bool isBackAttack = false;
	bool isGuarded = false;
	bool isCounter = false;
	string actionType;
	const Protocol::AxialCoord* targetAxial = nullptr;
	function<bool(const BattlePawn&, BattlePawn&, const BattleSkillTemplate&)> shouldEvadeTarget;
	function<bool(const BattlePawn&, const BattleSkillTemplate&)> shouldCriticalTarget;
	const Protocol::AxialCoord* areaDirectionAxial = nullptr;
	function<BattlePawn*(const Protocol::AxialCoord&)> findAlivePawnAt;
	function<BattlePawn*(const BattlePawn&, uint64)> findAdjacentAliveAlly;
	function<vector<BattlePawn*>(const BattlePawn&)> findAlliedPawns;
	function<Protocol::BattleTileType(const Protocol::AxialCoord&)> getBaseTileType;
	function<Protocol::BattleTileOverlayType(const Protocol::AxialCoord&)> getTileOverlayType;
	function<void(const Protocol::AxialCoord&, Protocol::BattleTileOverlayType)> setTileOverlayType;
	function<string(const Protocol::AxialCoord&)> getTileEquipmentKey;
	function<uint64(const Protocol::AxialCoord&)> getTileEquipmentOwnerPawnId;
	function<void(const Protocol::AxialCoord&, const string&, uint64)> setTileEquipment;
	function<bool(const Protocol::AxialCoord&)> isTileValid;
	function<BattlePushResult(BattlePawn&, BattlePawn&)> tryPushTarget;
	function<BattleRetreatResult(BattlePawn&, BattlePawn&)> tryRetreatCaster;
	function<BattleDashResult(BattlePawn&, BattlePawn*, const Protocol::AxialCoord&, int32)> tryDashCaster;
	uint64* barrierIdGenerator = nullptr;
};

struct BattleSkillActionResult
{
	int32 appliedDamage = 0;
	vector<Protocol::BattleActionLog> logs;
	vector<Protocol::BattleTileInfo> tileDeltas;
	vector<const BattlePawn*> extraChangedPawns;
	vector<BattlePawn*> affectedTargets;
	vector<BattlePawn*> deathCandidates;
};

class BattleSkillExecutionService
{
public:
	explicit BattleSkillExecutionService(const BattleSkillResolver& skillResolver) : _skillResolver(skillResolver) { }

	BattleSkillActionResult Execute(const BattleSkillActionRequest& request) const;

private:
	BattleEffectPawnContext MakeEffectContext(BattlePawn& pawn) const;
	void AppendEffectResult(BattleEffectExecutionResult& destination, const BattleEffectExecutionResult& additional) const;

private:
	const BattleSkillResolver& _skillResolver;
};
