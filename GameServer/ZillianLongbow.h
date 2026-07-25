#pragma once

#include "Zillian.h"

class ZillianLongbow final : public Zillian
{
public:
	const char* GetBehaviorKey() const override { return "ZILLIAN_LONGBOW"; }
	void OnSuccessfulHitReceived(BattlePawn& attacker) override;
	bool RequiresHitCheck(const BattleSkillTemplate& skill) const override;
};
