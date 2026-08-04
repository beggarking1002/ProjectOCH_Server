#include "pch.h"
#include "BattleSpatialService.h"
#include "BattlePawn.h"
#include "BattleMapData.h"

const int32 BattleSpatialService::Directions[BattleSpatialService::DirectionCount][2] =
{
	{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
};

namespace
{
	Protocol::BattleFacingDirection FacingFromDirectionIndex(int32 index)
	{
		return static_cast<Protocol::BattleFacingDirection>(index + 1);
	}

	int32 FacingToDirectionIndex(Protocol::BattleFacingDirection facing)
	{
		const int32 index = static_cast<int32>(facing) - 1;
		return index >= 0 && index < BattleSpatialService::DirectionCount ? index : -1;
	}
}

bool BattleSpatialService::IsInBounds(const Protocol::AxialCoord& coord) const
{
	return GBattleMapData.ContainsTile(coord);
}

int32 BattleSpatialService::AxialDistance(const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs) const
{
	const int32 dq = lhs.q() - rhs.q();
	const int32 dr = lhs.r() - rhs.r();
	const int32 ds = -dq - dr;
	return (abs(dq) + abs(dr) + abs(ds)) / 2;
}

int32 BattleSpatialService::FindClosestDirectionIndex(const Protocol::AxialCoord& source, const Protocol::AxialCoord& target) const
{
	const int32 q = target.q() - source.q();
	const int32 r = target.r() - source.r();
	const int32 s = -q - r;
	if (q == 0 && r == 0)
		return -1;

	int32 bestDirection = 0;
	int32 bestDotProduct = numeric_limits<int32>::lowest();
	for (int32 index = 0; index < DirectionCount; ++index)
	{
		const int32 directionQ = Directions[index][0];
		const int32 directionR = Directions[index][1];
		const int32 directionS = -directionQ - directionR;
		const int32 dotProduct = q * directionQ + r * directionR + s * directionS;
		if (dotProduct > bestDotProduct)
		{
			bestDotProduct = dotProduct;
			bestDirection = index;
		}
	}
	return bestDirection;
}

Protocol::BattleFacingDirection BattleSpatialService::GetFacingToward(const Protocol::AxialCoord& start,
	const Protocol::AxialCoord& target, Protocol::BattleFacingDirection fallbackFacing) const
{
	const int32 directionIndex = FindClosestDirectionIndex(start, target);
	return directionIndex >= 0 ? FacingFromDirectionIndex(directionIndex) : fallbackFacing;
}

bool BattleSpatialService::IsBackAttack(const BattlePawn& attacker, const BattlePawn& defender) const
{
	const int32 facingIndex = FacingToDirectionIndex(defender.facingDirection);
	const int32 attackerDirection = FindClosestDirectionIndex(defender.axial, attacker.axial);
	if (facingIndex < 0 || attackerDirection < 0)
		return false;

	const int32 rearDirection = (facingIndex + 3) % DirectionCount;
	return attackerDirection == rearDirection || attackerDirection == (rearDirection + 1) % DirectionCount ||
		attackerDirection == (rearDirection + DirectionCount - 1) % DirectionCount;
}
