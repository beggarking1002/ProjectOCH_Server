#pragma once

#include "BattleSkillResolver.h"

// Shared movement-rule evaluator. Character skills may add a MOVE_RANGE
// modifier through data, while this service applies the result consistently
// to validation and packets.
class BattleMovementService
{
public:
	explicit BattleMovementService(const BattleSkillResolver& skillResolver)
		: _skillResolver(skillResolver)
	{
	}

	int32 GetMoveRange(const BattlePawn& pawn) const;

private:
	const BattleSkillResolver& _skillResolver;
};
