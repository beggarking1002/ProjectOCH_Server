#include "pch.h"
#include "Creature.h"

Creature::Creature()
{
	objectInfo->set_object_type(Protocol::ObjectType::OBJECT_TYPE_CREATURE);
	objectInfo->set_creature_type(Protocol::CreatureType::CREATURE_TYPE_NONE);
}

Creature::~Creature()
{

}
