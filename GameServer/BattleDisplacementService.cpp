#include "pch.h"
#include "BattleDisplacementService.h"
#include "BattlePawn.h"

BattlePushResult BattleDisplacementService::TryPush(const BattleDisplacementRequest& request) const
{
	BattlePushResult result;
	if (request.attacker == nullptr || request.target == nullptr || request.isWalkable == nullptr ||
		request.findAlivePawnAt == nullptr || request.attacker->isDead || request.target->isDead ||
		request.attacker->hp <= 0 || request.target->hp <= 0)
	{
		return result;
	}

	const int32 directionIndex = _spatialService.FindClosestDirectionIndex(request.attacker->axial, request.target->axial);
	if (directionIndex < 0)
		return result;

	Protocol::AxialCoord destination;
	destination.set_q(request.target->axial.q() + BattleSpatialService::Directions[directionIndex][0]);
	destination.set_r(request.target->axial.r() + BattleSpatialService::Directions[directionIndex][1]);
	if (_spatialService.IsInBounds(destination) == false)
		return result;
	if (request.isWalkable(destination) == false)
	{
		result.blockedByObstacle = true;
		return result;
	}

	if (BattlePawn* collisionPawn = request.findAlivePawnAt(destination))
	{
		if (collisionPawn->pawnId != request.target->pawnId)
			result.collisionPawn = collisionPawn;
		return result;
	}

	request.target->axial.CopyFrom(destination);
	cout << "BATTLE_PUSH_SUCCESS"
		<< " attacker_pawn_id=" << request.attacker->pawnId
		<< " defender_pawn_id=" << request.target->pawnId
		<< " target=(" << destination.q() << "," << destination.r() << ")"
		<< endl;
	result.pushed = true;
	return result;
}
