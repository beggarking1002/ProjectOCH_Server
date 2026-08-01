#pragma once

#include "BattlePawn.h"

class Alen : public BattlePawn
{
public:
	const char* GetBehaviorKey() const override { return "ALEN"; }

protected:
	bool HasActiveStatus(const string& statusKey) const;
};
