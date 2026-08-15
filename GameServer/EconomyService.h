#pragma once

class Player;

class EconomyService
{
public:
	bool Initialize(uint64 nowMs);
	void UpdateTick(uint64 nowMs);
	void SendExpeditionState(const PlayerRef& player,
		const vector<string>& autoConsumedItemIds = {}, const vector<string>& expiredItemIds = {}) const;
	void HandleShopOpen(GameSessionRef session, PlayerRef player, const string& villageId);
	void HandleShopBuy(GameSessionRef session, PlayerRef player, const string& villageId, const string& itemId, int32 quantity);
	void HandleShopSell(GameSessionRef session, PlayerRef player, const string& villageId, uint64 stackId, int32 quantity);

private:
	bool ValidateShopAccess(const PlayerRef& player, const string& villageId, string& reason) const;
	void ResetAllStock();
	void SendShopState(GameSessionRef session, const PlayerRef& player, const string& villageId,
		const string& action, bool success, const string& reason,
		const vector<string>& autoConsumedItemIds = {}, const vector<string>& expiredItemIds = {}) const;
	int32* FindRuntimeStock(const string& villageId, const string& itemId);
	uint32 GetStockResetRemainingSeconds(uint64 nowMs) const;

private:
	bool _initialized = false;
	uint64 _stockResetIntervalMs = 0;
	uint64 _nextStockResetMs = 0;
	unordered_map<string, unordered_map<string, int32>> _currentStockByVillageId;
};

extern EconomyService GEconomyService;
