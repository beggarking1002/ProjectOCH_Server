#include "pch.h"
#include "BattleSkillExecutionService.h"

BattleSkillActionResult BattleSkillExecutionService::Execute(const BattleSkillActionRequest& request) const
{
	BattleSkillActionResult result;
	if (request.skill == nullptr || request.casterTemplate == nullptr || request.caster == nullptr || request.targetAxial == nullptr)
		return result;

	BattlePawn& caster = *request.caster;
	BattlePawn* target = request.target;
	const bool directEnemyTarget = target != nullptr && target->ownerId != caster.ownerId &&
		(request.skill->targetType == "ENEMY_SINGLE" || request.skill->targetType == "TILE_OR_ENEMY");
	if (target != nullptr)
	{
		result.affectedTargets.push_back(target);
		result.deathCandidates.push_back(target);
	}

	BattleEffectExecutionRequest effectRequest;
	effectRequest.skill = request.skill;
	effectRequest.casterTemplate = request.casterTemplate;
	effectRequest.skillSlot = request.skillSlot;
	effectRequest.actionType = request.actionType.empty() ? (request.isUltimate ? "ultimate" : "skill") : request.actionType;
	effectRequest.isBackAttack = request.isBackAttack;
	effectRequest.isGuarded = request.isGuarded;
	effectRequest.isCounter = request.isCounter;
	effectRequest.damageMultiplier = _skillResolver.GetDamageMultiplier(caster, request.skill->skillKey);
	effectRequest.auraRadiusBonus = _skillResolver.GetAuraRadiusBonus(caster, request.skill->skillKey);
	effectRequest.hasTargetPawn = target != nullptr;
	effectRequest.isEvaded = directEnemyTarget && request.shouldEvadeTarget && request.shouldEvadeTarget(caster, *target);
	effectRequest.isAreaDamage = request.skill->targetShape.empty() == false;
	effectRequest.targetDamageMultiplier = target != nullptr ? _skillResolver.GetStatModifierMultiplier(*target, "DAMAGE_TAKEN",
		effectRequest.isAreaDamage ? "AREA_AND_DOT" : "") : 1.0;
	effectRequest.targetAxial = request.targetAxial;
	effectRequest.getBaseTileType = request.getBaseTileType;
	effectRequest.getTileOverlayType = request.getTileOverlayType;
	effectRequest.setTileOverlayType = request.setTileOverlayType;
	effectRequest.getTileEquipmentKey = request.getTileEquipmentKey;
	effectRequest.getTileEquipmentOwnerPawnId = request.getTileEquipmentOwnerPawnId;
	effectRequest.setTileEquipment = request.setTileEquipment;
	effectRequest.isTileValid = request.isTileValid;
	effectRequest.logs = &result.logs;
	effectRequest.caster = MakeEffectContext(caster);
	effectRequest.target = MakeEffectContext(target != nullptr ? *target : caster);
	effectRequest.casterPawn = &caster;
	effectRequest.targetPawn = target;
	effectRequest.barrierIdGenerator = request.barrierIdGenerator;

	BattleEffectExecutor executor;
	BattleEffectExecutionResult effectResult = executor.ExecuteOnCast(effectRequest);
	auto executeOnKill = [&executor, &effectRequest, &effectResult, this](BattlePawn* defeatedPawn)
		{
			if (defeatedPawn == nullptr || defeatedPawn->hp > 0)
				return;

			effectRequest.target = MakeEffectContext(*defeatedPawn);
			effectRequest.targetPawn = defeatedPawn;
			effectRequest.hasTargetPawn = true;
			AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest, "ON_KILL_DEALT", BattleEffectTargetScope::CasterOnly));
		};

	if (target == nullptr)
	{
		AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest, "ON_EMPTY_TILE_CAST"));
	}
	else if (effectRequest.isEvaded == false && request.skill->targetType == "TILE_OR_ENEMY" && request.skill->targetShape.empty())
	{
		AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest, "ON_HIT_DEALT"));
	}
	executeOnKill(target);

	const int32 extraTargets = _skillResolver.GetExtraTargets(caster, request.skill->skillKey, "EXTRA_TARGET_COUNT");
	if (extraTargets > 0 && target != nullptr && request.findAdjacentAliveAlly)
	{
		BattlePawn* extraTarget = request.findAdjacentAliveAlly(*target, target->pawnId);
		if (extraTarget != nullptr)
		{
			effectRequest.target = MakeEffectContext(*extraTarget);
			effectRequest.targetPawn = extraTarget;
			effectRequest.isEvaded = false;
			effectRequest.targetDamageMultiplier = _skillResolver.GetStatModifierMultiplier(*extraTarget, "DAMAGE_TAKEN", "");
			AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest, "ON_CAST", BattleEffectTargetScope::TargetOnly));
			result.extraChangedPawns.push_back(extraTarget);
		}
	}

	string areaShape = _skillResolver.GetAreaShape(caster, request.skill->skillKey);
	if (areaShape.empty())
		areaShape = request.skill->targetShape;
	if (areaShape.empty() == false && request.findAlivePawnAt && request.isTileValid)
	{
		const vector<Protocol::AxialCoord> area = caster.ResolveTargetArea(areaShape, *request.targetAxial, request.areaDirectionAxial);
		for (size_t i = 1; i < area.size(); i++)
		{
			const Protocol::AxialCoord& areaAxial = area[i];
			if (request.isTileValid(areaAxial) == false)
				continue;

			BattlePawn* areaTarget = request.findAlivePawnAt(areaAxial);
			if (areaTarget != nullptr && areaTarget->ownerId == caster.ownerId)
				continue;

			effectRequest.targetAxial = &areaAxial;
			effectRequest.target = MakeEffectContext(areaTarget != nullptr ? *areaTarget : caster);
			effectRequest.targetPawn = areaTarget;
			effectRequest.isBackAttack = false;
			effectRequest.hasTargetPawn = areaTarget != nullptr;
			effectRequest.isAreaDamage = true;
			effectRequest.isEvaded = areaTarget != nullptr && request.shouldEvadeTarget && request.shouldEvadeTarget(caster, *areaTarget);
			effectRequest.targetDamageMultiplier = areaTarget != nullptr ? _skillResolver.GetStatModifierMultiplier(*areaTarget, "DAMAGE_TAKEN", "AREA_AND_DOT") : 1.0;
			if (areaTarget == nullptr)
				AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest, "ON_EMPTY_TILE_CAST"));
			else if (effectRequest.isEvaded == false)
				AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest, "ON_HIT_DEALT"));

			if (areaTarget != nullptr)
			{
				result.affectedTargets.push_back(areaTarget);
				executeOnKill(areaTarget);
				result.extraChangedPawns.push_back(areaTarget);
				result.deathCandidates.push_back(areaTarget);
			}
		}
	}

	if (request.isUltimate)
		_skillResolver.RefreshAuraRadii(caster);

	result.appliedDamage = effectResult.totalDamage;
	result.tileDeltas = move(effectResult.tileDeltas);
	return result;
}

BattleEffectPawnContext BattleSkillExecutionService::MakeEffectContext(BattlePawn& pawn) const
{
	BattleEffectPawnContext context;
	context.pawnId = pawn.pawnId;
	context.ownerId = pawn.ownerId;
	context.pawnClass = pawn.pawnClass;
	context.axial = &pawn.axial;
	context.hp = &pawn.hp;
	context.maxHp = &pawn.maxHp;
	context.armor = &pawn.armor;
	context.resources = &pawn.resources;
	context.maxResources = &pawn.maxResources;
	context.barriers = &pawn.barriers;
	context.statuses = &pawn.statuses;
	context.statBonuses = &pawn.statBonuses;
	context.auras = &pawn.auras;
	context.zocModifiers = &pawn.zocModifiers;
	return context;
}

void BattleSkillExecutionService::AppendEffectResult(BattleEffectExecutionResult& destination,
	const BattleEffectExecutionResult& additional) const
{
	destination.totalDamage += additional.totalDamage;
	destination.dealtDamage = destination.dealtDamage || additional.dealtDamage;
	for (const Protocol::BattleTileInfo& tileDelta : additional.tileDeltas)
		destination.tileDeltas.push_back(tileDelta);
}
