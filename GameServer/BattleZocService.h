#pragma once

#include "BattlePawn.h"

struct BattleZocProfile
{
	bool enabled = false;
	int32 range = 1;
	int32 frontArcWidth = 3;
	int32 reactionLimitPerTurn = 1;
	int32 reactionSkillSlot = 2;
	unordered_set<string> triggers;

	bool SupportsTrigger(const string& trigger) const
	{
		return triggers.find(trigger) != triggers.end();
	}
};

class BattleZocService
{
public:
	BattleZocProfile GetProfile(const BattlePawn& pawn) const;
	bool IsInsideZone(const BattlePawn& zocOwner, const BattleZocProfile& profile,
		const Protocol::AxialCoord& axial) const;
	bool ShouldTriggerOnEnemyMoveInZone(const BattlePawn& zocOwner, const Protocol::AxialCoord& enemyStart) const;
	bool ShouldTriggerOnAllyAttacked(const BattlePawn& zocOwner, const BattlePawn& attacker,
		const BattlePawn& attackedAlly) const;

private:
	bool HasMeleeReactionSkill(const BattlePawn& pawn) const;
	int32 AxialDistance(const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs) const;
	int32 FindClosestDirectionIndex(const Protocol::AxialCoord& source, const Protocol::AxialCoord& target) const;
};
