#pragma once

#include "BattleSkillResolver.h"
#include "BattleSpatialService.h"

// Shared movement-rule evaluator. Character skills may add a MOVE_RANGE
// modifier through data, while this service applies the result consistently
// to validation and packets.
class BattleMovementService
{
public:
	explicit BattleMovementService(const BattleSkillResolver& skillResolver, const BattleSpatialService& spatialService)
		: _skillResolver(skillResolver), _spatialService(spatialService)
	{
	}

	int32 GetMoveRange(const BattlePawn& pawn) const;
	bool IsReachable(const Protocol::AxialCoord& start, const Protocol::AxialCoord& target, int32 maxSteps,
		const function<bool(const Protocol::AxialCoord&)>& canTraverse) const;

private:
	const BattleSkillResolver& _skillResolver;
	const BattleSpatialService& _spatialService;
};
