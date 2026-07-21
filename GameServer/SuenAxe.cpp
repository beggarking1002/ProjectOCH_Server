#include "pch.h"
#include "SuenAxe.h"

namespace
{
	constexpr const char* kInterceptGuardStatusKey = "SUEN_AXE_LAST_MAN_INTERCEPT_GUARD";
	constexpr const char* kFirstHitEvadeStatusKey = "SUEN_AXE_LAST_MAN_FIRST_HIT_EVADE";
	constexpr const char* kAxeEquipmentKey = "AXE";
	constexpr const char* kAxeOffStatusKey = "SUEN_AXE_AXE_OFF";
	constexpr const char* kAxeOffEvasionStatusKey = "SUEN_AXE_AXE_OFF_EVASION";
}

vector<Protocol::AxialCoord> SuenAxe::ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target,
	const Protocol::AxialCoord* directionTarget) const
{
	if (shape != "ADJACENT_6")
		return BattlePawn::ResolveTargetArea(shape, target, directionTarget);

	static constexpr int32 kDirections[6][2] =
	{
		{ 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, 0 }, { -1, 1 }, { 0, 1 }
	};

	vector<Protocol::AxialCoord> area{ axial };
	for (const auto& direction : kDirections)
	{
		Protocol::AxialCoord neighbor;
		neighbor.set_q(axial.q() + direction[0]);
		neighbor.set_r(axial.r() + direction[1]);
		area.push_back(neighbor);
	}
	return area;
}

bool SuenAxe::CanInterceptSingleTargetAttack() const
{
	return HasActiveStatus(kInterceptGuardStatusKey);
}

bool SuenAxe::TryConsumeGuaranteedEvade()
{
	auto statusIt = statuses.find(kFirstHitEvadeStatusKey);
	if (statusIt == statuses.end() || statusIt->second.remainingOwnerTurns == 0 || statusIt->second.stacks <= 0)
		return false;

	statusIt->second.stacks--;
	cout << "BATTLE_GUARANTEED_EVADE pawn_id=" << pawnId
		<< " status_key=" << kFirstHitEvadeStatusKey << endl;
	return true;
}

bool SuenAxe::CanDropEquipment(const string& equipmentKey) const
{
	return equipmentKey == kAxeEquipmentKey;
}

bool SuenAxe::CanPickupEquipment(const string& equipmentKey, uint64 equipmentOwnerPawnId) const
{
	return equipmentKey == kAxeEquipmentKey && equipmentOwnerPawnId == pawnId;
}

bool SuenAxe::CanActivateSkill(int32 skillSlot, string& reason) const
{
	if (skillSlot != 7 || HasActiveStatus(kAxeOffStatusKey))
		return true;

	reason = "axe pickup requires AxeOff";
	return false;
}

void SuenAxe::OnEquipmentPickedUp(const string& equipmentKey)
{
	if (equipmentKey != kAxeEquipmentKey)
		return;
	statuses.erase(kAxeOffStatusKey);
	statuses.erase(kAxeOffEvasionStatusKey);
}

bool SuenAxe::ApplyStatFromStat(const string& sourceStat, const string& targetStat, int32 value)
{
	if (sourceStat != "STR" || targetStat != "DEX")
		return false;
	statBonuses["DEX"] += value;
	return true;
}
