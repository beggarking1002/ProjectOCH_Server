#include "pch.h"
#include "Player.h"
#include "EconomyDataManager.h"

#include <algorithm>
#include <limits>

Player::Player()
{
	_isPlayer = true;
	objectInfo->set_creature_type(Protocol::CreatureType::CREATURE_TYPE_PLAYER);
}

Player::~Player()
{

}

PawnRef Player::AddBattlePawn(Protocol::PawnClass pawnClass, int32 level)
{
	const uint64 ownerId = objectInfo != nullptr ? objectInfo->object_id() : 0;
	const uint64 pawnId = ownerId * 100 + static_cast<uint64>(battlePawns.size()) + 1;

	PawnRef pawn = make_shared<Pawn>(ownerId, pawnId, pawnClass, level);
	battlePawns.push_back(pawn);
	return pawn;
}

void Player::SetBattlePawnClasses(const vector<Protocol::PawnClass>& pawnClasses, int32 level)
{
	battlePawns.clear();
	for (Protocol::PawnClass pawnClass : pawnClasses)
		AddBattlePawn(pawnClass, level);
}

void Player::InitializeEconomy(uint64 nowMs)
{
	if (_economyInitialized)
		return;

	_maxSatiety = GEconomyData.GetConfigValue("expedition_satiety_max", 100);
	_satiety = _maxSatiety;
	_maxThirst = 100;
	_thirst = _maxThirst;
	_gold = GEconomyData.GetConfigValue("expedition_starting_gold", 0);
	_lastEconomyTickMs = nowMs;
	_satietyDrainNumerator = 0;
	_shopRestockElapsedMs = 0;
	_shopStockGeneration = 1;
	ResetVillageShopStock();
	_economyInitialized = true;
	_economyDirty = true;
}

void Player::ResetEconomyProgress(uint64 nowMs)
{
	_economyInitialized = false;
	_inventory.clear();
	_quests.clear();
	_nextInventoryStackId = 1;
	_nextAcquiredSequence = 1;
	_lastEconomyPersistedMs = 0;
	InitializeEconomy(nowMs);
}

void Player::RestoreEconomyState(const PersistentPlayerEconomyState& state, uint64 nowMs)
{
	_maxSatiety = (max)(1, state.maxSatiety);
	_satiety = (max)(0, (min)(state.satiety, _maxSatiety));
	_maxThirst = (max)(1, state.maxThirst);
	_thirst = (max)(0, (min)(state.thirst, _maxThirst));
	_gold = (max)(0, state.gold);
	_satietyDrainNumerator = (max)(int64{ 0 }, state.satietyDrainNumerator);
	_inventory.clear();
	_inventory.reserve(state.inventory.size());

	uint64 maxStackId = 0;
	uint64 maxAcquiredSequence = 0;
	for (const ExpeditionItemStackState& stack : state.inventory)
	{
		if (stack.stackId == 0 || stack.itemId.empty() || stack.quantity <= 0)
			continue;
		_inventory.push_back(stack);
		maxStackId = (max)(maxStackId, stack.stackId);
		maxAcquiredSequence = (max)(maxAcquiredSequence, stack.acquiredSequence);
	}

	_nextInventoryStackId = (max)(state.nextInventoryStackId, maxStackId + 1);
	_nextAcquiredSequence = (max)(state.nextAcquiredSequence, maxAcquiredSequence + 1);
	const uint64 stockResetMinutes = static_cast<uint64>(GEconomyData.GetConfigValue("village_stock_reset_interval_real_minutes", 70));
	const uint64 stockResetIntervalMs = stockResetMinutes * 60 * 1000;
	_shopRestockElapsedMs = stockResetIntervalMs > 0 ? state.shopRestockElapsedMs % stockResetIntervalMs : 0;
	_shopStockGeneration = (max)(uint64{ 1 }, state.shopStockGeneration);
	ResetVillageShopStock();
	for (const PlayerVillageShopStockState& stock : state.shopStock)
	{
		if (stock.stockGeneration != _shopStockGeneration)
			continue;
		auto villageIt = _shopStockByVillageId.find(stock.villageId);
		if (villageIt == _shopStockByVillageId.end())
			continue;
		auto itemIt = villageIt->second.find(stock.itemId);
		if (itemIt == villageIt->second.end())
			continue;
		itemIt->second = (max)(0, (min)(stock.stock, itemIt->second));
	}
	_quests.clear();
	for (const PlayerQuestState& quest : state.quests)
	{
		if (quest.questId.empty())
			continue;
		_quests.push_back(quest);
	}
	_lastEconomyTickMs = nowMs;
	_economyInitialized = true;
	_economyDirty = false;
	_lastEconomyPersistedMs = nowMs;
}

PersistentPlayerEconomyState Player::ExportEconomyState() const
{
	PersistentPlayerEconomyState state;
	state.gold = _gold;
	state.satiety = _satiety;
	state.maxSatiety = _maxSatiety;
	state.thirst = _thirst;
	state.maxThirst = _maxThirst;
	state.satietyDrainNumerator = _satietyDrainNumerator;
	state.nextInventoryStackId = _nextInventoryStackId;
	state.nextAcquiredSequence = _nextAcquiredSequence;
	state.inventory = _inventory;
	state.shopRestockElapsedMs = _shopRestockElapsedMs;
	state.shopStockGeneration = _shopStockGeneration;
	state.quests = _quests;
	for (const auto& villagePair : _shopStockByVillageId)
	{
		for (const auto& itemPair : villagePair.second)
		{
			PlayerVillageShopStockState stock;
			stock.villageId = villagePair.first;
			stock.itemId = itemPair.first;
			stock.stock = itemPair.second;
			stock.stockGeneration = _shopStockGeneration;
			state.shopStock.push_back(move(stock));
		}
	}
	return state;
}

bool Player::NeedsEconomyPersistence(uint64 nowMs, uint64 minimumIntervalMs) const
{
	return _economyDirty && nowMs >= _lastEconomyPersistedMs &&
		nowMs - _lastEconomyPersistedMs >= minimumIntervalMs;
}

void Player::MarkEconomyPersisted(uint64 nowMs)
{
	_economyDirty = false;
	_lastEconomyPersistedMs = nowMs;
}

bool Player::AdvanceEconomy(uint64 nowMs, vector<string>& autoConsumedItemIds, vector<string>& expiredItemIds)
{
	InitializeEconomy(nowMs);
	if (nowMs <= _lastEconomyTickMs)
		return false;

	const uint64 elapsedMs = nowMs - _lastEconomyTickMs;
	_lastEconomyTickMs = nowMs;
	bool changed = false;
	AdvanceShopRestock(elapsedMs);

	for (ExpeditionItemStackState& stack : _inventory)
	{
		if (stack.remainingShelfLifeMs < 0)
			continue;
		stack.remainingShelfLifeMs = elapsedMs >= static_cast<uint64>(stack.remainingShelfLifeMs)
			? 0
			: stack.remainingShelfLifeMs - static_cast<int64>(elapsedMs);
	}

	for (auto it = _inventory.begin(); it != _inventory.end();)
	{
		if (it->remainingShelfLifeMs == 0)
		{
			expiredItemIds.push_back(it->itemId);
			it = _inventory.erase(it);
			changed = true;
		}
		else
		{
			++it;
		}
	}

	const int32 drainPerMinute = GEconomyData.GetConfigValue("expedition_satiety_drain_per_real_minute", 0);
	if (drainPerMinute > 0 && _satiety > 0)
	{
		_satietyDrainNumerator += static_cast<int64>(elapsedMs) * drainPerMinute;
		const int32 drained = static_cast<int32>(_satietyDrainNumerator / 60000);
		_satietyDrainNumerator %= 60000;
		if (drained > 0)
		{
			_satiety = (max)(0, _satiety - drained);
			changed = true;
		}
	}

	if (TryAutoConsume(autoConsumedItemIds))
		changed = true;
	if (changed)
		_economyDirty = true;
	return changed;
}

bool Player::AddInventoryItem(const EconomyItemTemplate& item, int32 quantity, vector<string>& autoConsumedItemIds)
{
	if (quantity <= 0)
		return false;

	const int64 shelfLifeMs = GetShelfLifeMs(item);
	// Perishable goods are purchase batches: a later purchase must not inherit
	// the earlier stack's remaining shelf life. Non-perishable goods still stack.
	if (shelfLifeMs < 0)
	{
		for (auto it = _inventory.begin(); it != _inventory.end(); ++it)
		{
			if (it->itemId != item.itemId || it->remainingShelfLifeMs >= 0)
				continue;

			if (it->quantity > (numeric_limits<int32>::max)() - quantity)
				return false;

			it->quantity += quantity;
			TryAutoConsume(autoConsumedItemIds);
			_economyDirty = true;
			return true;
		}
	}

	ExpeditionItemStackState stack;
	stack.stackId = _nextInventoryStackId++;
	stack.itemId = item.itemId;
	stack.quantity = quantity;
	stack.remainingShelfLifeMs = shelfLifeMs;
	stack.acquiredSequence = _nextAcquiredSequence++;
	_inventory.push_back(move(stack));
	TryAutoConsume(autoConsumedItemIds);
	_economyDirty = true;
	return true;
}

const ExpeditionItemStackState* Player::FindInventoryStack(uint64 stackId) const
{
	const auto it = find_if(_inventory.begin(), _inventory.end(), [stackId](const ExpeditionItemStackState& stack)
		{
			return stack.stackId == stackId;
		});
	return it != _inventory.end() ? &*it : nullptr;
}

bool Player::RemoveInventoryItem(uint64 stackId, int32 quantity)
{
	if (quantity <= 0)
		return false;

	const auto it = find_if(_inventory.begin(), _inventory.end(), [stackId](const ExpeditionItemStackState& stack)
		{
			return stack.stackId == stackId;
		});
	if (it == _inventory.end() || it->quantity < quantity)
		return false;

	it->quantity -= quantity;
	if (it->quantity == 0)
		_inventory.erase(it);
	_economyDirty = true;
	return true;
}

int32 Player::CountInventoryItem(const string& itemId) const
{
	int64 total = 0;
	for (const ExpeditionItemStackState& stack : _inventory)
	{
		if (stack.itemId == itemId)
			total += stack.quantity;
	}
	return total > (numeric_limits<int32>::max)() ? (numeric_limits<int32>::max)() : static_cast<int32>(total);
}

bool Player::RemoveInventoryItemFefo(const string& itemId, int32 quantity)
{
	if (itemId.empty() || quantity <= 0 || CountInventoryItem(itemId) < quantity)
		return false;

	while (quantity > 0)
	{
		auto best = _inventory.end();
		for (auto it = _inventory.begin(); it != _inventory.end(); ++it)
		{
			if (it->itemId != itemId || it->quantity <= 0)
				continue;
			if (best == _inventory.end())
			{
				best = it;
				continue;
			}
			const bool expires = it->remainingShelfLifeMs >= 0;
			const bool bestExpires = best->remainingShelfLifeMs >= 0;
			if ((expires != bestExpires && expires) ||
				(expires == bestExpires && expires && it->remainingShelfLifeMs < best->remainingShelfLifeMs) ||
				(expires == bestExpires && it->remainingShelfLifeMs == best->remainingShelfLifeMs && it->acquiredSequence < best->acquiredSequence))
				best = it;
		}
		if (best == _inventory.end())
			return false;
		const int32 removed = (min)(quantity, best->quantity);
		best->quantity -= removed;
		quantity -= removed;
		if (best->quantity == 0)
			_inventory.erase(best);
	}
	_economyDirty = true;
	return true;
}

const PlayerQuestState* Player::FindQuestState(const string& questId) const
{
	const auto it = find_if(_quests.begin(), _quests.end(), [&questId](const PlayerQuestState& state) { return state.questId == questId; });
	return it != _quests.end() ? &*it : nullptr;
}

PlayerQuestState* Player::FindQuestState(const string& questId)
{
	const auto it = find_if(_quests.begin(), _quests.end(), [&questId](const PlayerQuestState& state) { return state.questId == questId; });
	return it != _quests.end() ? &*it : nullptr;
}

bool Player::AddQuestState(PlayerQuestState state)
{
	if (state.questId.empty() || FindQuestState(state.questId) != nullptr)
		return false;
	_quests.push_back(move(state));
	_economyDirty = true;
	return true;
}

int32 Player::GetVillageShopStock(const string& villageId, const string& itemId) const
{
	const auto villageIt = _shopStockByVillageId.find(villageId);
	if (villageIt == _shopStockByVillageId.end())
		return -1;
	const auto itemIt = villageIt->second.find(itemId);
	return itemIt != villageIt->second.end() ? itemIt->second : -1;
}

bool Player::SpendVillageShopStock(const string& villageId, const string& itemId, int32 quantity)
{
	if (quantity <= 0)
		return false;
	auto villageIt = _shopStockByVillageId.find(villageId);
	if (villageIt == _shopStockByVillageId.end())
		return false;
	auto itemIt = villageIt->second.find(itemId);
	if (itemIt == villageIt->second.end() || itemIt->second < quantity)
		return false;
	itemIt->second -= quantity;
	_economyDirty = true;
	return true;
}

uint32 Player::GetShopRestockRemainingSeconds(uint64 intervalMs) const
{
	if (intervalMs == 0)
		return 0;
	const uint64 elapsed = _shopRestockElapsedMs % intervalMs;
	const uint64 remainingMs = intervalMs - elapsed;
	const uint64 remainingSeconds = (remainingMs + 999) / 1000;
	return remainingSeconds > (numeric_limits<uint32>::max)()
		? (numeric_limits<uint32>::max)()
		: static_cast<uint32>(remainingSeconds);
}

bool Player::SpendGold(int32 amount)
{
	if (amount < 0 || _gold < amount)
		return false;
	_gold -= amount;
	_economyDirty = true;
	return true;
}

void Player::AddGold(int32 amount)
{
	if (amount <= 0)
		return;
	_gold = amount > (numeric_limits<int32>::max)() - _gold
		? (numeric_limits<int32>::max)()
		: _gold + amount;
	_economyDirty = true;
}

void Player::FillExpeditionState(Protocol::S_EXPEDITION_STATE& packet,
	const vector<string>& autoConsumedItemIds, const vector<string>& expiredItemIds) const
{
	packet.set_gold(_gold);
	packet.set_satiety(_satiety);
	packet.set_max_satiety(_maxSatiety);
	packet.set_thirst(_thirst);
	packet.set_max_thirst(_maxThirst);
	for (const ExpeditionItemStackState& stack : _inventory)
	{
		Protocol::ExpeditionItemStackInfo* stackInfo = packet.add_inventory();
		stackInfo->set_stack_id(stack.stackId);
		stackInfo->set_item_id(stack.itemId);
		stackInfo->set_quantity(stack.quantity);
		stackInfo->set_remaining_shelf_life_seconds(stack.remainingShelfLifeMs < 0
			? -1
			: (stack.remainingShelfLifeMs + 999) / 1000);
	}
	for (const string& itemId : autoConsumedItemIds)
		packet.add_auto_consumed_item_ids(itemId);
	for (const string& itemId : expiredItemIds)
		packet.add_expired_item_ids(itemId);
}

bool Player::TryAutoConsume(vector<string>& autoConsumedItemIds)
{
	const int32 threshold = GEconomyData.GetConfigValue("expedition_satiety_refill_threshold", 60);
	const int32 target = GEconomyData.GetConfigValue("expedition_satiety_refill_target", 80);
	if (_satiety > threshold)
		return false;

	bool consumedAny = false;
	while (_satiety < target)
	{
		auto best = _inventory.end();
		const EconomyItemTemplate* bestItem = nullptr;
		for (auto it = _inventory.begin(); it != _inventory.end(); ++it)
		{
			const EconomyItemTemplate* item = GEconomyData.GetItem(it->itemId);
			if (item == nullptr || item->itemType != EconomyItemType::Food || item->satietyDelta <= 0 || it->quantity <= 0)
				continue;

			if (best == _inventory.end())
			{
				best = it;
				bestItem = item;
				continue;
			}

			const bool candidateExpires = it->remainingShelfLifeMs >= 0;
			const bool bestExpires = best->remainingShelfLifeMs >= 0;
			const bool candidateFirst =
				(candidateExpires != bestExpires && candidateExpires) ||
				(candidateExpires == bestExpires && candidateExpires && it->remainingShelfLifeMs < best->remainingShelfLifeMs) ||
				(candidateExpires == bestExpires && it->remainingShelfLifeMs == best->remainingShelfLifeMs && item->satietyDelta < bestItem->satietyDelta) ||
				(candidateExpires == bestExpires && it->remainingShelfLifeMs == best->remainingShelfLifeMs && item->satietyDelta == bestItem->satietyDelta && it->acquiredSequence < best->acquiredSequence);
			if (candidateFirst)
			{
				best = it;
				bestItem = item;
			}
		}

		if (best == _inventory.end() || bestItem == nullptr)
			break;

		_satiety = (min)(_maxSatiety, _satiety + bestItem->satietyDelta);
		autoConsumedItemIds.push_back(best->itemId);
		best->quantity--;
		if (best->quantity == 0)
			_inventory.erase(best);
		consumedAny = true;
	}

	return consumedAny;
}

int64 Player::GetShelfLifeMs(const EconomyItemTemplate& item) const
{
	if (item.shelfLifeDays < 0)
		return -1;

	const int64 realMinutesPerGameDay = GEconomyData.GetConfigValue("real_minutes_per_game_day", 10);
	return static_cast<int64>(item.shelfLifeDays) * realMinutesPerGameDay * 60 * 1000;
}

void Player::ResetVillageShopStock()
{
	_shopStockByVillageId.clear();
	for (const auto& villagePair : GEconomyData.GetAllVillageShopStock())
	{
		for (const VillageShopStockTemplate& stock : villagePair.second)
			_shopStockByVillageId[villagePair.first][stock.itemId] = stock.maxStock;
	}
}

void Player::AdvanceShopRestock(uint64 elapsedMs)
{
	const int32 resetMinutes = GEconomyData.GetConfigValue("village_stock_reset_interval_real_minutes", 0);
	if (resetMinutes <= 0 || elapsedMs == 0)
		return;
	const uint64 intervalMs = static_cast<uint64>(resetMinutes) * 60 * 1000;
	const uint64 elapsedBefore = _shopRestockElapsedMs % intervalMs;
	const uint64 completeIntervals = elapsedMs / intervalMs;
	const uint64 remainder = elapsedMs % intervalMs;
	const bool crossedBoundary = completeIntervals > 0 || elapsedBefore >= intervalMs - remainder;
	const uint64 crossedIntervals = completeIntervals + (elapsedBefore >= intervalMs - remainder ? 1 : 0);
	_shopRestockElapsedMs = (elapsedBefore + remainder) % intervalMs;
	_economyDirty = true;

	if (!crossedBoundary)
		return;
	if (_shopStockGeneration > (numeric_limits<uint64>::max)() - crossedIntervals)
		_shopStockGeneration = 1;
	else
		_shopStockGeneration += crossedIntervals;
	ResetVillageShopStock();
	cout << "PLAYER_SHOP_STOCK_RESET player_id=" << objectInfo->object_id()
		<< " generation=" << _shopStockGeneration << endl;
}
