#include "pch.h"
#include "BattleMovementService.h"

#include <cmath>
#include <queue>

int32 BattleMovementService::GetMoveRange(const BattlePawn& pawn) const
{
	// Movement range is discrete. Fractions produced by a buff are rounded
	// down, so a base range of 3 with a 1.5x modifier becomes 4 tiles.
	return max(0, static_cast<int32>(floor(static_cast<double>(pawn.moveRange) *
		_skillResolver.GetStatModifierMultiplier(pawn, "MOVE_RANGE"))));
}

bool BattleMovementService::IsReachable(const Protocol::AxialCoord& start, const Protocol::AxialCoord& target, int32 maxSteps,
	const function<bool(const Protocol::AxialCoord&)>& canTraverse) const
{
	if (maxSteps < 0 || canTraverse == nullptr)
		return false;
	if (start.q() == target.q() && start.r() == target.r())
		return true;

	auto makeKey = [](const Protocol::AxialCoord& coord)
		{
			return (static_cast<uint64>(static_cast<uint32>(coord.q())) << 32) |
				static_cast<uint32>(coord.r());
		};

	queue<pair<Protocol::AxialCoord, int32>> frontier;
	unordered_set<uint64> visited;
	frontier.emplace(start, 0);
	visited.insert(makeKey(start));

	while (frontier.empty() == false)
	{
		auto [current, steps] = frontier.front();
		frontier.pop();
		if (steps >= maxSteps)
			continue;

		for (int32 directionIndex = 0; directionIndex < BattleSpatialService::DirectionCount; ++directionIndex)
		{
			Protocol::AxialCoord next;
			next.set_q(current.q() + BattleSpatialService::Directions[directionIndex][0]);
			next.set_r(current.r() + BattleSpatialService::Directions[directionIndex][1]);
			if (canTraverse(next) == false || visited.insert(makeKey(next)).second == false)
				continue;
			if (next.q() == target.q() && next.r() == target.r())
				return true;
			frontier.emplace(next, steps + 1);
		}
	}

	return false;
}
