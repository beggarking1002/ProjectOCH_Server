#include "pch.h"
#include "BattleMovementService.h"

#include <cmath>

int32 BattleMovementService::GetMoveRange(const BattlePawn& pawn) const
{
	// Movement range is discrete. Fractions produced by a buff are rounded
	// down, so a base range of 3 with a 1.5x modifier becomes 4 tiles.
	return max(0, static_cast<int32>(floor(static_cast<double>(pawn.moveRange) *
		_skillResolver.GetStatModifierMultiplier(pawn, "MOVE_RANGE"))));
}
