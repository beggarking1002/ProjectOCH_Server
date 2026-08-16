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
	_economyInitialized = true;
	_economyDirty = true;
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
	for (auto it = _inventory.begin(); it != _inventory.end();)
	{
		if (it->itemId != item.itemId)
		{
			++it;
			continue;
		}

		if (it->quantity > (numeric_limits<int32>::max)() - quantity)
			return false;

		// Stack identical items even when they are perishable.  Keeping the earliest
		// expiry prevents a newly acquired item from extending an existing stack.
		it->quantity += quantity;
		if (shelfLifeMs >= 0 && (it->remainingShelfLifeMs < 0 || shelfLifeMs < it->remainingShelfLifeMs))
			it->remainingShelfLifeMs = shelfLifeMs;
		TryAutoConsume(autoConsumedItemIds);
		_economyDirty = true;
		return true;
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
