#pragma once

#include "Alen.h"

class AlenSpear final : public Alen
{
public:
	const char* GetBehaviorKey() const override { return "ALEN_SPEAR"; }
	vector<Protocol::AxialCoord> ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target,
		const Protocol::AxialCoord* directionTarget = nullptr) const override;
};
