#pragma once

#include "Suen.h"

class SuenAxe final : public Suen
{
public:
	const char* GetBehaviorKey() const override { return "SUEN_AXE"; }
	vector<Protocol::AxialCoord> ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target,
		const Protocol::AxialCoord* directionTarget = nullptr) const override;
	bool CanInterceptSingleTargetAttack() const override;
	bool TryConsumeGuaranteedEvade() override;
	bool CanDropEquipment(const string& equipmentKey) const override;
	bool CanPickupEquipment(const string& equipmentKey, uint64 equipmentOwnerPawnId) const override;
	bool CanActivateSkill(int32 skillSlot, string& reason) const override;
	void OnEquipmentPickedUp(const string& equipmentKey) override;
	bool ApplyStatFromStat(const string& sourceStat, const string& targetStat, int32 value) override;
};
