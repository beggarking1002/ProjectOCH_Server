#pragma once

enum class EconomyItemType
{
	Food,
	Drink,
	Trade,
};

struct EconomyItemTemplate
{
	string itemId;
	EconomyItemType itemType = EconomyItemType::Food;
	string displayName;
	int32 satietyDelta = 0;
	int32 happinessDelta = 0;
	int32 thirstDelta = 0;
	int32 shelfLifeDays = -1;
	int32 basePrice = 0;
};

struct VillageShopStockTemplate
{
	string villageId;
	string itemId;
	int32 initialStock = 0;
	int32 maxStock = 0;
	int32 unitSellPrice = 0;
};

struct VillageTradeTemplate
{
	string itemId;
	string originVillageId;
	string premiumBuyerVillageId;
};

struct EconomyConfigTemplate
{
	string key;
	int32 value = 0;
	string unit;
};

class EconomyDataManager
{
public:
	bool Load();

	const EconomyItemTemplate* GetItem(const string& itemId) const;
	const vector<VillageShopStockTemplate>* GetVillageShopStock(const string& villageId) const;
	const unordered_map<string, vector<VillageShopStockTemplate>>& GetAllVillageShopStock() const { return _shopStockByVillageId; }
	const VillageTradeTemplate* GetTrade(const string& itemId) const;
	int32 GetConfigValue(const string& key, int32 fallback = 0) const;

	// Food cannot be resold. Trade goods cannot be sold to their origin or to a
	// village that already stocks the same item. Other villages pay the configured
	// normal or premium margin over the origin price.
	bool TryGetTradeBuyPrice(const string& itemId, const string& villageId, int32& outPrice) const;

private:
	bool LoadItems();
	bool LoadVillageShopStock();
	bool LoadVillageTrade();
	bool LoadConfig();
	bool Validate() const;
	bool VillageSellsItem(const string& villageId, const string& itemId) const;
	string ResolveDataPath(const string& fileName) const;

private:
	bool _loaded = false;
	unordered_map<string, EconomyItemTemplate> _itemsById;
	unordered_map<string, vector<VillageShopStockTemplate>> _shopStockByVillageId;
	unordered_map<string, VillageTradeTemplate> _tradeByItemId;
	unordered_map<string, EconomyConfigTemplate> _configByKey;
};

extern EconomyDataManager GEconomyData;
