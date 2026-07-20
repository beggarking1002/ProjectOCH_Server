#pragma once

#include "Beige.h"

class BeigeIce final : public Beige
{
public:
	const char* GetBehaviorKey() const override { return "BEIGE_ICE"; }
	vector<Protocol::AxialCoord> ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target) const override;
};
