#pragma once

// Shared combat-system rules. Character and skill balance remains in CSV.
namespace BattleRules
{
	constexpr const char* DizzyStatusKey = "DIZZY";
	constexpr const char* DizzyResolvedStatusKey = "DIZZY_RESOLVED";
	constexpr const char* StunStatusKey = "STUN";

	constexpr int DizzyMaxStacks = 4;
	constexpr int DizzyResistBasePercent = 10;
	constexpr double DizzyResistWillMultiplier = 2.0;
	constexpr double DizzyResistMoraleRatioMultiplier = 15.0;

	constexpr int MoraleMaxFromWillMultiplier = 10;
	constexpr double MoraleStageRatio = 0.20;
}
