#pragma once

#include "Zillian.h"

class ZillianMace final : public Zillian
{
public:
	const char* GetBehaviorKey() const override { return "ZILLIAN_MACE"; }
	void OnSuccessfulHitReceived(BattlePawn& attacker) override;
};
