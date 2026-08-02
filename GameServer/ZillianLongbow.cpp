#include "pch.h"
#include "ZillianLongbow.h"

void ZillianLongbow::OnSuccessfulHitReceived(BattlePawn& attacker)
{
	BattleEffectExecutor effectExecutor;
	effectExecutor.ApplyDizzyToPawn(*this, attacker);
}

bool ZillianLongbow::RequiresHitCheck(const BattleSkillTemplate& skill) const
{
	// A support skill that fires an arrow still uses the normal hit check.
	return skill.skillKey == "ZILLIAN_LONGBOW_ROUGH_HEALING";
}
