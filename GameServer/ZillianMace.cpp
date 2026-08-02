#include "pch.h"
#include "ZillianMace.h"

void ZillianMace::OnSuccessfulHitReceived(BattlePawn& attacker)
{
	BattleEffectExecutor effectExecutor;
	effectExecutor.ApplyDizzyToPawn(*this, attacker);
}
