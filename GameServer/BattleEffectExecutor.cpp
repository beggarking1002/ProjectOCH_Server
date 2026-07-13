#include "pch.h"
#include "BattleEffectExecutor.h"

#include <cmath>

namespace
{
	string ToUpperString(string value)
	{
		transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
			{
				return static_cast<char>(toupper(ch));
			});
		return value;
	}
}

BattleEffectExecutionResult BattleEffectExecutor::ExecuteOnCast(const BattleEffectExecutionRequest& request)
{
	BattleEffectExecutionResult result;
	if (request.skill == nullptr || request.casterTemplate == nullptr)
		return result;

	const vector<BattleEffectTemplate>* effects = GBattleTemplates.GetEffects(request.skill->effectGroupKey);
	if (effects == nullptr)
		return result;

	for (const BattleEffectTemplate& effect : *effects)
	{
		if (effect.trigger != "ON_CAST")
			continue;

		if (effect.effectKey == "DEAL_DAMAGE")
			ExecuteDealDamage(effect, request, result);
		else if (effect.effectKey == "MODIFY_RESOURCE")
			ExecuteModifyResource(effect, request);
	}

	return result;
}

void BattleEffectExecutor::ExecuteDealDamage(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
	BattleEffectExecutionResult& result)
{
	BattleEffectPawnContext target = SelectTarget(effect, request);
	if (target.hp == nullptr || target.armor == nullptr)
		return;

	const int32 damage = CalculateValue(effect, *request.casterTemplate);
	const int32 appliedDamage = ApplyDamage(target, damage);
	result.totalDamage += appliedDamage;
	result.dealtDamage = true;

	if (request.logs != nullptr)
	{
		Protocol::BattleActionLog actionLog;
		actionLog.set_attacker_pawn_id(request.caster.pawnId);
		actionLog.set_defender_pawn_id(target.pawnId);
		actionLog.set_skill_slot(request.skillSlot);
		actionLog.set_action_type(request.actionType);
		actionLog.set_damage(appliedDamage);
		actionLog.set_is_critical(false);
		actionLog.set_is_evaded(false);
		actionLog.set_is_guarded(false);
		actionLog.set_is_perfect_guarded(false);
		actionLog.set_is_counter(false);
		actionLog.set_is_back_attack(request.isBackAttack);
		actionLog.set_hp_after(target.hp != nullptr ? *target.hp : 0);
		actionLog.set_armor_after(target.armor != nullptr ? *target.armor : 0);
		request.logs->push_back(actionLog);
	}
}

void BattleEffectExecutor::ExecuteModifyResource(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	BattleEffectPawnContext target = SelectTarget(effect, request);
	if (target.resources == nullptr)
		return;

	const string resourceKey = GetParam(effect, "resource_key");
	if (resourceKey.empty())
		return;

	int32 amount = GetIntParam(effect, "amount", 0);
	const string operation = ToUpperString(GetParam(effect, "operation"));
	if (operation == "MULTIPLY")
	{
		const double multiplier = GetDoubleParam(effect, "multiplier", 1.0);
		const int32 currentValue = (*target.resources)[resourceKey];
		amount = static_cast<int32>(floor(static_cast<double>(currentValue) * multiplier)) - currentValue;
	}

	int32& value = (*target.resources)[resourceKey];
	value += amount;

	int32 maxValue = 0;
	if (target.maxResources != nullptr)
	{
		auto maxIt = target.maxResources->find(resourceKey);
		if (maxIt != target.maxResources->end())
			maxValue = maxIt->second;
	}

	if (maxValue > 0)
		value = min(max(value, 0), maxValue);

	cout << "BATTLE_RESOURCE_MODIFY"
		<< " pawn_id=" << target.pawnId
		<< " resource_key=" << resourceKey
		<< " amount=" << amount
		<< " value=" << value
		<< " max_value=" << maxValue
		<< endl;
}

int32 BattleEffectExecutor::CalculateValue(const BattleEffectTemplate& effect, const BattlePawnClassTemplate& casterTemplate)
{
	const int32 baseValue = GetIntParam(effect, "base_value", 0);
	const string scalingStat = GetParam(effect, "scaling_stat");
	const double coefficient = GetDoubleParam(effect, "coefficient", 0.0);
	const int32 scalingValue = GetStatValue(casterTemplate, scalingStat);
	return baseValue + static_cast<int32>(floor(static_cast<double>(scalingValue) * coefficient));
}

int32 BattleEffectExecutor::GetStatValue(const BattlePawnClassTemplate& pawnTemplate, const string& statKey) const
{
	const string normalized = ToUpperString(statKey);
	if (normalized == "STR")
		return pawnTemplate.baseStr;
	if (normalized == "CON")
		return pawnTemplate.baseCon;
	if (normalized == "DEX")
		return pawnTemplate.baseDex;
	if (normalized == "SPELL")
		return pawnTemplate.baseSpell;
	if (normalized == "DEFENSE")
		return pawnTemplate.baseDefense;
	if (normalized == "FOCUS")
		return pawnTemplate.baseFocus;
	if (normalized == "WILL")
		return pawnTemplate.baseWill;
	return 0;
}

int32 BattleEffectExecutor::ApplyDamage(BattleEffectPawnContext target, int32 damage)
{
	if (target.hp == nullptr || target.armor == nullptr)
		return 0;

	const int32 beforeHp = *target.hp;
	const int32 beforeArmor = *target.armor;
	const int32 armorDamage = min(*target.armor, damage);
	*target.armor -= armorDamage;

	const int32 hpDamage = damage - armorDamage;
	if (hpDamage > 0)
		*target.hp = max(0, *target.hp - hpDamage);

	const int32 afterHp = *target.hp;
	const int32 afterArmor = *target.armor;
	return (beforeHp - afterHp) + (beforeArmor - afterArmor);
}

BattleEffectPawnContext BattleEffectExecutor::SelectTarget(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	if (effect.effectTarget == "CASTER" || effect.effectTarget == "SELF")
		return request.caster;
	return request.target;
}

string BattleEffectExecutor::GetParam(const BattleEffectTemplate& effect, const string& key, const string& fallback) const
{
	auto it = effect.params.find(key);
	if (it == effect.params.end())
		return fallback;
	return it->second;
}

int32 BattleEffectExecutor::GetIntParam(const BattleEffectTemplate& effect, const string& key, int32 fallback) const
{
	const string value = GetParam(effect, key);
	if (value.empty())
		return fallback;

	try
	{
		return static_cast<int32>(stod(value));
	}
	catch (...)
	{
		return fallback;
	}
}

double BattleEffectExecutor::GetDoubleParam(const BattleEffectTemplate& effect, const string& key, double fallback) const
{
	const string value = GetParam(effect, key);
	if (value.empty())
		return fallback;

	try
	{
		return stod(value);
	}
	catch (...)
	{
		return fallback;
	}
}
