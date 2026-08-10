#include "pch.h"
#include "BattlePawn.h"
#include "BeigeFire.h"
#include "BeigeIce.h"
#include "AlenSpear.h"
#include "AlenShield.h"
#include "SuenAxe.h"
#include "SuenParvis.h"
#include "ZillianLongbow.h"
#include "ZillianMace.h"

bool BattlePawn::IsNormalSkillSlot(int32 skillSlot)
{
	return skillSlot >= 2 && skillSlot <= 5;
}

bool BattlePawn::IsUltimateSkillSlot(int32 skillSlot)
{
	return skillSlot == 6;
}

bool BattlePawn::IsSubActionSkillSlot(int32 skillSlot)
{
	return skillSlot == 7;
}

const char* BattlePawn::GetBehaviorKey() const { return "DEFAULT"; }
bool BattlePawn::CanInterceptSingleTargetAttack() const { return false; }
bool BattlePawn::UsesConditionalCounterattack() const { return false; }
bool BattlePawn::CanCounterattackOnSuccessfulHit() const { return false; }
bool BattlePawn::TryConsumeGuaranteedEvade() { return false; }
bool BattlePawn::CanDropEquipment(const string& equipmentKey) const { return false; }
bool BattlePawn::CanPickupEquipment(const string& equipmentKey, uint64 equipmentOwnerPawnId) const { return false; }
bool BattlePawn::CanActivateSkill(int32 skillSlot, string& reason) const { return true; }
void BattlePawn::OnEquipmentPickedUp(const string& equipmentKey) { }
bool BattlePawn::ApplyStatFromStat(const string& sourceStat, const string& targetStat, int32 value) { return false; }
void BattlePawn::OnSuccessfulHitReceived(BattlePawn& attacker) { }
bool BattlePawn::RequiresHitCheck(const BattleSkillTemplate& skill) const { return false; }
int32 BattlePawn::GetHitRateBonus(const BattleSkillTemplate& skill) const { return 0; }
bool BattlePawn::IsGuaranteedHit(const BattleSkillTemplate& skill) const { return false; }
bool BattlePawn::IsGuaranteedCritical(const BattleSkillTemplate& skill) const { return false; }
string BattlePawn::ResolveSkillKey(int32 skillSlot) const { return ""; }
bool BattlePawn::BlocksMoveAfterSkill(int32 skillSlot) const { return false; }

bool BattlePawn::CanMove() const
{
	return isDead == false && hp > 0 && isActionBlockedThisTurn == false && hasMovedThisTurn == false;
}

void BattlePawn::ApplyResolvedMove(const Protocol::AxialCoord& destination, Protocol::BattleFacingDirection facing)
{
	axial.CopyFrom(destination);
	facingDirection = facing;
	MarkMoved();
}

void BattlePawn::MarkMoved()
{
	hasMovedThisTurn = true;
}

bool BattlePawn::CanUseZocReaction(int32 reactionLimitPerTurn) const
{
	return isDead == false && hp > 0 && zocReactionsUsedThisTurn < reactionLimitPerTurn;
}

void BattlePawn::MarkZocReactionUsed()
{
	zocReactionsUsedThisTurn++;
}

bool BattlePawn::CanUseSkillSlot(int32 skillSlot, string& reason) const
{
	if (isActionBlockedThisTurn)
	{
		reason = "pawn is stunned";
		return false;
	}
	if (IsNormalSkillSlot(skillSlot) && usedNormalSkillThisTurn)
	{
		reason = "normal skill already used this turn";
		return false;
	}
	if (IsUltimateSkillSlot(skillSlot) && usedUltimate)
	{
		reason = "ultimate already used";
		return false;
	}
	if (IsSubActionSkillSlot(skillSlot) && usedSubActionThisTurn)
	{
		reason = "sub action already used this turn";
		return false;
	}
	return true;
}

void BattlePawn::MarkSkillSlotUsed(int32 skillSlot)
{
	if (IsNormalSkillSlot(skillSlot))
	{
		usedNormalSkillThisTurn = true;
	}
	else if (IsUltimateSkillSlot(skillSlot))
		usedUltimate = true;
	else if (IsSubActionSkillSlot(skillSlot))
		usedSubActionThisTurn = true;
}

void BattlePawn::ResetTurnActionUsage()
{
	hasMovedThisTurn = false;
	zocReactionsUsedThisTurn = 0;
	usedNormalSkillThisTurn = false;
	usedSubActionThisTurn = false;
	isActionBlockedThisTurn = false;
}

void BattlePawn::InitializeBattleActionUsage()
{
	hasMovedThisTurn = false;
	zocReactionsUsedThisTurn = 0;
	usedNormalSkillThisTurn = false;
	usedSubActionThisTurn = false;
	usedUltimate = false;
	isActionBlockedThisTurn = false;
}

void BattlePawn::MarkDefeated()
{
	hp = 0;
	hasMovedThisTurn = true;
	isActionBlockedThisTurn = true;
	isDead = true;
}

int32 BattlePawn::GetShieldCurrent() const
{
	int32 value = armor;
	for (const BattleBarrierState& barrier : barriers)
		value += barrier.value;
	return value;
}

int32 BattlePawn::GetShieldMax() const
{
	return max(0, maxHp);
}

int32 BattlePawn::GetEffectiveMaxArmor() const
{
	int32 value = maxArmor;
	for (const auto& item : statuses)
	{
		const BattleStatusState& status = item.second;
		if (status.remainingOwnerTurns != 0)
			value += status.flatArmorBonus;
	}
	return min(max(0, maxHp), max(0, value));
}

vector<Protocol::AxialCoord> BattlePawn::ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target,
	const Protocol::AxialCoord* /*directionTarget*/) const
{
	vector<Protocol::AxialCoord> area{ target };
	if (shape != "RADIUS_1")
		return area;

	static constexpr int32 kDirections[6][2] =
	{
		{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
	};
	for (const auto& direction : kDirections)
	{
		Protocol::AxialCoord neighbor;
		neighbor.set_q(target.q() + direction[0]);
		neighbor.set_r(target.r() + direction[1]);
		area.push_back(neighbor);
	}

	return area;
}

BattlePawnRef CreateBattlePawn(Protocol::PawnClass pawnClass)
{
	if (pawnClass == Protocol::PAWN_CLASS_BEIGE_FIRE)
		return make_shared<BeigeFire>();

	if (pawnClass == Protocol::PAWN_CLASS_BEIGE_ICE)
		return make_shared<BeigeIce>();

	if (pawnClass == Protocol::PAWN_CLASS_SUEN_AXE_SWORD)
		return make_shared<SuenAxe>();
	if (pawnClass == Protocol::PAWN_CLASS_SUEN_PARVIS)
		return make_shared<SuenParvis>();

	if (pawnClass == Protocol::PAWN_CLASS_ALEN_SPEAR)
		return make_shared<AlenSpear>();
	if (pawnClass == Protocol::PAWN_CLASS_ALEN_SWORD_SHIELD)
		return make_shared<AlenShield>();

	if (pawnClass == Protocol::PAWN_CLASS_ZILLIAN_LONGBOW)
		return make_shared<ZillianLongbow>();
	if (pawnClass == Protocol::PAWN_CLASS_ZILLIAN_MACE)
		return make_shared<ZillianMace>();

	return make_shared<BattlePawn>();
}
