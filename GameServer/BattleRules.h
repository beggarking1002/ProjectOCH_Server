#pragma once

// Shared combat-system rules. Character and skill balance remains in CSV.
namespace BattleRules
{
	constexpr const char* DizzyStatusKey = "DIZZY";
	constexpr const char* StunStatusKey = "STUN";

	constexpr int MoraleMaxFromWillMultiplier = 10;
	constexpr double MoraleStageRatio = 0.20;
}
