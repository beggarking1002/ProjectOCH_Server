#pragma once

#include "Player.h"

class DatabaseManager
{
public:
	// Data/Database.json is intentionally untracked. When absent, the local
	// server keeps its existing in-memory behavior; enabled=true must connect.
	bool Initialize();
	bool IsEnabled() const { return _enabled; }

	bool FindOrCreateGoogleAccount(const string& googleSubject, const string& email,
		const string& displayName, uint64& outAccountId);
	bool LoadPlayerEconomy(uint64 playerId, PersistentPlayerEconomyState& outState, bool& outFound);
	bool SavePlayerEconomy(uint64 playerId, const PersistentPlayerEconomyState& state);
	bool SavePlayerEconomyIfDue(const PlayerRef& player, uint64 nowMs);
	void SavePlayerEconomyOnDisconnect(const PlayerRef& player, uint64 nowMs);

private:
	bool LoadConfig();
	bool Connect();
	bool RunMigrations();
	bool EnsureMigrationTable();
	bool Execute(const string& sql);
	string Escape(const string& value) const;
	void Disconnect();

private:
	struct Impl;
	unique_ptr<Impl> _impl;
	bool _enabled = false;
	uint64 _autosaveIntervalMs = 5000;
	mutable mutex _mutex;
};

extern DatabaseManager GDatabase;
