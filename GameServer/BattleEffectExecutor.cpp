#include "pch.h"
#include "BattleEffectExecutor.h"
#include "BattlePawn.h"

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
		if (request.isEvaded && (effect.effectTarget == "TARGET" || effect.effectTarget == "TARGET_ENEMY"))
			continue;

		if (effect.exclusiveGroup.empty() == false)
		{
			auto stopIt = stopPriorities.find(effect.exclusiveGroup);
			if (stopIt != stopPriorities.end() && effect.exclusivePriority > stopIt->second)
				continue;
		}

		if (effect.effectKey == "DEAL_DAMAGE")
			ExecuteDealDamage(effect, request, result);
		else if (effect.effectKey == "RESTORE_HP")
			ExecuteRestoreHp(effect, request);
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
		else if (effect.effectKey == "TELEPORT_TO_OVERLAY")
			ExecuteTeleportToOverlay(effect, request);
		else if (effect.effectKey == "DROP_EQUIPMENT")
			ExecuteDropEquipment(effect, request, result);
		else if (effect.effectKey == "PICKUP_EQUIPMENT")
			ExecutePickupEquipment(effect, request, result);
		else if (effect.effectKey == "SWAP_POSITION")
			ExecuteSwapPosition(effect, request);
		else if (effect.effectKey == "ADD_STAT_FROM_STAT")
			ExecuteAddStatFromStat(effect, request);
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
		if (item.second.chargesPerOwnerTurn > 0)
			item.second.stacks = item.second.chargesPerOwnerTurn;
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
	if ((effect.effectTarget == "TARGET" || effect.effectTarget == "TARGET_ENEMY") && request.hasTargetPawn == false)
		return;

	BattleEffectPawnContext target = SelectTarget(effect, request);
	if (target.hp == nullptr || target.armor == nullptr)
		return;

	const bool canEvade = effect.effectTarget == "TARGET" || effect.effectTarget == "TARGET_ENEMY";
	const bool isEvaded = canEvade && request.isEvaded;
	const int32 damage = static_cast<int32>(floor(static_cast<double>(CalculateValue(effect, *request.casterTemplate, request.caster)) *
		max(0.0, request.damageMultiplier) * max(0.0, request.targetDamageMultiplier)));
	const int32 appliedDamage = isEvaded ? 0 : ApplyDamage(target, damage);
	result.totalDamage += appliedDamage;
	result.dealtDamage = result.dealtDamage || isEvaded == false;

	if (request.logs != nullptr)
	{
		Protocol::BattleActionLog actionLog;
		actionLog.set_attacker_pawn_id(request.caster.pawnId);
		actionLog.set_defender_pawn_id(target.pawnId);
		actionLog.set_skill_slot(request.skillSlot);
		actionLog.set_action_type(request.actionType);
		actionLog.set_damage(appliedDamage);
		actionLog.set_is_critical(false);
		actionLog.set_is_evaded(isEvaded);
		actionLog.set_is_guarded(request.isGuarded);
		actionLog.set_is_perfect_guarded(false);
		actionLog.set_is_counter(request.isCounter);
		actionLog.set_is_back_attack(request.isBackAttack);
		actionLog.set_hp_after(target.hp != nullptr ? *target.hp : 0);
		actionLog.set_armor_after(target.armor != nullptr ? *target.armor : 0);
		request.logs->push_back(actionLog);
	}
}

void BattleEffectExecutor::ExecuteRestoreHp(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	BattleEffectPawnContext target = SelectTarget(effect, request);
	if (target.hp == nullptr || target.maxHp == nullptr)
		return;

	const int32 value = max(0, CalculateValue(effect, *request.casterTemplate, request.caster));
	if (value <= 0)
		return;

	const int32 beforeHp = *target.hp;
	*target.hp = min(*target.maxHp, *target.hp + value);
	cout << "BATTLE_HP_RESTORE"
		<< " pawn_id=" << target.pawnId
		<< " amount=" << (*target.hp - beforeHp)
		<< " hp=" << *target.hp
		<< endl;
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

	const int32 value = CalculateValue(effect, *request.casterTemplate, request.caster);
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
	const int32 chargesPerOwnerTurn = GetIntParam(effect, "charges_per_owner_turn", 0);
	if (chargesPerOwnerTurn > 0)
	{
		status.chargesPerOwnerTurn = chargesPerOwnerTurn;
		status.stacks = chargesPerOwnerTurn;
		status.consumeOn = GetParam(effect, "consume_on");
	}

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

void BattleEffectExecutor::ExecuteTeleportToOverlay(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	if (request.targetAxial == nullptr || request.caster.axial == nullptr || request.getTileOverlayType == nullptr ||
		request.isTileValid == nullptr)
	{
		return;
	}

	Protocol::BattleTileOverlayType requiredOverlayType = Protocol::BATTLE_TILE_OVERLAY_TYPE_NONE;
	if (GBattleTemplates.TryParseBattleTileOverlayType(GetParam(effect, "required_overlay_type"), requiredOverlayType) == false ||
		request.isTileValid(*request.targetAxial) == false || request.getTileOverlayType(*request.targetAxial) != requiredOverlayType)
	{
		return;
	}

	request.caster.axial->CopyFrom(*request.targetAxial);
	cout << "BATTLE_TELEPORT"
		<< " pawn_id=" << request.caster.pawnId
		<< " q=" << request.targetAxial->q()
		<< " r=" << request.targetAxial->r()
		<< " overlay_type=" << Protocol::BattleTileOverlayType_Name(requiredOverlayType)
		<< endl;
}

void BattleEffectExecutor::ExecuteDropEquipment(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
	BattleEffectExecutionResult& result)
{
	if (request.targetAxial == nullptr || request.setTileEquipment == nullptr || request.getBaseTileType == nullptr ||
		request.getTileOverlayType == nullptr)
	{
		return;
	}
	const string equipmentKey = GetParam(effect, "equipment_key");
	if (equipmentKey.empty() || request.casterPawn == nullptr || request.casterPawn->CanDropEquipment(equipmentKey) == false)
		return;

	request.setTileEquipment(*request.targetAxial, equipmentKey, request.caster.pawnId);
	Protocol::BattleTileInfo delta;
	delta.mutable_axial()->CopyFrom(*request.targetAxial);
	delta.set_tile_type(request.getBaseTileType(*request.targetAxial));
	delta.set_overlay_type(request.getTileOverlayType(*request.targetAxial));
	delta.set_equipment_key(equipmentKey);
	delta.set_equipment_owner_pawn_id(request.caster.pawnId);
	result.tileDeltas.push_back(delta);
	cout << "BATTLE_EQUIPMENT_DROP pawn_id=" << request.caster.pawnId << " equipment_key=" << equipmentKey
		<< " q=" << request.targetAxial->q() << " r=" << request.targetAxial->r() << endl;
}

void BattleEffectExecutor::ExecutePickupEquipment(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
	BattleEffectExecutionResult& result)
{
	if (request.targetAxial == nullptr || request.getTileEquipmentKey == nullptr || request.getTileEquipmentOwnerPawnId == nullptr ||
		request.setTileEquipment == nullptr || request.getBaseTileType == nullptr || request.getTileOverlayType == nullptr)
	{
		return;
	}
	const string equipmentKey = GetParam(effect, "equipment_key");
	if (equipmentKey.empty() || request.casterPawn == nullptr || request.getTileEquipmentKey(*request.targetAxial) != equipmentKey ||
		request.casterPawn->CanPickupEquipment(equipmentKey, request.getTileEquipmentOwnerPawnId(*request.targetAxial)) == false)
	{
		return;
	}

	request.setTileEquipment(*request.targetAxial, "", 0);
	request.casterPawn->OnEquipmentPickedUp(equipmentKey);

	Protocol::BattleTileInfo delta;
	delta.mutable_axial()->CopyFrom(*request.targetAxial);
	delta.set_tile_type(request.getBaseTileType(*request.targetAxial));
	delta.set_overlay_type(request.getTileOverlayType(*request.targetAxial));
	result.tileDeltas.push_back(delta);
	cout << "BATTLE_EQUIPMENT_PICKUP pawn_id=" << request.caster.pawnId << " equipment_key=" << equipmentKey
		<< " q=" << request.targetAxial->q() << " r=" << request.targetAxial->r() << endl;
}

void BattleEffectExecutor::ExecuteSwapPosition(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	if (request.caster.axial == nullptr || request.target.axial == nullptr || request.hasTargetPawn == false)
		return;
	if (request.caster.pawnId == request.target.pawnId)
		return;
	Protocol::AxialCoord originalCasterAxial = *request.caster.axial;
	request.caster.axial->CopyFrom(*request.target.axial);
	request.target.axial->CopyFrom(originalCasterAxial);
	cout << "BATTLE_POSITION_SWAP caster_pawn_id=" << request.caster.pawnId
		<< " target_pawn_id=" << request.target.pawnId << endl;
}

void BattleEffectExecutor::ExecuteAddStatFromStat(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request)
{
	BattleEffectPawnContext target = SelectTarget(effect, request);
	if (target.statBonuses == nullptr || request.casterTemplate == nullptr)
		return;
	const string sourceStat = GetParam(effect, "source_stat");
	const string targetStat = GetParam(effect, "target_stat");
	if (sourceStat.empty() || targetStat.empty())
		return;
	const int32 sourceValue = GetStatValue(*request.casterTemplate, &request.caster, sourceStat);
	BattlePawn* targetPawn = (effect.effectTarget == "CASTER" || effect.effectTarget == "SELF") ? request.casterPawn : request.targetPawn;
	if (targetPawn == nullptr || targetPawn->ApplyStatFromStat(ToUpperString(sourceStat), ToUpperString(targetStat), sourceValue) == false)
		return;
	cout << "BATTLE_STAT_ADD_FROM_STAT pawn_id=" << target.pawnId << " source_stat=" << sourceStat
		<< " target_stat=" << targetStat << " amount=" << sourceValue << endl;
}

int32 BattleEffectExecutor::CalculateValue(const BattleEffectTemplate& effect, const BattlePawnClassTemplate& casterTemplate,
	const BattleEffectPawnContext& caster)
{
	const int32 baseValue = GetIntParam(effect, "base_value", 0);
	const string scalingStat = GetParam(effect, "scaling_stat");
	const double coefficient = GetDoubleParam(effect, "coefficient", 0.0);
	const int32 scalingValue = GetStatValue(casterTemplate, &caster, scalingStat);
	return baseValue + static_cast<int32>(floor(static_cast<double>(scalingValue) * coefficient));
}

int32 BattleEffectExecutor::GetStatValue(const BattlePawnClassTemplate& pawnTemplate, const BattleEffectPawnContext* pawn,
	const string& statKey) const
{
	const string normalized = ToUpperString(statKey);
	int32 baseValue = 0;
	if (normalized == "STR")
		baseValue = pawnTemplate.baseStr;
	else if (normalized == "CON")
		baseValue = pawnTemplate.baseCon;
	else if (normalized == "DEX")
		baseValue = pawnTemplate.baseDex;
	else if (normalized == "SPELL")
		baseValue = pawnTemplate.baseSpell;
	else if (normalized == "DEFENSE")
		baseValue = pawnTemplate.baseDefense;
	else if (normalized == "FOCUS")
		baseValue = pawnTemplate.baseFocus;
	else if (normalized == "WILL")
		baseValue = pawnTemplate.baseWill;
	if (pawn != nullptr && pawn->statBonuses != nullptr)
	{
		auto bonusIt = pawn->statBonuses->find(normalized);
		if (bonusIt != pawn->statBonuses->end())
			baseValue += bonusIt->second;
	}
	return baseValue;
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
