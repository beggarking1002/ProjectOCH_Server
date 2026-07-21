#include "pch.h"
#include "BattlePawn.h"
#include "BeigeFire.h"
#include "BeigeIce.h"
#include "SuenAxe.h"

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
bool BattlePawn::TryConsumeGuaranteedEvade() { return false; }
bool BattlePawn::CanDropEquipment(const string& equipmentKey) const { return false; }
bool BattlePawn::CanPickupEquipment(const string& equipmentKey, uint64 equipmentOwnerPawnId) const { return false; }
bool BattlePawn::CanActivateSkill(int32 skillSlot, string& reason) const { return true; }
void BattlePawn::OnEquipmentPickedUp(const string& equipmentKey) { }
bool BattlePawn::ApplyStatFromStat(const string& sourceStat, const string& targetStat, int32 value) { return false; }

bool BattlePawn::CanMove() const
{
	return isDead == false && hp > 0 && hasMovedThisTurn == false;
}

void BattlePawn::MarkMoved()
{
	hasMovedThisTurn = true;
}

bool BattlePawn::CanUseSkillSlot(int32 skillSlot, string& reason) const
{
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
		currentAp = 0;
	}
	else if (IsUltimateSkillSlot(skillSlot))
		usedUltimate = true;
	else if (IsSubActionSkillSlot(skillSlot))
		usedSubActionThisTurn = true;
}

void BattlePawn::ResetTurnActionUsage()
{
	// currentAp is kept for legacy client/UI compatibility.  The authoritative
	// action rule is the per-slot usage state below: one normal skill per turn.
	currentAp = 2;
	hasMovedThisTurn = false;
	usedNormalSkillThisTurn = false;
	usedSubActionThisTurn = false;
}

void BattlePawn::InitializeBattleActionUsage()
{
	currentAp = 0;
	hasMovedThisTurn = false;
	usedNormalSkillThisTurn = false;
	usedSubActionThisTurn = false;
	usedUltimate = false;
}

void BattlePawn::MarkDefeated()
{
	hp = 0;
	currentAp = 0;
	hasMovedThisTurn = true;
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
	int32 value = maxArmor;
	for (const BattleBarrierState& barrier : barriers)
		value += barrier.maxValue;
	return value;
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

	return make_shared<BattlePawn>();
}
