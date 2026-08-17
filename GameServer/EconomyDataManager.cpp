#include "pch.h"
#include "EconomyDataManager.h"
#include "VillageDataManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>

EconomyDataManager GEconomyData;

namespace
{
	struct CsvTable
	{
		unordered_map<string, size_t> columns;
		vector<vector<string>> rows;
	};

	string Trim(string value)
	{
		if (value.size() >= 3 &&
			static_cast<unsigned char>(value[0]) == 0xEF &&
			static_cast<unsigned char>(value[1]) == 0xBB &&
			static_cast<unsigned char>(value[2]) == 0xBF)
		{
			value.erase(0, 3);
		}

		auto isNotSpace = [](unsigned char ch) { return isspace(ch) == 0; };
		value.erase(value.begin(), find_if(value.begin(), value.end(), isNotSpace));
		value.erase(find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
		return value;
	}

	vector<string> ParseCsvLine(const string& line)
	{
		vector<string> cells;
		string cell;
		bool inQuotes = false;

		for (size_t i = 0; i < line.size(); i++)
		{
			const char ch = line[i];
			if (inQuotes)
			{
				if (ch == '"')
				{
					if (i + 1 < line.size() && line[i + 1] == '"')
					{
						cell.push_back('"');
						i++;
					}
					else
					{
						inQuotes = false;
					}
				}
				else
				{
					cell.push_back(ch);
				}
			}
			else if (ch == '"')
			{
				inQuotes = true;
			}
			else if (ch == ',')
			{
				cells.push_back(Trim(cell));
				cell.clear();
			}
			else
			{
				cell.push_back(ch);
			}
		}

		cells.push_back(Trim(cell));
		return cells;
	}

	bool LoadCsv(const string& path, const vector<string>& requiredColumns, CsvTable& outTable)
	{
		ifstream file(path);
		if (!file.is_open())
		{
			cout << "[EconomyDataManager] Failed to read " << path << endl;
			return false;
		}

		string line;
		if (!getline(file, line))
		{
			cout << "[EconomyDataManager] Empty CSV " << path << endl;
			return false;
		}

		const vector<string> header = ParseCsvLine(line);
		outTable.columns.clear();
		outTable.rows.clear();
		for (size_t index = 0; index < header.size(); index++)
			outTable.columns[header[index]] = index;

		for (const string& column : requiredColumns)
		{
			if (outTable.columns.find(column) == outTable.columns.end())
			{
				cout << "[EconomyDataManager] Missing column " << column << " in " << path << endl;
				return false;
			}
		}

		// The second row documents field types and is intentionally not data.
		if (!getline(file, line))
		{
			cout << "[EconomyDataManager] Missing type row in " << path << endl;
			return false;
		}

		while (getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (Trim(line).empty())
				continue;
			outTable.rows.push_back(ParseCsvLine(line));
		}

		return true;
	}

	string Cell(const CsvTable& table, const vector<string>& row, const string& column)
	{
		const size_t index = table.columns.at(column);
		return index < row.size() ? row[index] : "";
	}

	bool TryParseInt32(const string& text, int32& outValue, bool allowEmpty = false)
	{
		if (text.empty())
		{
			outValue = 0;
			return allowEmpty;
		}

		try
		{
			size_t parsed = 0;
			const long long value = stoll(text, &parsed);
			if (parsed != text.size() || value < (numeric_limits<int32>::min)() || value > (numeric_limits<int32>::max)())
				return false;
			outValue = static_cast<int32>(value);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	string NormalizeUpper(string value)
	{
		transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
			{
				return static_cast<char>(toupper(ch));
			});
		return value;
	}
}

bool EconomyDataManager::Load()
{
	if (_loaded)
		return true;

	_itemsById.clear();
	_shopStockByVillageId.clear();
	_tradeByItemId.clear();
	_configByKey.clear();

	if (!LoadItems() || !LoadVillageShopStock() || !LoadVillageTrade() || !LoadConfig() || !Validate())
		return false;

	_loaded = true;
	size_t stockCount = 0;
	for (const auto& pair : _shopStockByVillageId)
		stockCount += pair.second.size();

	cout << "[EconomyDataManager] Load success items=" << _itemsById.size()
		<< " stock_rows=" << stockCount
		<< " trade_routes=" << _tradeByItemId.size()
		<< " configs=" << _configByKey.size() << endl;
	return true;
}

const EconomyItemTemplate* EconomyDataManager::GetItem(const string& itemId) const
{
	const auto it = _itemsById.find(itemId);
	return it != _itemsById.end() ? &it->second : nullptr;
}

const vector<VillageShopStockTemplate>* EconomyDataManager::GetVillageShopStock(const string& villageId) const
{
	const auto it = _shopStockByVillageId.find(villageId);
	return it != _shopStockByVillageId.end() ? &it->second : nullptr;
}

const VillageTradeTemplate* EconomyDataManager::GetTrade(const string& itemId) const
{
	const auto it = _tradeByItemId.find(itemId);
	return it != _tradeByItemId.end() ? &it->second : nullptr;
}

int32 EconomyDataManager::GetConfigValue(const string& key, int32 fallback) const
{
	const auto it = _configByKey.find(key);
	return it != _configByKey.end() ? it->second.value : fallback;
}

bool EconomyDataManager::TryGetTradeBuyPrice(const string& itemId, const string& villageId, int32& outPrice) const
{
	outPrice = 0;
	const EconomyItemTemplate* item = GetItem(itemId);
	const VillageTradeTemplate* trade = GetTrade(itemId);
	if (item == nullptr || item->itemType != EconomyItemType::Trade || trade == nullptr)
		return false;
	if (GVillageData.GetVillage(villageId) == nullptr)
		return false;
	if (villageId == trade->originVillageId || VillageSellsItem(villageId, itemId))
		return false;

	const vector<VillageShopStockTemplate>* originStock = GetVillageShopStock(trade->originVillageId);
	if (originStock == nullptr)
		return false;

	int32 originPrice = 0;
	for (const VillageShopStockTemplate& stock : *originStock)
	{
		if (stock.itemId == itemId)
		{
			originPrice = stock.unitSellPrice;
			break;
		}
	}
	if (originPrice <= 0)
		return false;

	const string marginKey = villageId == trade->premiumBuyerVillageId
		? "trade_premium_margin_percent"
		: "trade_other_margin_percent";
	const int32 marginPercent = GetConfigValue(marginKey, -1);
	if (marginPercent < 0)
		return false;

	// Gold is integral. Round to the nearest gold and round exact .5 values up.
	const int64 calculated = (static_cast<int64>(originPrice) * (100 + marginPercent) + 50) / 100;
	if (calculated <= 0 || calculated > (numeric_limits<int32>::max)())
		return false;

	outPrice = static_cast<int32>(calculated);
	return true;
}

bool EconomyDataManager::LoadItems()
{
	CsvTable table;
	const string path = ResolveDataPath("Item.csv");
	if (!LoadCsv(path,
		{ "ItemId", "ItemType", "DisplayName", "SatietyDelta", "HappinessDelta", "ThirstDelta", "WaterCapacity", "ShelfLifeDays", "BasePrice" }, table))
		return false;

	int32 lineNumber = 2;
	for (const vector<string>& row : table.rows)
	{
		lineNumber++;
		EconomyItemTemplate item;
		item.itemId = Cell(table, row, "ItemId");
		item.displayName = Cell(table, row, "DisplayName");
		const string itemType = NormalizeUpper(Cell(table, row, "ItemType"));
		if (itemType == "FOOD")
			item.itemType = EconomyItemType::Food;
		else if (itemType == "DRINK")
			item.itemType = EconomyItemType::Drink;
		else if (itemType == "TRADE")
			item.itemType = EconomyItemType::Trade;
		else
		{
			cout << "[EconomyDataManager] Invalid ItemType at " << path << ':' << lineNumber << endl;
			return false;
		}

		if (item.itemId.empty() || item.displayName.empty() ||
			!TryParseInt32(Cell(table, row, "SatietyDelta"), item.satietyDelta) ||
			!TryParseInt32(Cell(table, row, "HappinessDelta"), item.happinessDelta) ||
			!TryParseInt32(Cell(table, row, "ThirstDelta"), item.thirstDelta) ||
			!TryParseInt32(Cell(table, row, "WaterCapacity"), item.waterCapacity) ||
			!TryParseInt32(Cell(table, row, "ShelfLifeDays"), item.shelfLifeDays) ||
			!TryParseInt32(Cell(table, row, "BasePrice"), item.basePrice, item.itemType == EconomyItemType::Trade))
		{
			cout << "[EconomyDataManager] Invalid item row " << path << ':' << lineNumber << endl;
			return false;
		}

		if (!_itemsById.emplace(item.itemId, move(item)).second)
		{
			cout << "[EconomyDataManager] Duplicate ItemId at " << path << ':' << lineNumber << endl;
			return false;
		}
	}

	return !_itemsById.empty();
}

bool EconomyDataManager::LoadVillageShopStock()
{
	CsvTable table;
	const string path = ResolveDataPath("VillageShopStock.csv");
	if (!LoadCsv(path, { "VillageId", "ItemId", "InitialStock", "MaxStock", "UnitSellPrice" }, table))
		return false;

	unordered_map<string, bool> uniqueRows;
	int32 lineNumber = 2;
	for (const vector<string>& row : table.rows)
	{
		lineNumber++;
		VillageShopStockTemplate stock;
		stock.villageId = Cell(table, row, "VillageId");
		stock.itemId = Cell(table, row, "ItemId");
		if (stock.villageId.empty() || stock.itemId.empty() ||
			!TryParseInt32(Cell(table, row, "InitialStock"), stock.initialStock) ||
			!TryParseInt32(Cell(table, row, "MaxStock"), stock.maxStock) ||
			!TryParseInt32(Cell(table, row, "UnitSellPrice"), stock.unitSellPrice))
		{
			cout << "[EconomyDataManager] Invalid stock row " << path << ':' << lineNumber << endl;
			return false;
		}

		const string uniqueKey = stock.villageId + "\x1f" + stock.itemId;
		if (!uniqueRows.emplace(uniqueKey, true).second)
		{
			cout << "[EconomyDataManager] Duplicate village stock row " << path << ':' << lineNumber << endl;
			return false;
		}
		_shopStockByVillageId[stock.villageId].push_back(move(stock));
	}

	return !_shopStockByVillageId.empty();
}

bool EconomyDataManager::LoadVillageTrade()
{
	CsvTable table;
	const string path = ResolveDataPath("VillageTrade.csv");
	if (!LoadCsv(path, { "ItemId", "OriginVillageId", "PremiumBuyerVillageId" }, table))
		return false;

	int32 lineNumber = 2;
	for (const vector<string>& row : table.rows)
	{
		lineNumber++;
		VillageTradeTemplate trade;
		trade.itemId = Cell(table, row, "ItemId");
		trade.originVillageId = Cell(table, row, "OriginVillageId");
		trade.premiumBuyerVillageId = Cell(table, row, "PremiumBuyerVillageId");
		if (trade.itemId.empty() || trade.originVillageId.empty() || trade.premiumBuyerVillageId.empty())
		{
			cout << "[EconomyDataManager] Invalid trade row " << path << ':' << lineNumber << endl;
			return false;
		}
		if (!_tradeByItemId.emplace(trade.itemId, move(trade)).second)
		{
			cout << "[EconomyDataManager] Duplicate trade ItemId at " << path << ':' << lineNumber << endl;
			return false;
		}
	}

	return !_tradeByItemId.empty();
}

bool EconomyDataManager::LoadConfig()
{
	CsvTable table;
	const string path = ResolveDataPath("EconomyConfig.csv");
	if (!LoadCsv(path, { "ConfigKey", "Value", "Unit" }, table))
		return false;

	int32 lineNumber = 2;
	for (const vector<string>& row : table.rows)
	{
		lineNumber++;
		EconomyConfigTemplate config;
		config.key = Cell(table, row, "ConfigKey");
		config.unit = Cell(table, row, "Unit");
		if (config.key.empty() || !TryParseInt32(Cell(table, row, "Value"), config.value))
		{
			cout << "[EconomyDataManager] Invalid config row " << path << ':' << lineNumber << endl;
			return false;
		}
		if (!_configByKey.emplace(config.key, move(config)).second)
		{
			cout << "[EconomyDataManager] Duplicate ConfigKey at " << path << ':' << lineNumber << endl;
			return false;
		}
	}

	return !_configByKey.empty();
}

bool EconomyDataManager::Validate() const
{
	for (const auto& pair : _itemsById)
	{
		const EconomyItemTemplate& item = pair.second;
		if (item.satietyDelta < 0 || item.happinessDelta < 0 || item.thirstDelta < 0 ||
			item.waterCapacity < 0 || item.shelfLifeDays < -1)
		{
			cout << "[EconomyDataManager] Invalid item values item_id=" << item.itemId << endl;
			return false;
		}
		if (item.itemType == EconomyItemType::Food &&
			(item.basePrice <= 0 || item.waterCapacity != 0 || (item.satietyDelta <= 0 && item.happinessDelta <= 0)))
		{
			cout << "[EconomyDataManager] Invalid food values item_id=" << item.itemId << endl;
			return false;
		}
		if (item.itemType == EconomyItemType::Drink &&
			(item.basePrice <= 0 || item.waterCapacity <= 0 || item.thirstDelta != 0 ||
				item.satietyDelta != 0 || item.happinessDelta != 0 || item.shelfLifeDays != -1))
		{
			cout << "[EconomyDataManager] Invalid drink values item_id=" << item.itemId << endl;
			return false;
		}
		if (item.itemType == EconomyItemType::Trade &&
			(item.satietyDelta != 0 || item.happinessDelta != 0 || item.thirstDelta != 0 ||
				item.waterCapacity != 0 || item.shelfLifeDays != -1))
		{
			cout << "[EconomyDataManager] Trade item has food values item_id=" << item.itemId << endl;
			return false;
		}
	}

	for (const auto& pair : _shopStockByVillageId)
	{
		if (GVillageData.GetVillage(pair.first) == nullptr)
		{
			cout << "[EconomyDataManager] Unknown stock village_id=" << pair.first << endl;
			return false;
		}
		for (const VillageShopStockTemplate& stock : pair.second)
		{
			if (GetItem(stock.itemId) == nullptr || stock.initialStock <= 0 ||
				stock.maxStock <= 0 || stock.initialStock > stock.maxStock || stock.unitSellPrice <= 0)
			{
				cout << "[EconomyDataManager] Invalid village stock village_id=" << stock.villageId
					<< " item_id=" << stock.itemId << endl;
				return false;
			}
		}
	}

	for (const auto& pair : _tradeByItemId)
	{
		const VillageTradeTemplate& trade = pair.second;
		const EconomyItemTemplate* item = GetItem(trade.itemId);
		if (item == nullptr || item->itemType != EconomyItemType::Trade ||
			GVillageData.GetVillage(trade.originVillageId) == nullptr ||
			GVillageData.GetVillage(trade.premiumBuyerVillageId) == nullptr ||
			trade.originVillageId == trade.premiumBuyerVillageId ||
			!VillageSellsItem(trade.originVillageId, trade.itemId) ||
			VillageSellsItem(trade.premiumBuyerVillageId, trade.itemId))
		{
			cout << "[EconomyDataManager] Invalid trade route item_id=" << trade.itemId << endl;
			return false;
		}
	}

	for (const auto& pair : _itemsById)
	{
		if (pair.second.itemType == EconomyItemType::Trade && GetTrade(pair.first) == nullptr)
		{
			cout << "[EconomyDataManager] Missing trade route item_id=" << pair.first << endl;
			return false;
		}
	}

	const array<string, 17> requiredConfigs =
	{
		"real_minutes_per_game_day",
		"expedition_satiety_max",
		"expedition_satiety_drain_per_real_minute",
		"expedition_satiety_refill_threshold",
		"expedition_satiety_refill_target",
		"expedition_happiness_max",
		"expedition_happiness_drain_per_real_minute",
		"expedition_happiness_refill_threshold",
		"expedition_happiness_refill_target",
		"expedition_thirst_max",
		"expedition_thirst_drain_per_real_minute",
		"expedition_thirst_refill_threshold",
		"expedition_thirst_refill_target",
		"village_stock_reset_interval_real_minutes",
		"trade_premium_margin_percent",
		"trade_other_margin_percent",
		"expedition_starting_gold"
	};
	for (const string& key : requiredConfigs)
	{
		if (_configByKey.find(key) == _configByKey.end())
		{
			cout << "[EconomyDataManager] Missing required config key=" << key << endl;
			return false;
		}
	}

	const int32 maxSatiety = GetConfigValue("expedition_satiety_max");
	const int32 threshold = GetConfigValue("expedition_satiety_refill_threshold");
	const int32 target = GetConfigValue("expedition_satiety_refill_target");
	const int32 maxHappiness = GetConfigValue("expedition_happiness_max");
	const int32 happinessThreshold = GetConfigValue("expedition_happiness_refill_threshold");
	const int32 happinessTarget = GetConfigValue("expedition_happiness_refill_target");
	const int32 maxThirst = GetConfigValue("expedition_thirst_max");
	const int32 thirstThreshold = GetConfigValue("expedition_thirst_refill_threshold");
	const int32 thirstTarget = GetConfigValue("expedition_thirst_refill_target");
	if (maxSatiety <= 0 || threshold < 0 || threshold >= target || target > maxSatiety ||
		GetConfigValue("expedition_satiety_drain_per_real_minute") <= 0 ||
		maxHappiness <= 0 || happinessThreshold < 0 || happinessThreshold >= happinessTarget || happinessTarget > maxHappiness ||
		GetConfigValue("expedition_happiness_drain_per_real_minute") <= 0 ||
		maxThirst <= 0 || thirstThreshold < 0 || thirstThreshold >= thirstTarget || thirstTarget > maxThirst ||
		GetConfigValue("expedition_thirst_drain_per_real_minute") <= 0 ||
		GetConfigValue("real_minutes_per_game_day") <= 0 ||
		GetConfigValue("village_stock_reset_interval_real_minutes") <= 0 ||
		GetConfigValue("trade_premium_margin_percent") < 0 ||
		GetConfigValue("trade_other_margin_percent") < 0 ||
		GetConfigValue("expedition_starting_gold") < 0)
	{
		cout << "[EconomyDataManager] Invalid economy configuration" << endl;
		return false;
	}

	return true;
}

bool EconomyDataManager::VillageSellsItem(const string& villageId, const string& itemId) const
{
	const vector<VillageShopStockTemplate>* stockRows = GetVillageShopStock(villageId);
	if (stockRows == nullptr)
		return false;
	return any_of(stockRows->begin(), stockRows->end(), [&itemId](const VillageShopStockTemplate& stock)
		{
			return stock.itemId == itemId;
		});
}

string EconomyDataManager::ResolveDataPath(const string& fileName) const
{
	const vector<string> candidates =
	{
		"Data\\" + fileName,
		"..\\Data\\" + fileName,
		"..\\..\\Data\\" + fileName,
		"..\\..\\..\\Data\\" + fileName
	};

	for (const string& candidate : candidates)
	{
		ifstream file(candidate);
		if (file.is_open())
			return candidate;
	}

	return candidates.front();
}
