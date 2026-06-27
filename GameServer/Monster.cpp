#include "pch.h"
#include "Monster.h"

Monster::Monster()
{
	objectInfo->set_creature_type(Protocol::CreatureType::CREATURE_TYPE_MONSTER);
}

Monster::~Monster()
{
}