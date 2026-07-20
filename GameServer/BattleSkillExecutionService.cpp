#include "pch.h"
#include "BattleSkillExecutionService.h"

BattleSkillActionResult BattleSkillExecutionService::Execute(const BattleSkillActionRequest& request) const
{
	BattleSkillActionResult result;
	if (request.skill == nullptr || request.casterTemplate == nullptr || request.caster == nullptr || request.targetAxial == nullptr)
		return result;

	BattlePawn& caster = *request.caster;
	BattlePawn* target = request.target;
	if (target != nullptr)
		result.deathCandidates.push_back(target);

	BattleEffectExecutionRequest effectRequest;
	effectRequest.skill = request.skill;
	effectRequest.casterTemplate = request.casterTemplate;
	effectRequest.skillSlot = request.skillSlot;
	effectRequest.actionType = request.isUltimate ? "ultimate" : "skill";
	effectRequest.isBackAttack = request.isBackAttack;
	effectRequest.damageMultiplier = _skillResolver.GetDamageMultiplier(caster, request.skill->skillKey);
	effectRequest.auraRadiusBonus = _skillResolver.GetAuraRadiusBonus(caster, request.skill->skillKey);
	effectRequest.hasTargetPawn = target != nullptr;
	effectRequest.targetAxial = request.targetAxial;
	effectRequest.getBaseTileType = request.getBaseTileType;
	effectRequest.getTileOverlayType = request.getTileOverlayType;
	effectRequest.setTileOverlayType = request.setTileOverlayType;
	effectRequest.isTileValid = request.isTileValid;
	effectRequest.logs = &result.logs;
	effectRequest.caster = MakeEffectContext(caster);
	effectRequest.target = MakeEffectContext(target != nullptr ? *target : caster);
	effectRequest.barrierIdGenerator = request.barrierIdGenerator;

	BattleEffectExecutor executor;
	BattleEffectExecutionResult effectResult = executor.ExecuteOnCast(effectRequest);

	if (target == nullptr)
	{
		AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest, "ON_EMPTY_TILE_CAST"));
	}
	else if (request.skill->targetType == "TILE_OR_ENEMY" && request.skill->targetShape.empty())
	{
		AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest, "ON_HIT_DEALT"));
	}

	const int32 extraTargets = _skillResolver.GetExtraTargets(caster, request.skill->skillKey, "EXTRA_TARGET_COUNT");
	if (extraTargets > 0 && target != nullptr && request.findAdjacentAliveAlly)
	{
		BattlePawn* extraTarget = request.findAdjacentAliveAlly(*target, target->pawnId);
		if (extraTarget != nullptr)
		{
			effectRequest.target = MakeEffectContext(*extraTarget);
			AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest, "ON_CAST", BattleEffectTargetScope::TargetOnly));
			result.extraChangedPawns.push_back(extraTarget);
		}
	}

	string areaShape = _skillResolver.GetAreaShape(caster, request.skill->skillKey);
	if (areaShape.empty())
		areaShape = request.skill->targetShape;
	if (areaShape.empty() == false && request.findAlivePawnAt && request.isTileValid)
	{
		const vector<Protocol::AxialCoord> area = caster.ResolveTargetArea(areaShape, *request.targetAxial);
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
			effectRequest.isBackAttack = false;
			effectRequest.hasTargetPawn = areaTarget != nullptr;
			AppendEffectResult(effectResult, executor.ExecuteTrigger(effectRequest,
				areaTarget != nullptr ? "ON_HIT_DEALT" : "ON_EMPTY_TILE_CAST"));

			if (areaTarget != nullptr)
			{
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
	context.armor = &pawn.armor;
	context.resources = &pawn.resources;
	context.maxResources = &pawn.maxResources;
	context.barriers = &pawn.barriers;
	context.statuses = &pawn.statuses;
	context.auras = &pawn.auras;
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
