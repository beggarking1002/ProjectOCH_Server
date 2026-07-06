#include "pch.h"
#include "Player.h"

Player::Player()
{
	_isPlayer = true;
	objectInfo->set_creature_type(Protocol::CreatureType::CREATURE_TYPE_PLAYER);
}

Player::~Player()
{

}

PawnRef Player::AddBattlePawn(Protocol::PawnClass pawnClass, int32 level)
{
	const uint64 ownerId = objectInfo != nullptr ? objectInfo->object_id() : 0;
	const uint64 pawnId = ownerId * 100 + static_cast<uint64>(battlePawns.size()) + 1;

	PawnRef pawn = make_shared<Pawn>(ownerId, pawnId, pawnClass, level);
	battlePawns.push_back(pawn);
	return pawn;
}
