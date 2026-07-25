#pragma once

#include "Suen.h"

class SuenParvis final : public Suen
{
public:
	const char* GetBehaviorKey() const override { return "SUEN_PARVIS"; }
	string ResolveSkillKey(int32 skillSlot) const override;
	bool CanDropEquipment(const string& equipmentKey) const override;
	bool CanPickupEquipment(const string& equipmentKey, uint64 equipmentOwnerPawnId) const override;
	bool CanActivateSkill(int32 skillSlot, string& reason) const override;
	void OnEquipmentPickedUp(const string& equipmentKey) override;
	bool ApplyStatFromStat(const string& sourceStat, const string& targetStat, int32 value) override;
	bool BlocksMoveAfterSkill(int32 skillSlot) const override;
	int32 GetHitRateBonus(const BattleSkillTemplate& skill) const override;
	bool IsGuaranteedHit(const BattleSkillTemplate& skill) const override;
	bool IsGuaranteedCritical(const BattleSkillTemplate& skill) const override;
};
