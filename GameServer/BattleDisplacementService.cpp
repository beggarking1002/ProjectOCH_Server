#include "pch.h"
#include "BattleDisplacementService.h"
#include "BattlePawn.h"

BattlePushResult BattleDisplacementService::TryPush(const BattleDisplacementRequest& request) const
{
	BattlePushResult result;
	if (request.attacker == nullptr || request.target == nullptr || request.isWalkable == nullptr ||
		request.findAlivePawnAt == nullptr || request.attacker->isDead || request.attacker->hp <= 0)
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

BattleRetreatResult BattleDisplacementService::TryRetreatFromTarget(const BattleDisplacementRequest& request) const
{
	BattleRetreatResult result;
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
	destination.set_q(request.attacker->axial.q() - BattleSpatialService::Directions[directionIndex][0]);
	destination.set_r(request.attacker->axial.r() - BattleSpatialService::Directions[directionIndex][1]);
	if (_spatialService.IsInBounds(destination) == false || request.isWalkable(destination) == false)
		return result;

	BattlePawn* occupant = request.findAlivePawnAt(destination);
	if (occupant != nullptr && occupant->pawnId != request.attacker->pawnId)
	{
		if (occupant->ownerId != request.attacker->ownerId)
			return result;

		Protocol::AxialCoord originalCasterAxial = request.attacker->axial;
		request.attacker->axial.CopyFrom(occupant->axial);
		occupant->axial.CopyFrom(originalCasterAxial);
		result.moved = true;
		result.swappedWithAlly = true;
		result.swappedAlly = occupant;
	}
	else
	{
		request.attacker->axial.CopyFrom(destination);
		result.moved = true;
	}

	if (result.moved)
	{
		cout << "BATTLE_RETREAT_SUCCESS pawn_id=" << request.attacker->pawnId
			<< " target_pawn_id=" << request.target->pawnId
			<< " swapped_with_ally=" << result.swappedWithAlly
			<< " destination=(" << request.attacker->axial.q() << "," << request.attacker->axial.r() << ")"
			<< endl;
	}
	return result;
}
