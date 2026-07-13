#pragma once
#include "BattleTemplateManager.h"

struct BattleBarrierState
{
	uint64 barrierId = 0;
	string sourceSkillKey;
	int32 value = 0;
	int32 remainingOwnerTurns = 0;
};

struct BattleStatusState
{
	int32 stacks = 0;
	int32 remainingOwnerTurns = -1;
};

struct BattleEffectPawnContext
{
	uint64 pawnId = 0;
	uint64 ownerId = 0;
	Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
	Protocol::AxialCoord* axial = nullptr;
	int32* hp = nullptr;
	int32* armor = nullptr;
	unordered_map<Protocol::BattleResourceType, int32>* resources = nullptr;
	unordered_map<Protocol::BattleResourceType, int32>* maxResources = nullptr;
	vector<BattleBarrierState>* barriers = nullptr;
	unordered_map<string, BattleStatusState>* statuses = nullptr;
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
	uint64* barrierIdGenerator = nullptr;
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
	BattleEffectExecutionResult ExecuteTrigger(const BattleEffectExecutionRequest& request, const string& trigger);
	void AdvanceOwnerTurn(BattleEffectPawnContext pawn);

private:
	void ExecuteDealDamage(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
		BattleEffectExecutionResult& result);
	void ExecuteModifyResource(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteSetResourceMax(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteApplyBarrier(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteApplyStatus(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);

	int32 CalculateValue(const BattleEffectTemplate& effect, const BattlePawnClassTemplate& casterTemplate);
	int32 GetStatValue(const BattlePawnClassTemplate& pawnTemplate, const string& statKey) const;
	int32 ApplyDamage(BattleEffectPawnContext target, int32 damage);
	BattleEffectPawnContext SelectTarget(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request) const;
	bool IsConditionMet(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request) const;
	string GetParam(const BattleEffectTemplate& effect, const string& key, const string& fallback = "") const;
	int32 GetIntParam(const BattleEffectTemplate& effect, const string& key, int32 fallback = 0) const;
	double GetDoubleParam(const BattleEffectTemplate& effect, const string& key, double fallback = 0.0) const;
};
