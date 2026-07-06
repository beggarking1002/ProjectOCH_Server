#include "pch.h"
#include "Pawn.h"

Pawn::Pawn()
{
}

Pawn::Pawn(uint64 ownerId, uint64 pawnId, Protocol::PawnClass pawnClass, int32 level)
	: ownerId(ownerId), pawnId(pawnId), pawnClass(pawnClass), level(level)
{
}

Pawn::~Pawn()
{
}
