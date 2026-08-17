#pragma once
#include "Creature.h"
#include "Pawn.h"

class GameSession;
class Room;
struct EconomyItemTemplate;

struct ExpeditionItemStackState
{
	uint64 stackId = 0;
	string itemId;
	int32 quantity = 0;
	int64 remainingShelfLifeMs = -1;
	uint64 acquiredSequence = 0;
};

struct PlayerVillageShopStockState
{
	string villageId;
	string itemId;
	int32 stock = 0;
	uint64 stockGeneration = 1;
};

enum class PlayerQuestStatus
{
	Available,
	Active,
	Ready,
	Completed,
	Abandoned,
};

struct PlayerQuestObjectiveState
{
	uint32 objectiveIndex = 0;
	string objectiveType;
	string targetVillageId;
	string targetItemId;
	int32 requiredCount = 1;
	int32 progress = 0;
};

struct PlayerQuestRewardState
{
	uint32 rewardIndex = 0;
	string rewardType;
	string targetId;
	int32 amount = 0;
};

struct PlayerQuestState
{
	string questId;
	string templateId;
	string displayName;
	string description;
	string category;
	string startVillageId;
	string completionVillageId;
	string prerequisiteTemplateId;
	int32 boardSlot = -1;
	PlayerQuestStatus status = PlayerQuestStatus::Available;
	uint64 acceptedAtMs = 0;
	uint64 readyAtMs = 0;
	uint64 completedAtMs = 0;
	vector<PlayerQuestObjectiveState> objectives;
	vector<PlayerQuestRewardState> rewards;
};

// Runtime state stays owned by Player. Storage implementations serialize this
// value object, keeping gameplay code independent from a database client.
struct PersistentPlayerEconomyState
{
	int32 gold = 0;
	int32 fame = 0;
	int32 satiety = 100;
	int32 maxSatiety = 100;
	int32 thirst = 100;
	int32 maxThirst = 100;
	int64 satietyDrainNumerator = 0;
	uint64 nextInventoryStackId = 1;
	uint64 nextAcquiredSequence = 1;
	vector<ExpeditionItemStackState> inventory;
	uint64 shopRestockElapsedMs = 0;
	uint64 shopStockGeneration = 1;
	vector<PlayerVillageShopStockState> shopStock;
	vector<PlayerQuestState> quests;
};

class Player : public Creature
{
public:
	Player();
	virtual ~Player();

public:
	weak_ptr<GameSession> session;
	vector<PawnRef> battlePawns;
	string activeVillageId;
	bool hasPersistentIdentity = false;

public:
	PawnRef AddBattlePawn(Protocol::PawnClass pawnClass, int32 level = 1);
	void SetBattlePawnClasses(const vector<Protocol::PawnClass>& pawnClasses, int32 level = 1);

	void InitializeEconomy(uint64 nowMs);
	void ResetEconomyProgress(uint64 nowMs);
	void RestoreEconomyState(const PersistentPlayerEconomyState& state, uint64 nowMs);
	PersistentPlayerEconomyState ExportEconomyState() const;
	bool NeedsEconomyPersistence(uint64 nowMs, uint64 minimumIntervalMs) const;
	void MarkEconomyPersisted(uint64 nowMs);
	void MarkEconomyDirty() { _economyDirty = true; }
	bool AdvanceEconomy(uint64 nowMs, vector<string>& autoConsumedItemIds, vector<string>& expiredItemIds);
	bool AddInventoryItem(const EconomyItemTemplate& item, int32 quantity, vector<string>& autoConsumedItemIds);
	const ExpeditionItemStackState* FindInventoryStack(uint64 stackId) const;
	bool RemoveInventoryItem(uint64 stackId, int32 quantity);
	int32 CountInventoryItem(const string& itemId) const;
	bool RemoveInventoryItemFefo(const string& itemId, int32 quantity);
	const PlayerQuestState* FindQuestState(const string& questId) const;
	PlayerQuestState* FindQuestState(const string& questId);
	bool AddQuestState(PlayerQuestState state);
	const vector<PlayerQuestState>& QuestStates() const { return _quests; }
	int32 GetVillageShopStock(const string& villageId, const string& itemId) const;
	bool SpendVillageShopStock(const string& villageId, const string& itemId, int32 quantity);
	uint32 GetShopRestockRemainingSeconds(uint64 intervalMs) const;
	bool SpendGold(int32 amount);
	void AddGold(int32 amount);
	void ModifyFame(int32 amount);
	int32 Gold() const { return _gold; }
	int32 Fame() const { return _fame; }
	int32 Satiety() const { return _satiety; }
	int32 MaxSatiety() const { return _maxSatiety; }
	int32 Thirst() const { return _thirst; }
	int32 MaxThirst() const { return _maxThirst; }
	const vector<ExpeditionItemStackState>& Inventory() const { return _inventory; }
	mutex& EconomyMutex() const { return _economyMutex; }
	void FillExpeditionState(Protocol::S_EXPEDITION_STATE& packet,
		const vector<string>& autoConsumedItemIds = {}, const vector<string>& expiredItemIds = {}) const;

private:
	bool TryAutoConsume(vector<string>& autoConsumedItemIds);
	int64 GetShelfLifeMs(const EconomyItemTemplate& item) const;
	void ResetVillageShopStock();
	void AdvanceShopRestock(uint64 elapsedMs);

private:
	bool _economyInitialized = false;
	int32 _gold = 0;
	int32 _fame = 0;
	int32 _satiety = 100;
	int32 _maxSatiety = 100;
	int32 _thirst = 100;
	int32 _maxThirst = 100;
	uint64 _lastEconomyTickMs = 0;
	int64 _satietyDrainNumerator = 0;
	uint64 _nextInventoryStackId = 1;
	uint64 _nextAcquiredSequence = 1;
	vector<ExpeditionItemStackState> _inventory;
	uint64 _shopRestockElapsedMs = 0;
	uint64 _shopStockGeneration = 1;
	unordered_map<string, unordered_map<string, int32>> _shopStockByVillageId;
	vector<PlayerQuestState> _quests;
	bool _economyDirty = false;
	uint64 _lastEconomyPersistedMs = 0;
	mutable mutex _economyMutex;
};

