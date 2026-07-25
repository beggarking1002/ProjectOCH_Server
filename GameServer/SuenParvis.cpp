#include "pch.h"
#include "SuenParvis.h"

namespace { constexpr const char* kParvisOff = "SUEN_PARVIS_OFF"; constexpr const char* kParvis = "PARVIS"; }

string SuenParvis::ResolveSkillKey(int32 slot) const
{
	const bool off = HasActiveStatus(kParvisOff);
	if (slot == 2) return off ? "SUEN_PARVIS_STAND_SHOT_OFF" : "SUEN_PARVIS_INSTALL";
	if (slot == 3) return off ? "SUEN_PARVIS_SIT_SHOT" : "SUEN_PARVIS_STAND_SHOT_ON";
	if (slot == 4) return "SUEN_PARVIS_POINT_BLANK";
	if (slot == 5) return off ? "SUEN_PARVIS_ROLL_SHOT" : "SUEN_PARVIS_YABAWI";
	if (slot == 6) return "SUEN_PARVIS_DARK_HAND";
	if (slot == 7) return "SUEN_PARVIS_PICKUP";
	return "";
}
bool SuenParvis::CanDropEquipment(const string& key) const { return key == kParvis && !HasActiveStatus(kParvisOff); }
bool SuenParvis::CanPickupEquipment(const string& key, uint64 owner) const { return key == kParvis && owner == pawnId; }
bool SuenParvis::CanActivateSkill(int32 slot, string& reason) const { if (slot != 7 || HasActiveStatus(kParvisOff)) return true; reason = "parvis pickup requires ParvisOff"; return false; }
void SuenParvis::OnEquipmentPickedUp(const string& key) { if (key == kParvis) statuses.erase(kParvisOff); }
bool SuenParvis::ApplyStatFromStat(const string& source, const string& target, int32 value) { if (source != "STR" || target != "DEX") return false; statBonuses["DEX"] += value; return true; }
bool SuenParvis::BlocksMoveAfterSkill(int32 slot) const { return (slot == 3 && HasActiveStatus(kParvisOff)) || slot == 6; }
int32 SuenParvis::GetHitRateBonus(const BattleSkillTemplate& skill) const
{
	if (skill.skillKey == "SUEN_PARVIS_STAND_SHOT_OFF" || skill.skillKey == "SUEN_PARVIS_STAND_SHOT_ON")
		return -25;
	if (skill.skillKey == "SUEN_PARVIS_SIT_SHOT")
		return 25;
	return 0;
}
bool SuenParvis::IsGuaranteedHit(const BattleSkillTemplate& skill) const { return skill.skillKey == "SUEN_PARVIS_DARK_HAND"; }
bool SuenParvis::IsGuaranteedCritical(const BattleSkillTemplate& skill) const { return skill.skillKey == "SUEN_PARVIS_DARK_HAND"; }
