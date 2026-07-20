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
	return ExecuteTrigger(request, "ON_CAST");
}

BattleEffectExecutionResult BattleEffectExecutor::ExecuteTrigger(const BattleEffectExecutionRequest& request, const string& trigger,
	BattleEffectTargetScope scope)
{
	BattleEffectExecutionResult result;
	if (request.skill == nullptr || request.casterTemplate == nullptr)
		return result;

	const vector<BattleEffectTemplate>* effects = GBattleTemplates.GetEffects(request.skill->effectGroupKey);
	if (effects == nullptr)
		return result;

	auto matchesScope = [scope](const BattleEffectTemplate& effect)
		{
			const bool casterEffect = effect.effectTarget == "CASTER" || effect.effectTarget == "SELF";
			if (scope == BattleEffectTargetScope::CasterOnly)
				return casterEffect;
			if (scope == BattleEffectTargetScope::TargetOnly)
				return casterEffect == false;
			return true;
		};

	unordered_map<string, int32> stopPriorities;
	for (const BattleEffectTemplate& effect : *effects)
	{
		if (matchesScope(effect) == false || effect.trigger != trigger || effect.exclusiveGroup.empty() || effect.stopOnMatch == false || IsConditionMet(effect, request) == false)
			continue;

		auto [it, inserted] = stopPriorities.emplace(effect.exclusiveGroup, effect.exclusivePriority);
		if (inserted == false)
			it->second = min(it->second, effect.exclusivePriority);
	}

	for (const BattleEffectTemplate& effect : *effects)
	{
		if (matchesScope(effect) == false || effect.trigger != trigger || IsConditionMet(effect, request) == false)
			continue;

		if (effect.exclusiveGroup.empty() == false)
		{
			auto stopIt = stopPriorities.find(effect.exclusiveGroup);
			if (stopIt != stopPriorities.end() && effect.exclusivePriority > stopIt->second)
				continue;
		}

		if (effect.effectKey == "DEAL_DAMAGE")
			ExecuteDealDamage(effect, request, result);
		else if (effect.effectKey == "MODIFY_RESOURCE")
			ExecuteModifyResource(effect, request);
		else if (effect.effectKey == "SET_RESOURCE_MAX")
			ExecuteSetResourceMax(effect, request);
		else if (effect.effectKey == "APPLY_BARRIER")
			ExecuteApplyBarrier(effect, request);
		else if (effect.effectKey == "APPLY_STATUS")
			ExecuteApplyStatus(effect, request);
		else if (effect.effectKey == "APPLY_STAT_MODIFIER")
			ExecuteApplyStatus(effect, request);
		else if (effect.effectKey == "TOGGLE_AURA")
			ExecuteToggleAura(effect, request);
		else if (effect.effectKey == "CHANGE_TILE_TYPE")
			ExecuteChangeTileOverlay(effect, request, result);
	}

	return result;
}

void BattleEffectExecutor::AdvanceOwnerTurn(BattleEffectPawnContext pawn)
{
	if (pawn.barriers != nullptr)
	{
		for (BattleBarrierState& barrier : *pawn.barriers)
			barrier.remainingOwnerTurns--;

		for (const BattleBarrierState& barrier : *pawn.barriers)
		{
			if (barrier.value > 0 && barrier.remainingOwnerTurns <= 0)
			{
				cout << "BATTLE_BARRIER_EXPIRE"
					<< " pawn_id=" << pawn.pawnId
					<< " source_skill_key=" << barrier.sourceSkillKey
					<< endl;
			}
		}

		auto eraseBegin = remove_if(pawn.barriers->begin(), pawn.barriers->end(), [](const BattleBarrierState& barrier)
			{
				return barrier.value <= 0 || barrier.remainingOwnerTurns <= 0;
			});
		pawn.barriers->erase(eraseBegin, pawn.barriers->end());
	}

	if (pawn.statuses == nullptr)
		return;

	for (auto& item : *pawn.statuses)
	{
		if (item.second.remainingOwnerTurns > 0)
			item.second.remainingOwnerTurns--;
	}

	for (const auto& item : *pawn.statuses)
	{
		if (item.second.remainingOwnerTurns == 0)
		{
			cout << "BATTLE_STATUS_EXPIRE"
				<< " pawn_id=" << pawn.pawnId
				<< " status_key=" << item.first
				<< endl;
		}
	}

	for (auto it = pawn.statuses->begin(); it != pawn.statuses->end();)
	{
		if (it->second.remainingOwnerTurns == 0)
			it = pawn.statuses->erase(it);
		else
			++it;
	}
}

void BattleEffectExecutor::ExecuteDealDamage(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
	BattleEffectExecutionResult& result)
{
	BattleEffectPawnContext target = SelectTarget(effect, request);
	if (target.hp == nullptr || target.armor == nullptr)
		return;

	const int32 damage = static_cast<int32>(floor(static_cast<double>(CalculateValue(effect, *request.casterTemplate)) *
		max(0.0, request.damageMultiplier)));
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
	Protocol::BattleResourceType resourceType = Protocol::BATTLE_RESOURCE_TYPE_NONE;
	if (resourceKey.empty() || GBattleTemplates.TryParseBattleResourceType(resourceKey, resourceType) == false)
		return;

	int32 amount = GetIntParam(effect, "amount", 0);
	const string operation = ToUpperString(GetParam(effect, "operation"));
	if (operation == "MULTIPLY")
	{
		const double multiplier = GetDoubleParam(effect, "multiplier", 1.0);
		const int32 currentValue = (*target.resources)[resourceType];
		amount = static_cast<int32>(floor(static_cast<double>(currentValue) * multiplier)) - currentValue;
	}

	int32& value = (*target.resources)[resourceType];
	value += amount;

	int32 maxValue = 0;
	if (target.maxResources != nullptr)
	{
		auto maxIt = target.maxResources->find(resourceType);
		if (maxIt != target.maxResources->end())
			maxValue = maxIt->second;
	}

	if (maxValue > 0)
		value = min(max(value, 0), maxValue);

	cout << "BATTLE_RESOURCE_MODIFY"
		<< " pawn_id=" << target.pawnId
		<< " resource_type=" << Protocol::BattleResourceType_Name(resourceType)
		<< " amount=" << amount
		<< " value=" << value
		<< " max_value=" << maxValue
		<< endl;
}

void BattleEffectExecutor::ExecuteSetResourceMax(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	BattleEffectPawnContext target = SelectTarget(effect, request);
	if (target.resources == nullptr || target.maxResources == nullptr)
		return;

	const string resourceKey = GetParam(effect, "resource_key");
	Protocol::BattleResourceType resourceType = Protocol::BATTLE_RESOURCE_TYPE_NONE;
	if (resourceKey.empty() || GBattleTemplates.TryParseBattleResourceType(resourceKey, resourceType) == false)
		return;

	const int32 maxValue = max(0, GetIntParam(effect, "max_value", 0));
	(*target.maxResources)[resourceType] = maxValue;
	int32& value = (*target.resources)[resourceType];
	value = min(max(value, 0), maxValue);

	cout << "BATTLE_RESOURCE_MAX_SET"
		<< " pawn_id=" << target.pawnId
		<< " resource_type=" << Protocol::BattleResourceType_Name(resourceType)
		<< " value=" << value
		<< " max_value=" << maxValue
		<< endl;
}

void BattleEffectExecutor::ExecuteApplyBarrier(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	BattleEffectPawnContext target = SelectTarget(effect, request);
	if (target.barriers == nullptr)
		return;

	const int32 value = CalculateValue(effect, *request.casterTemplate);
	const int32 durationTurns = GetIntParam(effect, "duration_turns", 0);
	if (value <= 0 || durationTurns <= 0)
		return;

	BattleBarrierState barrier;
	barrier.barrierId = request.barrierIdGenerator != nullptr ? (*request.barrierIdGenerator)++ : 0;
	barrier.sourceSkillKey = request.skill != nullptr ? request.skill->skillKey : "";
	barrier.value = value;
	barrier.maxValue = value;
	barrier.remainingOwnerTurns = durationTurns;
	target.barriers->push_back(barrier);

	cout << "BATTLE_BARRIER_APPLY"
		<< " pawn_id=" << target.pawnId
		<< " value=" << value
		<< " duration_turns=" << durationTurns
		<< endl;
}

void BattleEffectExecutor::ExecuteApplyStatus(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	BattleEffectPawnContext target = SelectTarget(effect, request);
	if (target.statuses == nullptr)
		return;

	const string statusKey = GetParam(effect, "status_key");
	if (statusKey.empty())
		return;

	const int32 stackDelta = GetIntParam(effect, "stack_delta", 1);
	BattleStatusState& status = (*target.statuses)[statusKey];
	const string stackPolicy = ToUpperString(GetParam(effect, "stack_policy", "ADD"));
	if (stackPolicy == "REFRESH")
		status.stacks = max(1, status.stacks);
	else
		status.stacks = max(0, status.stacks + stackDelta);
	const int32 durationTurns = GetIntParam(effect, "duration_turns", -1);
	if (durationTurns > 0)
		status.remainingOwnerTurns = max(status.remainingOwnerTurns, durationTurns);

	cout << "BATTLE_STATUS_APPLY"
		<< " pawn_id=" << target.pawnId
		<< " status_key=" << statusKey
		<< " stack_delta=" << stackDelta
		<< " stack=" << status.stacks
		<< endl;
}

void BattleEffectExecutor::ExecuteToggleAura(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	if (request.skill == nullptr || request.caster.auras == nullptr)
		return;

	const int32 baseRadius = max(0, GetIntParam(effect, "aura_radius", 0));
	if (baseRadius <= 0)
		return;

	auto it = request.caster.auras->find(request.skill->skillKey);
	if (it != request.caster.auras->end())
	{
		request.caster.auras->erase(it);
		cout << "BATTLE_AURA_TOGGLE pawn_id=" << request.caster.pawnId
			<< " skill_key=" << request.skill->skillKey << " active=0" << endl;
		return;
	}

	BattleAuraState aura;
	aura.sourceSkillKey = request.skill->skillKey;
	aura.baseRadius = baseRadius;
	aura.radius = baseRadius + max(0, request.auraRadiusBonus);
	(*request.caster.auras)[aura.sourceSkillKey] = aura;
	cout << "BATTLE_AURA_TOGGLE pawn_id=" << request.caster.pawnId
		<< " skill_key=" << aura.sourceSkillKey << " radius=" << aura.radius << " active=1" << endl;
}

void BattleEffectExecutor::ExecuteChangeTileOverlay(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
	BattleEffectExecutionResult& result)
{
	if (request.targetAxial == nullptr || request.getBaseTileType == nullptr || request.getTileOverlayType == nullptr ||
		request.setTileOverlayType == nullptr || request.isTileValid == nullptr)
		return;

	Protocol::BattleTileType requiredTileType = Protocol::BATTLE_TILE_TYPE_NONE;
	Protocol::BattleTileOverlayType changedOverlayType = Protocol::BATTLE_TILE_OVERLAY_TYPE_NONE;
	if (GBattleTemplates.TryParseBattleTileType(GetParam(effect, "tile_filter"), requiredTileType) == false ||
		GBattleTemplates.TryParseBattleTileOverlayType(GetParam(effect, "overlay_type"), changedOverlayType) == false)
		return;

	const Protocol::AxialCoord& center = *request.targetAxial;
	if (request.getBaseTileType(center) != requiredTileType)
		return;

	vector<Protocol::AxialCoord> targets;
	targets.push_back(center);
	if (effect.effectTarget == "TARGET_AND_NEIGHBORS")
	{
		static constexpr int32 kNeighborOffsets[6][2] =
		{
			{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
		};

		for (const auto& offset : kNeighborOffsets)
		{
			Protocol::AxialCoord neighbor;
			neighbor.set_q(center.q() + offset[0]);
			neighbor.set_r(center.r() + offset[1]);
			targets.push_back(neighbor);
		}
	}

	for (const Protocol::AxialCoord& target : targets)
	{
		if (request.isTileValid(target) == false || request.getTileOverlayType(target) == changedOverlayType)
			continue;

		request.setTileOverlayType(target, changedOverlayType);
		Protocol::BattleTileInfo delta;
		delta.mutable_axial()->CopyFrom(target);
		delta.set_tile_type(request.getBaseTileType(target));
		delta.set_overlay_type(changedOverlayType);
		result.tileDeltas.push_back(delta);

		cout << "BATTLE_TILE_CHANGE"
			<< " q=" << target.q()
			<< " r=" << target.r()
			<< " base_tile_type=" << Protocol::BattleTileType_Name(request.getBaseTileType(target))
			<< " overlay_type=" << Protocol::BattleTileOverlayType_Name(changedOverlayType)
			<< endl;
	}
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
	int32 barrierDamage = 0;
	if (target.barriers != nullptr)
	{
		for (auto it = target.barriers->rbegin(); it != target.barriers->rend() && damage > 0; ++it)
		{
			const int32 absorbed = min(it->value, damage);
			it->value -= absorbed;
			damage -= absorbed;
			barrierDamage += absorbed;
		}

		if (barrierDamage > 0)
		{
			cout << "BATTLE_BARRIER_ABSORB"
				<< " pawn_id=" << target.pawnId
				<< " amount=" << barrierDamage
				<< endl;
		}

		auto eraseBegin = remove_if(target.barriers->begin(), target.barriers->end(), [](const BattleBarrierState& barrier)
			{
				return barrier.value <= 0;
			});
		target.barriers->erase(eraseBegin, target.barriers->end());
	}

	const int32 armorDamage = min(*target.armor, damage);
	*target.armor -= armorDamage;

	const int32 hpDamage = damage - armorDamage;
	if (hpDamage > 0)
		*target.hp = max(0, *target.hp - hpDamage);

	const int32 afterHp = *target.hp;
	const int32 afterArmor = *target.armor;
	return barrierDamage + (beforeHp - afterHp) + (beforeArmor - afterArmor);
}

BattleEffectPawnContext BattleEffectExecutor::SelectTarget(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request) const
{
	if (effect.effectTarget == "CASTER" || effect.effectTarget == "SELF")
		return request.caster;
	return request.target;
}

bool BattleEffectExecutor::IsConditionMet(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request) const
{
	BattleEffectPawnContext target = SelectTarget(effect, request);
	const auto hasActiveStatus = [&target](const string& statusKey)
		{
			if (statusKey.empty() || target.statuses == nullptr)
				return false;
			auto it = target.statuses->find(statusKey);
			return it != target.statuses->end() && it->second.remainingOwnerTurns != 0;
		};

	if (hasActiveStatus(GetParam(effect, "blocked_by_status")))
		return false;

	const string requiredStatusKey = GetParam(effect, "required_status_key");
	if (requiredStatusKey.empty() == false && hasActiveStatus(requiredStatusKey) == false)
		return false;

	const string resourceKey = GetParam(effect, "condition_resource_key");
	if (resourceKey.empty())
		return true;

	Protocol::BattleResourceType resourceType = Protocol::BATTLE_RESOURCE_TYPE_NONE;
	if (GBattleTemplates.TryParseBattleResourceType(resourceKey, resourceType) == false)
		return false;

	if (target.resources == nullptr || target.maxResources == nullptr)
		return false;

	auto valueIt = target.resources->find(resourceType);
	auto maxIt = target.maxResources->find(resourceType);
	if (valueIt == target.resources->end() || maxIt == target.maxResources->end() || maxIt->second <= 0)
		return false;

	const double thresholdRatio = GetDoubleParam(effect, "threshold_ratio", 0.0);
	return static_cast<double>(valueIt->second) / static_cast<double>(maxIt->second) >= thresholdRatio;
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
