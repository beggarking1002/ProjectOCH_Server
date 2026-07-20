#pragma once

#include "BattleEffectExecutor.h"

class BattlePawn
{
public:
	virtual ~BattlePawn() = default;

	virtual const char* GetBehaviorKey() const { return "DEFAULT"; }
	virtual vector<Protocol::AxialCoord> ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target,
		const Protocol::AxialCoord* directionTarget = nullptr) const;

	int32 GetShieldCurrent() const
	{
		int32 value = armor;
		for (const BattleBarrierState& barrier : barriers)
			value += barrier.value;
		return value;
	}

	int32 GetShieldMax() const
	{
		int32 value = maxArmor;
		for (const BattleBarrierState& barrier : barriers)
			value += barrier.maxValue;
		return value;
	}

public:
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
	bool isDead = false;
	Protocol::BattleFacingDirection facingDirection = Protocol::BATTLE_FACING_DIRECTION_RIGHT;
	Protocol::BattlePawnRole role = Protocol::BATTLE_PAWN_ROLE_NONE;
	unordered_map<Protocol::BattleResourceType, int32> resources;
	unordered_map<Protocol::BattleResourceType, int32> maxResources;
	vector<BattleBarrierState> barriers;
	unordered_map<string, BattleStatusState> statuses;
	unordered_map<string, BattleAuraState> auras;
};

using BattlePawnRef = shared_ptr<BattlePawn>;

BattlePawnRef CreateBattlePawn(Protocol::PawnClass pawnClass);

struct BattlePawnInitialStats
{
	int32 hp = 0;
	int32 moveRange = 0;
	int32 maxArmor = 0;
	Protocol::BattlePawnRole role = Protocol::BATTLE_PAWN_ROLE_NONE;
};
