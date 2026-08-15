#include "pch.h"
#include "EconomyService.h"
#include "EconomyDataManager.h"
#include "VillageDataManager.h"
#include "Player.h"
#include "GameSession.h"
#include "ServerPacketHandler.h"

#include <limits>

EconomyService GEconomyService;

bool EconomyService::Initialize(uint64 nowMs)
{
	if (_initialized)
		return true;

	const int32 resetMinutes = GEconomyData.GetConfigValue("village_stock_reset_interval_real_minutes", 0);
	if (resetMinutes <= 0)
		return false;

	_stockResetIntervalMs = static_cast<uint64>(resetMinutes) * 60 * 1000;
	_nextStockResetMs = nowMs + _stockResetIntervalMs;
	ResetAllStock();
	_initialized = !_currentStockByVillageId.empty();
	cout << "[EconomyService] Initialize " << (_initialized ? "success" : "failed")
		<< " villages=" << _currentStockByVillageId.size()
		<< " reset_minutes=" << resetMinutes << endl;
	return _initialized;
}

void EconomyService::UpdateTick(uint64 nowMs)
{
	if (!_initialized || nowMs < _nextStockResetMs)
		return;

	ResetAllStock();
	do
	{
		_nextStockResetMs += _stockResetIntervalMs;
	} while (nowMs >= _nextStockResetMs);

	cout << "[EconomyService] Village stock fully reset" << endl;
}

void EconomyService::SendExpeditionState(const PlayerRef& player,
	const vector<string>& autoConsumedItemIds, const vector<string>& expiredItemIds) const
{
	if (player == nullptr)
		return;
	GameSessionRef session = player->session.lock();
	if (session == nullptr)
		return;

	Protocol::S_EXPEDITION_STATE packet;
	player->FillExpeditionState(packet, autoConsumedItemIds, expiredItemIds);
	session->Send(ServerPacketHandler::MakeSendBuffer(packet));
}

void EconomyService::HandleShopOpen(GameSessionRef session, PlayerRef player, const string& villageId)
{
	string reason;
	const bool success = ValidateShopAccess(player, villageId, reason);
	SendShopState(session, player, villageId, "open", success, reason);
}

void EconomyService::HandleShopBuy(GameSessionRef session, PlayerRef player, const string& villageId,
	const string& itemId, int32 quantity)
{
	string reason;
	if (!ValidateShopAccess(player, villageId, reason))
	{
		SendShopState(session, player, villageId, "buy", false, reason);
		return;
	}
	if (quantity <= 0 || quantity > 999)
	{
		SendShopState(session, player, villageId, "buy", false, "invalid quantity");
		return;
	}

	const EconomyItemTemplate* item = GEconomyData.GetItem(itemId);
	const vector<VillageShopStockTemplate>* stockRows = GEconomyData.GetVillageShopStock(villageId);
	const VillageShopStockTemplate* stockTemplate = nullptr;
	if (stockRows != nullptr)
	{
		for (const VillageShopStockTemplate& row : *stockRows)
		{
			if (row.itemId == itemId)
			{
				stockTemplate = &row;
				break;
			}
		}
	}
	int32* currentStock = FindRuntimeStock(villageId, itemId);
	if (item == nullptr || stockTemplate == nullptr || currentStock == nullptr)
	{
		SendShopState(session, player, villageId, "buy", false, "item is not sold here");
		return;
	}
	if (*currentStock < quantity)
	{
		SendShopState(session, player, villageId, "buy", false, "not enough village stock");
		return;
	}

	const int64 totalPrice64 = static_cast<int64>(stockTemplate->unitSellPrice) * quantity;
	if (totalPrice64 <= 0 || totalPrice64 > (numeric_limits<int32>::max)())
	{
		SendShopState(session, player, villageId, "buy", false, "invalid total price");
		return;
	}
	const int32 totalPrice = static_cast<int32>(totalPrice64);
	if (!player->SpendGold(totalPrice))
	{
		SendShopState(session, player, villageId, "buy", false, "not enough gold");
		return;
	}

	vector<string> autoConsumedItemIds;
	if (!player->AddInventoryItem(*item, quantity, autoConsumedItemIds))
	{
		player->AddGold(totalPrice);
		SendShopState(session, player, villageId, "buy", false, "inventory update failed");
		return;
	}

	*currentStock -= quantity;
	cout << "VILLAGE_SHOP_BUY player_id=" << player->objectInfo->object_id()
		<< " village_id=" << villageId << " item_id=" << itemId
		<< " quantity=" << quantity << " total_price=" << totalPrice << endl;
	SendShopState(session, player, villageId, "buy", true, "", autoConsumedItemIds);
}

void EconomyService::HandleShopSell(GameSessionRef session, PlayerRef player, const string& villageId,
	uint64 stackId, int32 quantity)
{
	string reason;
	if (!ValidateShopAccess(player, villageId, reason))
	{
		SendShopState(session, player, villageId, "sell", false, reason);
		return;
	}
	if (quantity <= 0 || quantity > 999)
	{
		SendShopState(session, player, villageId, "sell", false, "invalid quantity");
		return;
	}

	const ExpeditionItemStackState* stack = player->FindInventoryStack(stackId);
	if (stack == nullptr || stack->quantity < quantity)
	{
		SendShopState(session, player, villageId, "sell", false, "inventory stack is unavailable");
		return;
	}
	const string itemId = stack->itemId;
	const EconomyItemTemplate* item = GEconomyData.GetItem(itemId);
	if (item == nullptr || item->itemType != EconomyItemType::Trade)
	{
		SendShopState(session, player, villageId, "sell", false, "food cannot be sold");
		return;
	}

	int32 unitBuyPrice = 0;
	if (!GEconomyData.TryGetTradeBuyPrice(itemId, villageId, unitBuyPrice))
	{
		SendShopState(session, player, villageId, "sell", false, "this village does not buy that trade good");
		return;
	}
	const int64 totalPrice64 = static_cast<int64>(unitBuyPrice) * quantity;
	if (totalPrice64 <= 0 || totalPrice64 > (numeric_limits<int32>::max)())
	{
		SendShopState(session, player, villageId, "sell", false, "invalid total price");
		return;
	}
	if (!player->RemoveInventoryItem(stackId, quantity))
	{
		SendShopState(session, player, villageId, "sell", false, "inventory update failed");
		return;
	}

	const int32 totalPrice = static_cast<int32>(totalPrice64);
	player->AddGold(totalPrice);
	cout << "VILLAGE_SHOP_SELL player_id=" << player->objectInfo->object_id()
		<< " village_id=" << villageId << " item_id=" << itemId
		<< " quantity=" << quantity << " total_price=" << totalPrice << endl;
	SendShopState(session, player, villageId, "sell", true, "");
}

bool EconomyService::ValidateShopAccess(const PlayerRef& player, const string& villageId, string& reason) const
{
	if (player == nullptr)
	{
		reason = "player is unavailable";
		return false;
	}
	if (villageId.empty() || player->activeVillageId != villageId)
	{
		reason = "player is not interacting with this village";
		return false;
	}
	const VillageTemplate* village = GVillageData.GetVillage(villageId);
	if (village == nullptr || !village->shopEnabled)
	{
		reason = "village shop is unavailable";
		return false;
	}
	return true;
}

void EconomyService::ResetAllStock()
{
	_currentStockByVillageId.clear();
	for (const auto& villagePair : GEconomyData.GetAllVillageShopStock())
	{
		for (const VillageShopStockTemplate& stock : villagePair.second)
			_currentStockByVillageId[villagePair.first][stock.itemId] = stock.maxStock;
	}
}

void EconomyService::SendShopState(GameSessionRef session, const PlayerRef& player, const string& villageId,
	const string& action, bool success, const string& reason,
	const vector<string>& autoConsumedItemIds, const vector<string>& expiredItemIds) const
{
	if (session == nullptr)
		return;

	Protocol::S_VILLAGE_SHOP_STATE packet;
	packet.set_success(success);
	packet.set_reason(reason);
	packet.set_action(action);
	packet.set_village_id(villageId);
	if (player != nullptr)
		player->FillExpeditionState(*packet.mutable_expedition(), autoConsumedItemIds, expiredItemIds);

	if (const vector<VillageShopStockTemplate>* rows = GEconomyData.GetVillageShopStock(villageId))
	{
		for (const VillageShopStockTemplate& row : *rows)
		{
			Protocol::VillageShopListingInfo* listing = packet.add_listings();
			listing->set_item_id(row.itemId);
			listing->set_max_stock(row.maxStock);
			listing->set_unit_sell_price(row.unitSellPrice);
			int32 currentStock = 0;
			const auto villageIt = _currentStockByVillageId.find(villageId);
			if (villageIt != _currentStockByVillageId.end())
			{
				const auto itemIt = villageIt->second.find(row.itemId);
				if (itemIt != villageIt->second.end())
					currentStock = itemIt->second;
			}
			listing->set_stock(currentStock);
		}
	}
	if (player != nullptr)
	{
		for (const ExpeditionItemStackState& stack : player->Inventory())
		{
			int32 unitBuyPrice = 0;
			if (!GEconomyData.TryGetTradeBuyPrice(stack.itemId, villageId, unitBuyPrice))
				continue;
			Protocol::VillageTradeBuyOfferInfo* offer = packet.add_trade_buy_offers();
			offer->set_stack_id(stack.stackId);
			offer->set_item_id(stack.itemId);
			offer->set_unit_buy_price(unitBuyPrice);
		}
	}
	packet.set_stock_reset_remaining_seconds(GetStockResetRemainingSeconds(::GetTickCount64()));
	session->Send(ServerPacketHandler::MakeSendBuffer(packet));
}

int32* EconomyService::FindRuntimeStock(const string& villageId, const string& itemId)
{
	auto villageIt = _currentStockByVillageId.find(villageId);
	if (villageIt == _currentStockByVillageId.end())
		return nullptr;
	auto itemIt = villageIt->second.find(itemId);
	return itemIt != villageIt->second.end() ? &itemIt->second : nullptr;
}

uint32 EconomyService::GetStockResetRemainingSeconds(uint64 nowMs) const
{
	if (!_initialized || nowMs >= _nextStockResetMs)
		return 0;
	const uint64 remainingMs = _nextStockResetMs - nowMs;
	const uint64 remainingSeconds = (remainingMs + 999) / 1000;
	return remainingSeconds > (numeric_limits<uint32>::max)()
		? (numeric_limits<uint32>::max)()
		: static_cast<uint32>(remainingSeconds);
}
