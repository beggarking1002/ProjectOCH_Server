#pragma once

#include "BattlePawn.h"

class Suen : public BattlePawn
{
public:
	const char* GetBehaviorKey() const override { return "SUEN"; }

protected:
	bool HasActiveStatus(const string& statusKey) const;
};
