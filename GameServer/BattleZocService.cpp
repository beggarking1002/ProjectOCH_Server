#include "pch.h"
#include "BattleZocService.h"
#include "BattlePawn.h"
#include "BattleTemplateManager.h"

BattleZocProfile BattleZocService::GetProfile(const BattlePawn& pawn) const
{
	BattleZocProfile profile;
	const BattleZocTemplate* templateProfile = GBattleTemplates.GetZocTemplate(pawn.pawnClass);
	if (templateProfile != nullptr)
	{
		profile.enabled = templateProfile->enabled;
		profile.range = templateProfile->range;
		profile.frontArcWidth = templateProfile->frontArcWidth;
		profile.reactionLimitPerTurn = templateProfile->reactionLimitPerTurn;
		profile.reactionSkillSlot = templateProfile->reactionSkillSlot;
		profile.triggers = templateProfile->triggers;
	}
	else if (HasMeleeReactionSkill(pawn))
	{
		profile.enabled = true;
		profile.triggers.insert("ENEMY_MOVE_IN_ZONE");
	}

	for (const BattleZocModifierState& modifier : pawn.zocModifiers)
	{
		if (modifier.sourceStatusKey.empty() == false)
		{
			auto statusIt = pawn.statuses.find(modifier.sourceStatusKey);
			if (statusIt == pawn.statuses.end() || statusIt->second.remainingOwnerTurns == 0)
				continue;
		}

		profile.enabled = profile.enabled || modifier.enableZoc;
		profile.range = max(0, profile.range + modifier.rangeDelta);
		profile.reactionLimitPerTurn = max(0, profile.reactionLimitPerTurn + modifier.reactionLimitDelta);
		if (modifier.reactionSkillSlotOverride > 0)
			profile.reactionSkillSlot = modifier.reactionSkillSlotOverride;
		for (const string& trigger : modifier.addTriggers)
			profile.triggers.insert(trigger);
	}

	if (HasMeleeReactionSkill(pawn) == false)
		profile.enabled = false;
	return profile;
}

bool BattleZocService::IsInsideZone(const BattlePawn& zocOwner, const BattleZocProfile& profile,
	const Protocol::AxialCoord& axial) const
{
	if (profile.enabled == false || profile.range <= 0 || _spatialService.AxialDistance(zocOwner.axial, axial) > profile.range)
		return false;

	const int32 facingIndex = static_cast<int32>(zocOwner.facingDirection) - 1;
	const int32 tileDirection = _spatialService.FindClosestDirectionIndex(zocOwner.axial, axial);
	if (facingIndex < 0 || facingIndex >= BattleSpatialService::DirectionCount || tileDirection < 0)
		return false;

	const int32 arcWidth = clamp(profile.frontArcWidth, 1, BattleSpatialService::DirectionCount);
	const int32 halfArc = (arcWidth - 1) / 2;
	for (int32 offset = -halfArc; offset <= halfArc; ++offset)
	{
		const int32 direction = (facingIndex + offset + BattleSpatialService::DirectionCount) % BattleSpatialService::DirectionCount;
		if (tileDirection == direction)
			return true;
	}
	return false;
}

bool BattleZocService::ShouldTriggerOnEnemyMoveInZone(const BattlePawn& zocOwner,
	const Protocol::AxialCoord& enemyStart) const
{
	const BattleZocProfile profile = GetProfile(zocOwner);
	return profile.SupportsTrigger("ENEMY_MOVE_IN_ZONE") && IsInsideZone(zocOwner, profile, enemyStart);
}

bool BattleZocService::ShouldTriggerOnAllyAttacked(const BattlePawn& zocOwner, const BattlePawn& attacker,
	const BattlePawn& attackedAlly) const
{
	const BattleZocProfile profile = GetProfile(zocOwner);
	return profile.SupportsTrigger("ALLY_ATTACKED_IN_ZONE") && zocOwner.pawnId != attackedAlly.pawnId &&
		zocOwner.ownerId == attackedAlly.ownerId && zocOwner.ownerId != attacker.ownerId &&
		zocOwner.isDead == false && zocOwner.hp > 0 && attacker.isDead == false && attacker.hp > 0 &&
		IsInsideZone(zocOwner, profile, attacker.axial);
}

bool BattleZocService::HasMeleeReactionSkill(const BattlePawn& pawn) const
{
	const BattleSkillTemplate* skill = GBattleTemplates.GetSkillByActionSlot(pawn.pawnClass, 2);
	return skill != nullptr && skill->skillCategory == "CAST" && skill->combatType == "MELEE" &&
		skill->targetType == "ENEMY_SINGLE";
}
