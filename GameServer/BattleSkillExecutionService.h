#pragma once

#include "BattleSkillResolver.h"

struct BattleSkillActionRequest
{
	const BattleSkillTemplate* skill = nullptr;
	const BattlePawnClassTemplate* casterTemplate = nullptr;
	BattlePawn* caster = nullptr;
	BattlePawn* target = nullptr;
	int32 skillSlot = 0;
	bool isUltimate = false;
	bool isBackAttack = false;
	const Protocol::AxialCoord* targetAxial = nullptr;
	const Protocol::AxialCoord* areaDirectionAxial = nullptr;
	function<BattlePawn*(const Protocol::AxialCoord&)> findAlivePawnAt;
	function<BattlePawn*(const BattlePawn&, uint64)> findAdjacentAliveAlly;
	function<Protocol::BattleTileType(const Protocol::AxialCoord&)> getBaseTileType;
	function<Protocol::BattleTileOverlayType(const Protocol::AxialCoord&)> getTileOverlayType;
	function<void(const Protocol::AxialCoord&, Protocol::BattleTileOverlayType)> setTileOverlayType;
	function<bool(const Protocol::AxialCoord&)> isTileValid;
	uint64* barrierIdGenerator = nullptr;
};

struct BattleSkillActionResult
{
	int32 appliedDamage = 0;
	vector<Protocol::BattleActionLog> logs;
	vector<Protocol::BattleTileInfo> tileDeltas;
	vector<const BattlePawn*> extraChangedPawns;
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
