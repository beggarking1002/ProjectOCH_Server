#pragma once

#include "BattleSpatialService.h"

class BattlePawn;

struct BattlePushResult
{
	bool pushed = false;
	bool blockedByObstacle = false;
	BattlePawn* collisionPawn = nullptr;
};

struct BattleRetreatResult
{
	bool moved = false;
	bool swappedWithAlly = false;
	BattlePawn* swappedAlly = nullptr;
};

struct BattleDashResult
{
	bool moved = false;
};

struct BattleDisplacementRequest
{
	BattlePawn* attacker = nullptr;
	BattlePawn* target = nullptr;
	function<bool(const Protocol::AxialCoord&)> isWalkable;
	function<BattlePawn*(const Protocol::AxialCoord&)> findAlivePawnAt;
};

// Resolves forced movement without owning a battle room.  The room supplies
// board queries while this service owns direction and collision decisions.
class BattleDisplacementService
{
public:
	explicit BattleDisplacementService(const BattleSpatialService& spatialService) : _spatialService(spatialService) { }
	BattlePushResult TryPush(const BattleDisplacementRequest& request) const;
	BattleRetreatResult TryRetreatFromTarget(const BattleDisplacementRequest& request) const;
	BattleDashResult TryDashTowardTarget(const BattleDisplacementRequest& request, const Protocol::AxialCoord& targetAxial,
		int32 maxDistance) const;

private:
	const BattleSpatialService& _spatialService;
};
