#pragma once

#include "Alen.h"

class AlenShield final : public Alen
{
public:
	const char* GetBehaviorKey() const override { return "ALEN_SHIELD"; }
	bool CanInterceptSingleTargetAttack() const override;
	bool UsesConditionalCounterattack() const override { return true; }
	bool CanCounterattackOnSuccessfulHit() const override;
};
