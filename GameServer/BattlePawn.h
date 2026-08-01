#pragma once

#include "BattleEffectExecutor.h"

class BattlePawn
{
public:
	virtual ~BattlePawn() = default;
	static bool IsNormalSkillSlot(int32 skillSlot);
	static bool IsUltimateSkillSlot(int32 skillSlot);
	static bool IsSubActionSkillSlot(int32 skillSlot);

	virtual const char* GetBehaviorKey() const;
	virtual vector<Protocol::AxialCoord> ResolveTargetArea(const string& shape, const Protocol::AxialCoord& target,
		const Protocol::AxialCoord* directionTarget = nullptr) const;
	virtual bool CanInterceptSingleTargetAttack() const;
	virtual bool UsesConditionalCounterattack() const;
	virtual bool CanCounterattackOnSuccessfulHit() const;
	virtual bool TryConsumeGuaranteedEvade();
	virtual bool CanDropEquipment(const string& equipmentKey) const;
	virtual bool CanPickupEquipment(const string& equipmentKey, uint64 equipmentOwnerPawnId) const;
	virtual bool CanActivateSkill(int32 skillSlot, string& reason) const;
	virtual void OnEquipmentPickedUp(const string& equipmentKey);
	virtual bool ApplyStatFromStat(const string& sourceStat, const string& targetStat, int32 value);
	virtual void OnSuccessfulHitReceived(BattlePawn& attacker);
	virtual bool RequiresHitCheck(const BattleSkillTemplate& skill) const;
	virtual int32 GetHitRateBonus(const BattleSkillTemplate& skill) const;
	virtual bool IsGuaranteedHit(const BattleSkillTemplate& skill) const;
	virtual bool IsGuaranteedCritical(const BattleSkillTemplate& skill) const;
	virtual string ResolveSkillKey(int32 skillSlot) const;
	virtual bool BlocksMoveAfterSkill(int32 skillSlot) const;

	bool CanMove() const;
	// Applies a movement result that has already been validated by the board/movement services.
	void ApplyResolvedMove(const Protocol::AxialCoord& destination, Protocol::BattleFacingDirection facing);
	void MarkMoved();
	bool CanUseZocReaction(int32 reactionLimitPerTurn) const;
	void MarkZocReactionUsed();
	bool CanUseSkillSlot(int32 skillSlot, string& reason) const;
	void MarkSkillSlotUsed(int32 skillSlot);
	void ResetTurnActionUsage();
	void InitializeBattleActionUsage();
	void MarkDefeated();
	int32 GetShieldCurrent() const;
	int32 GetShieldMax() const;
	int32 GetEffectiveMaxArmor() const;

public:
	uint64 pawnId = 0;
	uint64 ownerId = 0;
	Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
	Protocol::AxialCoord axial;
	int32 hp = 0;
	int32 maxHp = 0;
	int32 moveRange = 0;
	int32 armor = 0;
	int32 maxArmor = 0;
	int32 currentAp = 0;
	bool hasMovedThisTurn = false;
	int32 zocReactionsUsedThisTurn = 0;
	bool usedNormalSkillThisTurn = false;
	bool usedSubActionThisTurn = false;
	bool usedUltimate = false;
	bool isActionBlockedThisTurn = false;
	bool isDead = false;
	Protocol::BattleFacingDirection facingDirection = Protocol::BATTLE_FACING_DIRECTION_Q_POS;
	Protocol::BattlePawnRole role = Protocol::BATTLE_PAWN_ROLE_NONE;
	unordered_map<Protocol::BattleResourceType, int32> resources;
	unordered_map<Protocol::BattleResourceType, int32> maxResources;
	vector<BattleBarrierState> barriers;
	unordered_map<string, BattleStatusState> statuses;
	unordered_map<string, int32> statBonuses;
	unordered_map<string, BattleAuraState> auras;
	vector<BattleZocModifierState> zocModifiers;
};

using BattlePawnRef = shared_ptr<BattlePawn>;

BattlePawnRef CreateBattlePawn(Protocol::PawnClass pawnClass);

struct BattlePawnInitialStats
{
	int32 hp = 0;
	int32 moveRange = 0;
	int32 maxArmor = 0;
	Protocol::BattlePawnRole role = Protocol::BATTLE_PAWN_ROLE_NONE;
};
