#pragma once

#include "Beige.h"

class BeigeFire final : public Beige
{
public:
	const char* GetBehaviorKey() const override { return "BEIGE_FIRE"; }
	vector<Protocol::AxialCoord> ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target) const override;
};
