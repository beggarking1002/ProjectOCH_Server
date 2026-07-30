#pragma once

#include "Struct.pb.h"
#include "Enum.pb.h"

class BattlePawn;

// Stateless hex-grid geometry shared by movement, ZOC, knockback and combat
// direction rules.  It deliberately has no BattleRoom/BattleState dependency.
class BattleSpatialService
{
public:
	bool IsInBounds(const Protocol::AxialCoord& coord) const;
	int32 AxialDistance(const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs) const;
	int32 FindClosestDirectionIndex(const Protocol::AxialCoord& source, const Protocol::AxialCoord& target) const;
	Protocol::BattleFacingDirection GetFacingForMove(const Protocol::AxialCoord& start, const Protocol::AxialCoord& target,
		Protocol::BattleFacingDirection fallbackFacing) const;
	bool IsBackAttack(const BattlePawn& attacker, const BattlePawn& defender) const;

	static const int32 DirectionCount = 6;
	static const int32 Directions[DirectionCount][2];
};
