#pragma once
#include "BattleTemplateManager.h"

class BattlePawn;

struct BattleBarrierState
{
	uint64 barrierId = 0;
	string sourceSkillKey;
	int32 value = 0;
	int32 maxValue = 0;
	int32 remainingOwnerTurns = 0;
};

struct BattleStatusState
{
	int32 stacks = 0;
	int32 remainingOwnerTurns = -1;
	int32 chargesPerOwnerTurn = 0;
	string consumeOn;
};

struct BattleAuraState
{
	string sourceSkillKey;
	int32 baseRadius = 0;
	int32 radius = 0;
};

struct BattleZocModifierState
{
	string sourceStatusKey;
	bool enableZoc = false;
	int32 rangeDelta = 0;
	int32 reactionLimitDelta = 0;
	int32 reactionSkillSlotOverride = 0;
	unordered_set<string> addTriggers;
};

struct BattleEffectPawnContext
{
	uint64 pawnId = 0;
	uint64 ownerId = 0;
	Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
	Protocol::AxialCoord* axial = nullptr;
	int32* hp = nullptr;
	int32* maxHp = nullptr;
	int32* armor = nullptr;
	unordered_map<Protocol::BattleResourceType, int32>* resources = nullptr;
	unordered_map<Protocol::BattleResourceType, int32>* maxResources = nullptr;
	vector<BattleBarrierState>* barriers = nullptr;
	unordered_map<string, BattleStatusState>* statuses = nullptr;
	unordered_map<string, int32>* statBonuses = nullptr;
	unordered_map<string, BattleAuraState>* auras = nullptr;
	vector<BattleZocModifierState>* zocModifiers = nullptr;
};

struct BattleEffectExecutionRequest
{
	const BattleSkillTemplate* skill = nullptr;
	const BattlePawnClassTemplate* casterTemplate = nullptr;
	int32 skillSlot = 0;
	string actionType;
	bool isBackAttack = false;
	bool isGuarded = false;
	bool isCounter = false;
	double damageMultiplier = 1.0;
	int32 auraRadiusBonus = 0;
	bool hasTargetPawn = false;
	bool isEvaded = false;
	bool isAreaDamage = false;
	double targetDamageMultiplier = 1.0;
	const Protocol::AxialCoord* targetAxial = nullptr;
	function<Protocol::BattleTileType(const Protocol::AxialCoord&)> getBaseTileType;
	function<Protocol::BattleTileOverlayType(const Protocol::AxialCoord&)> getTileOverlayType;
	function<void(const Protocol::AxialCoord&, Protocol::BattleTileOverlayType)> setTileOverlayType;
	function<string(const Protocol::AxialCoord&)> getTileEquipmentKey;
	function<uint64(const Protocol::AxialCoord&)> getTileEquipmentOwnerPawnId;
	function<void(const Protocol::AxialCoord&, const string&, uint64)> setTileEquipment;
	function<bool(const Protocol::AxialCoord&)> isTileValid;
	BattleEffectPawnContext caster;
	BattleEffectPawnContext target;
	BattlePawn* casterPawn = nullptr;
	BattlePawn* targetPawn = nullptr;
	uint64* barrierIdGenerator = nullptr;
	vector<Protocol::BattleActionLog>* logs = nullptr;
};

struct BattleEffectExecutionResult
{
	int32 totalDamage = 0;
	bool dealtDamage = false;
	vector<Protocol::BattleTileInfo> tileDeltas;
};

enum class BattleEffectTargetScope
{
	All,
	CasterOnly,
	TargetOnly,
};

class BattleEffectExecutor
{
public:
	BattleEffectExecutionResult ExecuteOnCast(const BattleEffectExecutionRequest& request);
	BattleEffectExecutionResult ExecuteTrigger(const BattleEffectExecutionRequest& request, const string& trigger,
		BattleEffectTargetScope scope = BattleEffectTargetScope::All);
	void AdvanceOwnerTurn(BattleEffectPawnContext pawn);

private:
	void ExecuteDealDamage(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
		BattleEffectExecutionResult& result);
	void ExecuteRestoreHp(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteModifyResource(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteSetResourceMax(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteApplyBarrier(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteApplyStatus(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteApplyDizzy(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteToggleAura(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteChangeTileOverlay(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
		BattleEffectExecutionResult& result);
	void ExecuteTeleportToOverlay(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteDropEquipment(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
		BattleEffectExecutionResult& result);
	void ExecutePickupEquipment(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request,
		BattleEffectExecutionResult& result);
	void ExecuteSwapPosition(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteAddStatFromStat(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);
	void ExecuteApplyZocModifier(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request);

	int32 CalculateValue(const BattleEffectTemplate& effect, const BattlePawnClassTemplate& casterTemplate,
		const BattleEffectPawnContext& caster);
	int32 GetStatValue(const BattlePawnClassTemplate& pawnTemplate, const BattleEffectPawnContext* pawn, const string& statKey) const;
	int32 ApplyDamage(BattleEffectPawnContext target, int32 damage);
	BattleEffectPawnContext SelectTarget(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request) const;
	bool IsConditionMet(const BattleEffectTemplate& effect, const BattleEffectExecutionRequest& request) const;
	string GetParam(const BattleEffectTemplate& effect, const string& key, const string& fallback = "") const;
	int32 GetIntParam(const BattleEffectTemplate& effect, const string& key, int32 fallback = 0) const;
	double GetDoubleParam(const BattleEffectTemplate& effect, const string& key, double fallback = 0.0) const;
	bool RollDizzyResistance(const BattleEffectPawnContext& target) const;
};
