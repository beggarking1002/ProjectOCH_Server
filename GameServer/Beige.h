#pragma once

#include "BattlePawn.h"

class Beige : public BattlePawn
{
public:
	const char* GetBehaviorKey() const override { return "BEIGE"; }
};
