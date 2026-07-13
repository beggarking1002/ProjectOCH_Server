#pragma once
#include "BattleTemplateManager.h"

struct BattleEffectPawnContext
{
	uint64 pawnId = 0;
	uint64 ownerId = 0;
	Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
	Protocol::AxialCoord* axial = nullptr;
	int32* hp = nullptr;
	int32* armor = nullptr;
	unordered_map<string, int32>* resources = nullptr;
	unordered_map<string, int32>* maxResources = nullptr;
};

struct BattleEffectExecutionRequest
{
	const BattleSkillTemplate* skill = nullptr;
	const BattlePawnClassTemplate* casterTemplate = nullptr;
	int32 skillSlot = 0;
	string actionType;
	bool isBackAttack = false;
	BattleEffectPawnContext caster;
	BattleEffectPawnContext target;
	vector<Protocol::BattleActionLog>* logs = nullptr;
};

struct BattleEffectExecutionResult
{
	int32 totalDamage = 0;
	bool dealtDamage = false;
};

class BattleEffectExecutor
{
public:
	BattleEffectExecutionResult ExecuteOnCast(const BattleEffectExecutionRequest& request);

private:
	void ExecuteDealDamage(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
		BattleEffectExecutionResult& result);
	void ExecuteModifyResource(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);

	int32 CalculateValue(const BattleEffectTemplate& effect, const BattlePawnClassTemplate& casterTemplate);
	int32 GetStatValue(const BattlePawnClassTemplate& pawnTemplate, const string& statKey) const;
	int32 ApplyDamage(BattleEffectPawnContext target, int32 damage);
	BattleEffectPawnContext SelectTarget(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	string GetParam(const BattleEffectTemplate& effect, const string& key, const string& fallback = "") const;
	int32 GetIntParam(const BattleEffectTemplate& effect, const string& key, int32 fallback = 0) const;
	double GetDoubleParam(const BattleEffectTemplate& effect, const string& key, double fallback = 0.0) const;
};
