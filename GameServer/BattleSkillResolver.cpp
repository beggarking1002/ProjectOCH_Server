#include "pch.h"
#include "BattleSkillResolver.h"

bool BattleSkillResolver::IsStatusActive(const BattlePawn& pawn, const string& statusKey) const
{
	auto it = pawn.statuses.find(statusKey);
	return it != pawn.statuses.end() && it->second.remainingOwnerTurns != 0;
}

const BattleEffectTemplate* BattleSkillResolver::FindActiveSkillModifier(const BattlePawn& pawn, const string& targetSkillKey,
	const string& modifierType) const
{
	const BattleSkillTemplate* ultimate = GBattleTemplates.GetSkillByActionSlot(pawn.pawnClass, 6);
	if (ultimate == nullptr)
		return nullptr;

	const vector<BattleEffectTemplate>* effects = GBattleTemplates.GetEffects(ultimate->effectGroupKey);
	if (effects == nullptr)
		return nullptr;

	for (const BattleEffectTemplate& effect : *effects)
	{
		if (effect.trigger != "WHILE_EMPOWERED" || effect.targetSkillKey != targetSkillKey || effect.effectKey != "ADD_SKILL_MODIFIER")
			continue;

		auto modifierTypeIt = effect.params.find("modifier_type");
		if (modifierTypeIt == effect.params.end() || modifierTypeIt->second != modifierType)
			continue;

		auto statusIt = effect.params.find("required_status_key");
		if (statusIt != effect.params.end() && IsStatusActive(pawn, statusIt->second))
			return &effect;
	}

	return nullptr;
}

double BattleSkillResolver::GetDamageMultiplier(const BattlePawn& pawn, const string& targetSkillKey) const
{
	double multiplier = GetStatModifierMultiplier(pawn, "DAMAGE_DEALT");
	const BattleEffectTemplate* effect = FindActiveSkillModifier(pawn, targetSkillKey, "DAMAGE_MULTIPLIER");
	if (effect == nullptr)
		return multiplier;

	auto it = effect->params.find("multiplier");
	if (it == effect->params.end())
		return multiplier;

	try
	{
		return multiplier * max(0.0, stod(it->second));
	}
	catch (...)
	{
		return multiplier;
	}
}

double BattleSkillResolver::GetStatModifierMultiplier(const BattlePawn& pawn, const string& statKey) const

{
	return GetStatModifierMultiplier(pawn, statKey, "");
}

double BattleSkillResolver::GetStatModifierMultiplier(const BattlePawn& pawn, const string& statKey, const string& damageScope) const
{
	double multiplier = 1.0;
	for (const auto& item : pawn.statuses)
	{
		const BattleStatusState& status = item.second;
		if (status.remainingOwnerTurns == 0 || status.statKey != statKey)
			continue;
		if (status.damageScope.empty() == false && status.damageScope != damageScope)
			continue;
		if (status.modifierType == "ADD_RATIO")
			multiplier += status.modifierValue;
		else if (status.modifierType == "MULTIPLY")
			multiplier *= status.modifierValue;
	}

	return max(0.0, multiplier);
}

double BattleSkillResolver::GetStatModifierAdditiveRatio(const BattlePawn& pawn, const string& statKey) const
{
	double ratio = 0.0;
	for (const auto& item : pawn.statuses)
	{
		const BattleStatusState& status = item.second;
		if (status.remainingOwnerTurns != 0 && status.statKey == statKey && status.modifierType == "ADD_RATIO")
			ratio += status.modifierValue;
	}
	return ratio;
}

int32 BattleSkillResolver::GetExtraTargets(const BattlePawn& pawn, const string& targetSkillKey, const string& modifierType) const
{
	const BattleEffectTemplate* effect = FindActiveSkillModifier(pawn, targetSkillKey, modifierType);
	if (effect == nullptr)
		return 0;

	auto it = effect->params.find("extra_targets");
	if (it == effect->params.end())
		return 0;

	try
	{
		return max(0, static_cast<int32>(stod(it->second)));
	}
	catch (...)
	{
		return 0;
	}
}

int32 BattleSkillResolver::GetAuraRadiusBonus(const BattlePawn& pawn, const string& targetSkillKey) const
{
	const BattleEffectTemplate* effect = FindActiveSkillModifier(pawn, targetSkillKey, "AURA_RADIUS_DELTA");
	if (effect == nullptr)
		return 0;

	auto it = effect->params.find("radius_delta");
	if (it == effect->params.end())
		return 0;

	try
	{
		return max(0, static_cast<int32>(stod(it->second)));
	}
	catch (...)
	{
		return 0;
	}
}

string BattleSkillResolver::GetAreaShape(const BattlePawn& pawn, const string& targetSkillKey) const
{
	const BattleEffectTemplate* effect = FindActiveSkillModifier(pawn, targetSkillKey, "TARGET_SHAPE_OVERRIDE");
	if (effect == nullptr)
		return "";

	auto it = effect->params.find("shape");
	return it != effect->params.end() ? it->second : "";
}

void BattleSkillResolver::RefreshAuraRadii(BattlePawn& pawn) const
{
	for (auto& item : pawn.auras)
	{
		BattleAuraState& aura = item.second;
		if (aura.baseRadius <= 0)
			aura.baseRadius = aura.radius;
		aura.radius = aura.baseRadius + GetAuraRadiusBonus(pawn, aura.sourceSkillKey);
	}
}
