#pragma once

class Pawn
{
public:
	Pawn();
	Pawn(uint64 ownerId, uint64 pawnId, Protocol::PawnClass pawnClass, int32 level = 1);
	virtual ~Pawn();

public:
	uint64 ownerId = 0;
	uint64 pawnId = 0;
	Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
	int32 level = 1;
};

using PawnRef = shared_ptr<Pawn>;
