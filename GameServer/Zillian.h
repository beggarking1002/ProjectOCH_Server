#pragma once

#include "BattlePawn.h"

class Zillian : public BattlePawn
{
public:
	const char* GetBehaviorKey() const override { return "ZILLIAN"; }
};
