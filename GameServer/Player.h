#pragma once
#include "Creature.h"
#include "Pawn.h"

class GameSession;
class Room;

class Player : public Creature
{
public:
	Player();
	virtual ~Player();

public:
	weak_ptr<GameSession> session;
	vector<PawnRef> battlePawns;

public:
	PawnRef AddBattlePawn(Protocol::PawnClass pawnClass, int32 level = 1);

};

