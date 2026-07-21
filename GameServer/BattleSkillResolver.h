#pragma once

#include "BattlePawn.h"

class BattleSkillResolver
{
public:
	double GetDamageMultiplier(const BattlePawn& pawn, const string& targetSkillKey) const;
	double GetStatModifierMultiplier(const BattlePawn& pawn, const string& statKey) const;
	double GetStatModifierMultiplier(const BattlePawn& pawn, const string& statKey, const string& damageScope) const;
	double GetStatModifierAdditiveRatio(const BattlePawn& pawn, const string& statKey) const;
	int32 GetExtraTargets(const BattlePawn& pawn, const string& targetSkillKey, const string& modifierType) const;
	int32 GetAuraRadiusBonus(const BattlePawn& pawn, const string& targetSkillKey) const;
	string GetAreaShape(const BattlePawn& pawn, const string& targetSkillKey) const;
	void RefreshAuraRadii(BattlePawn& pawn) const;

private:
	bool IsStatusActive(const BattlePawn& pawn, const string& statusKey) const;
	const BattleEffectTemplate* FindActiveSkillModifier(const BattlePawn& pawn, const string& targetSkillKey,
		const string& modifierType) const;
};
