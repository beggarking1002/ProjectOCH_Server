#include "pch.h"
#include "DatabaseManager.h"

#include <fstream>
#include <regex>
#include <sstream>

#include <mysql.h>

namespace
{
	constexpr const char* kDatabaseConfigPath = "C:\\ProjectOCH\\Server\\Data\\Database.json";

	bool ReadAllText(const string& path, string& outText)
	{
		ifstream file(path);
		if (!file.is_open())
			return false;
		stringstream buffer;
		buffer << file.rdbuf();
		outText = buffer.str();
		return true;
	}

	bool ExtractString(const string& json, const string& key, string& outValue)
	{
		const regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
		smatch match;
		if (!regex_search(json, match, pattern))
			return false;
		outValue = match[1].str();
		return true;
	}

	bool ExtractUInt(const string& json, const string& key, uint32& outValue)
	{
		const regex pattern("\\\"" + key + "\\\"\\s*:\\s*(\\d+)");
		smatch match;
		if (!regex_search(json, match, pattern))
			return false;
		outValue = static_cast<uint32>(stoul(match[1].str()));
		return true;
	}

	bool ExtractBool(const string& json, const string& key, bool& outValue)
	{
		const regex pattern("\\\"" + key + "\\\"\\s*:\\s*(true|false)");
		smatch match;
		if (!regex_search(json, match, pattern))
			return false;
		outValue = match[1].str() == "true";
		return true;
	}

	uint64 ToUInt64(const char* value)
	{
		return value != nullptr ? static_cast<uint64>(strtoull(value, nullptr, 10)) : 0;
	}

	int64 ToInt64(const char* value)
	{
		return value != nullptr ? static_cast<int64>(strtoll(value, nullptr, 10)) : 0;
	}
}

struct DatabaseManager::Impl
{
	using MysqlInitFn = decltype(&mysql_init);
	using MysqlOptionsFn = decltype(&mysql_options);
	using MysqlRealConnectFn = decltype(&mysql_real_connect);
	using MysqlCloseFn = decltype(&mysql_close);
	using MysqlPingFn = decltype(&mysql_ping);
	using MysqlSetCharacterSetFn = decltype(&mysql_set_character_set);
	using MysqlRealQueryFn = decltype(&mysql_real_query);
	using MysqlStoreResultFn = decltype(&mysql_store_result);
	using MysqlFetchRowFn = decltype(&mysql_fetch_row);
	using MysqlFreeResultFn = decltype(&mysql_free_result);
	using MysqlErrorFn = decltype(&mysql_error);
	using MysqlRealEscapeStringFn = decltype(&mysql_real_escape_string);
	using MysqlAutocommitFn = decltype(&mysql_autocommit);
	using MysqlCommitFn = decltype(&mysql_commit);
	using MysqlRollbackFn = decltype(&mysql_rollback);
	using MysqlInsertIdFn = decltype(&mysql_insert_id);

	HMODULE library = nullptr;
	MYSQL* connection = nullptr;
	MysqlInitFn mysqlInit = nullptr;
	MysqlOptionsFn mysqlOptions = nullptr;
	MysqlRealConnectFn mysqlRealConnect = nullptr;
	MysqlCloseFn mysqlClose = nullptr;
	MysqlPingFn mysqlPing = nullptr;
	MysqlSetCharacterSetFn mysqlSetCharacterSet = nullptr;
	MysqlRealQueryFn mysqlRealQuery = nullptr;
	MysqlStoreResultFn mysqlStoreResult = nullptr;
	MysqlFetchRowFn mysqlFetchRow = nullptr;
	MysqlFreeResultFn mysqlFreeResult = nullptr;
	MysqlErrorFn mysqlError = nullptr;
	MysqlRealEscapeStringFn mysqlRealEscapeString = nullptr;
	MysqlAutocommitFn mysqlAutocommit = nullptr;
	MysqlCommitFn mysqlCommit = nullptr;
	MysqlRollbackFn mysqlRollback = nullptr;
	MysqlInsertIdFn mysqlInsertId = nullptr;

	string host;
	string user;
	string password;
	string database;
	string charset = "utf8mb4";
	string libraryPath;
	uint32 port = 3306;

	~Impl()
	{
		if (connection != nullptr && mysqlClose != nullptr)
			mysqlClose(connection);
		if (library != nullptr)
			FreeLibrary(library);
	}
};

DatabaseManager GDatabase;

bool DatabaseManager::Initialize()
{
	lock_guard lock(_mutex);
	if (!_impl)
		_impl = make_unique<Impl>();
	ifstream configFile(kDatabaseConfigPath);
	if (!configFile.is_open())
	{
		cout << "[Database] Data/Database.json is absent. Persistence is disabled." << endl;
		return true;
	}
	if (!LoadConfig())
	{
		cout << "[Database] Database.json is invalid." << endl;
		return false;
	}
	if (!_enabled)
	{
		cout << "[Database] Persistence is disabled by configuration." << endl;
		return true;
	}
	if (!Connect() || !EnsureSchema())
	{
		Disconnect();
		return false;
	}
	cout << "[Database] MySQL persistence initialized database=" << _impl->database << endl;
	return true;
}

bool DatabaseManager::LoadConfig()
{
	string json;
	if (!ReadAllText(kDatabaseConfigPath, json))
		return false;

	bool enabled = false;
	if (!ExtractBool(json, "enabled", enabled))
	{
		cout << "[Database] Invalid Database.json: enabled is required." << endl;
		return false;
	}
	_enabled = enabled;
	if (!_enabled)
		return true;

	uint32 autosaveSeconds = 5;
	if (!ExtractString(json, "host", _impl->host) || !ExtractString(json, "user", _impl->user) ||
		!ExtractString(json, "password", _impl->password) || !ExtractString(json, "database", _impl->database) ||
		!ExtractString(json, "mysql_library_path", _impl->libraryPath))
	{
		cout << "[Database] Invalid Database.json: host, user, password, database and mysql_library_path are required." << endl;
		return false;
	}
	ExtractUInt(json, "port", _impl->port);
	ExtractString(json, "charset", _impl->charset);
	if (ExtractUInt(json, "autosave_seconds", autosaveSeconds))
		_autosaveIntervalMs = (max)(uint64{ 1000 }, static_cast<uint64>(autosaveSeconds) * 1000);
	return true;
}

bool DatabaseManager::Connect()
{
	_impl->library = LoadLibraryA(_impl->libraryPath.c_str());
	if (_impl->library == nullptr)
	{
		cout << "[Database] Failed to load MySQL client DLL: " << _impl->libraryPath << endl;
		return false;
	}

#define LOAD_MYSQL_FUNCTION(member, type, exportName) \
	_impl->member = reinterpret_cast<Impl::type##Fn>(GetProcAddress(_impl->library, exportName)); \
	if (_impl->member == nullptr) { cout << "[Database] Missing MySQL client export: " << exportName << endl; return false; }
	LOAD_MYSQL_FUNCTION(mysqlInit, MysqlInit, "mysql_init")
	LOAD_MYSQL_FUNCTION(mysqlOptions, MysqlOptions, "mysql_options")
	LOAD_MYSQL_FUNCTION(mysqlRealConnect, MysqlRealConnect, "mysql_real_connect")
	LOAD_MYSQL_FUNCTION(mysqlClose, MysqlClose, "mysql_close")
	LOAD_MYSQL_FUNCTION(mysqlPing, MysqlPing, "mysql_ping")
	LOAD_MYSQL_FUNCTION(mysqlSetCharacterSet, MysqlSetCharacterSet, "mysql_set_character_set")
	LOAD_MYSQL_FUNCTION(mysqlRealQuery, MysqlRealQuery, "mysql_real_query")
	LOAD_MYSQL_FUNCTION(mysqlStoreResult, MysqlStoreResult, "mysql_store_result")
	LOAD_MYSQL_FUNCTION(mysqlFetchRow, MysqlFetchRow, "mysql_fetch_row")
	LOAD_MYSQL_FUNCTION(mysqlFreeResult, MysqlFreeResult, "mysql_free_result")
	LOAD_MYSQL_FUNCTION(mysqlError, MysqlError, "mysql_error")
	LOAD_MYSQL_FUNCTION(mysqlRealEscapeString, MysqlRealEscapeString, "mysql_real_escape_string")
	LOAD_MYSQL_FUNCTION(mysqlAutocommit, MysqlAutocommit, "mysql_autocommit")
	LOAD_MYSQL_FUNCTION(mysqlCommit, MysqlCommit, "mysql_commit")
	LOAD_MYSQL_FUNCTION(mysqlRollback, MysqlRollback, "mysql_rollback")
	LOAD_MYSQL_FUNCTION(mysqlInsertId, MysqlInsertId, "mysql_insert_id")
#undef LOAD_MYSQL_FUNCTION

	_impl->connection = _impl->mysqlInit(nullptr);
	if (_impl->connection == nullptr)
	{
		cout << "[Database] mysql_init failed." << endl;
		return false;
	}
	const uint32 connectTimeoutSeconds = 5;
	_impl->mysqlOptions(_impl->connection, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeoutSeconds);
	if (_impl->mysqlRealConnect(_impl->connection, _impl->host.c_str(), _impl->user.c_str(), _impl->password.c_str(),
		_impl->database.c_str(), _impl->port, nullptr, 0) == nullptr)
	{
		cout << "[Database] MySQL connection failed: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}
	if (_impl->mysqlSetCharacterSet(_impl->connection, _impl->charset.c_str()) != 0)
	{
		cout << "[Database] Failed to set MySQL charset: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}
	return true;
}

bool DatabaseManager::EnsureSchema()
{
	return Execute(
		"CREATE TABLE IF NOT EXISTS accounts ("
		"account_id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY, email VARCHAR(320) NOT NULL DEFAULT '', "
		"display_name VARCHAR(128) NOT NULL DEFAULT 'Player', created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
		"last_login_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP) ENGINE=InnoDB AUTO_INCREMENT=1000000000000 DEFAULT CHARSET=utf8mb4") &&
		Execute(
		"CREATE TABLE IF NOT EXISTS account_identities ("
		"account_id BIGINT UNSIGNED NOT NULL, provider VARCHAR(32) NOT NULL, provider_subject VARCHAR(255) NOT NULL, "
		"created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY (provider, provider_subject), "
		"KEY idx_account_identities_account (account_id), CONSTRAINT fk_account_identity_account FOREIGN KEY (account_id) "
		"REFERENCES accounts(account_id) ON DELETE CASCADE) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4") &&
		Execute(
		"CREATE TABLE IF NOT EXISTS player_profiles ("
		"player_id BIGINT UNSIGNED NOT NULL PRIMARY KEY, gold INT NOT NULL, satiety INT NOT NULL, max_satiety INT NOT NULL, "
		"thirst INT NOT NULL, max_thirst INT NOT NULL, satiety_drain_numerator BIGINT NOT NULL, "
		"next_inventory_stack_id BIGINT UNSIGNED NOT NULL, next_acquired_sequence BIGINT UNSIGNED NOT NULL, "
		"created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP) "
		"ENGINE=InnoDB DEFAULT CHARSET=utf8mb4") &&
		Execute(
		"CREATE TABLE IF NOT EXISTS player_inventory_stacks ("
		"player_id BIGINT UNSIGNED NOT NULL, stack_id BIGINT UNSIGNED NOT NULL, item_id VARCHAR(64) NOT NULL, quantity INT NOT NULL, "
		"remaining_shelf_life_ms BIGINT NOT NULL, acquired_sequence BIGINT UNSIGNED NOT NULL, "
		"PRIMARY KEY (player_id, stack_id), CONSTRAINT fk_player_inventory_profile FOREIGN KEY (player_id) "
		"REFERENCES player_profiles(player_id) ON DELETE CASCADE) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4") &&
		Execute(
		"CREATE TABLE IF NOT EXISTS player_village_shop_stock ("
		"player_id BIGINT UNSIGNED NOT NULL, village_id VARCHAR(64) NOT NULL, item_id VARCHAR(64) NOT NULL, stock INT NOT NULL, "
		"stock_generation BIGINT UNSIGNED NOT NULL DEFAULT 0, updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP, "
		"PRIMARY KEY (player_id, village_id, item_id), CONSTRAINT fk_player_shop_profile FOREIGN KEY (player_id) "
		"REFERENCES player_profiles(player_id) ON DELETE CASCADE) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
}

bool DatabaseManager::FindOrCreateGoogleAccount(const string& googleSubject, const string& email,
	const string& displayName, uint64& outAccountId)
{
	lock_guard lock(_mutex);
	outAccountId = 0;
	if (!_enabled || _impl == nullptr || _impl->connection == nullptr || googleSubject.empty())
		return false;
	if (_impl->mysqlPing(_impl->connection) != 0)
		return false;

	const string escapedSubject = Escape(googleSubject);
	const string lookupSql = "SELECT account_id FROM account_identities WHERE provider='google' AND provider_subject='" +
		escapedSubject + "'";
	if (!Execute(lookupSql))
		return false;
	MYSQL_RES* result = _impl->mysqlStoreResult(_impl->connection);
	if (result == nullptr)
		return false;
	MYSQL_ROW row = _impl->mysqlFetchRow(result);
	if (row != nullptr)
		outAccountId = ToUInt64(row[0]);
	_impl->mysqlFreeResult(result);
	if (outAccountId != 0)
	{
		const string updateSql = "UPDATE accounts SET email='" + Escape(email) + "',display_name='" +
			Escape(displayName) + "',last_login_at=CURRENT_TIMESTAMP WHERE account_id=" + to_string(outAccountId);
		return Execute(updateSql);
	}

	if (_impl->mysqlAutocommit(_impl->connection, false) != 0)
		return false;
	const string accountSql = "INSERT INTO accounts (email,display_name,last_login_at) VALUES ('" +
		Escape(email) + "','" + Escape(displayName) + "',CURRENT_TIMESTAMP)";
	bool success = Execute(accountSql);
	if (success)
		outAccountId = static_cast<uint64>(_impl->mysqlInsertId(_impl->connection));
	if (success && outAccountId != 0)
	{
		const string identitySql = "INSERT INTO account_identities (account_id,provider,provider_subject) VALUES (" +
			to_string(outAccountId) + ",'google','" + escapedSubject + "')";
		success = Execute(identitySql);
	}
	if (success)
		success = _impl->mysqlCommit(_impl->connection) == 0;
	if (!success)
	{
		_impl->mysqlRollback(_impl->connection);
		outAccountId = 0;
	}
	_impl->mysqlAutocommit(_impl->connection, true);
	return success;
}

bool DatabaseManager::LoadPlayerEconomy(uint64 playerId, PersistentPlayerEconomyState& outState, bool& outFound)
{
	lock_guard lock(_mutex);
	outFound = false;
	if (!_enabled || _impl == nullptr || _impl->connection == nullptr)
		return true;
	if (_impl->mysqlPing(_impl->connection) != 0)
	{
		cout << "[Database] MySQL ping failed: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}

	const string profileSql = "SELECT gold,satiety,max_satiety,thirst,max_thirst,satiety_drain_numerator,"
		"next_inventory_stack_id,next_acquired_sequence FROM player_profiles WHERE player_id=" + to_string(playerId);
	if (!Execute(profileSql))
		return false;
	MYSQL_RES* profileResult = _impl->mysqlStoreResult(_impl->connection);
	if (profileResult == nullptr)
	{
		cout << "[Database] Profile query returned no result: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}
	MYSQL_ROW profile = _impl->mysqlFetchRow(profileResult);
	if (profile == nullptr)
	{
		_impl->mysqlFreeResult(profileResult);
		return true;
	}
	outFound = true;
	outState.gold = static_cast<int32>(ToInt64(profile[0]));
	outState.satiety = static_cast<int32>(ToInt64(profile[1]));
	outState.maxSatiety = static_cast<int32>(ToInt64(profile[2]));
	outState.thirst = static_cast<int32>(ToInt64(profile[3]));
	outState.maxThirst = static_cast<int32>(ToInt64(profile[4]));
	outState.satietyDrainNumerator = ToInt64(profile[5]);
	outState.nextInventoryStackId = ToUInt64(profile[6]);
	outState.nextAcquiredSequence = ToUInt64(profile[7]);
	_impl->mysqlFreeResult(profileResult);

	const string inventorySql = "SELECT stack_id,item_id,quantity,remaining_shelf_life_ms,acquired_sequence "
		"FROM player_inventory_stacks WHERE player_id=" + to_string(playerId) + " ORDER BY stack_id";
	if (!Execute(inventorySql))
		return false;
	MYSQL_RES* inventoryResult = _impl->mysqlStoreResult(_impl->connection);
	if (inventoryResult == nullptr)
	{
		cout << "[Database] Inventory query returned no result: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}
	outState.inventory.clear();
	while (MYSQL_ROW row = _impl->mysqlFetchRow(inventoryResult))
	{
		ExpeditionItemStackState stack;
		stack.stackId = ToUInt64(row[0]);
		stack.itemId = row[1] != nullptr ? row[1] : "";
		stack.quantity = static_cast<int32>(ToInt64(row[2]));
		stack.remainingShelfLifeMs = ToInt64(row[3]);
		stack.acquiredSequence = ToUInt64(row[4]);
		outState.inventory.push_back(move(stack));
	}
	_impl->mysqlFreeResult(inventoryResult);
	return true;
}

bool DatabaseManager::SavePlayerEconomy(uint64 playerId, const PersistentPlayerEconomyState& state)
{
	lock_guard lock(_mutex);
	if (!_enabled || _impl == nullptr || _impl->connection == nullptr)
		return true;
	if (_impl->mysqlPing(_impl->connection) != 0)
	{
		cout << "[Database] MySQL ping failed: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}
	if (_impl->mysqlAutocommit(_impl->connection, false) != 0)
		return false;

	const string profileSql = "INSERT INTO player_profiles (player_id,gold,satiety,max_satiety,thirst,max_thirst,satiety_drain_numerator,next_inventory_stack_id,next_acquired_sequence) VALUES (" +
		to_string(playerId) + "," + to_string(state.gold) + "," + to_string(state.satiety) + "," + to_string(state.maxSatiety) + "," +
		to_string(state.thirst) + "," + to_string(state.maxThirst) + "," + to_string(state.satietyDrainNumerator) + "," +
		to_string(state.nextInventoryStackId) + "," + to_string(state.nextAcquiredSequence) + ") ON DUPLICATE KEY UPDATE " +
		"gold=VALUES(gold),satiety=VALUES(satiety),max_satiety=VALUES(max_satiety),thirst=VALUES(thirst),max_thirst=VALUES(max_thirst)," +
		"satiety_drain_numerator=VALUES(satiety_drain_numerator),next_inventory_stack_id=VALUES(next_inventory_stack_id),next_acquired_sequence=VALUES(next_acquired_sequence)";
	bool success = Execute(profileSql) && Execute("DELETE FROM player_inventory_stacks WHERE player_id=" + to_string(playerId));
	for (const ExpeditionItemStackState& stack : state.inventory)
	{
		if (!success)
			break;
		const string inventorySql = "INSERT INTO player_inventory_stacks (player_id,stack_id,item_id,quantity,remaining_shelf_life_ms,acquired_sequence) VALUES (" +
			to_string(playerId) + "," + to_string(stack.stackId) + ",\'" + Escape(stack.itemId) + "\'," + to_string(stack.quantity) + "," +
			to_string(stack.remainingShelfLifeMs) + "," + to_string(stack.acquiredSequence) + ")";
		success = Execute(inventorySql);
	}
	if (success)
	{
		success = _impl->mysqlCommit(_impl->connection) == 0;
		if (!success)
			_impl->mysqlRollback(_impl->connection);
	}
	else
		_impl->mysqlRollback(_impl->connection);
	_impl->mysqlAutocommit(_impl->connection, true);
	if (!success)
		cout << "[Database] Failed to save player economy: " << _impl->mysqlError(_impl->connection) << endl;
	return success;
}

bool DatabaseManager::SavePlayerEconomyIfDue(const PlayerRef& player, uint64 nowMs)
{
	if (player == nullptr || !player->hasPersistentIdentity || !player->NeedsEconomyPersistence(nowMs, _autosaveIntervalMs))
		return true;
	if (!SavePlayerEconomy(player->objectInfo->object_id(), player->ExportEconomyState()))
		return false;
	player->MarkEconomyPersisted(nowMs);
	return true;
}

void DatabaseManager::SavePlayerEconomyOnDisconnect(const PlayerRef& player, uint64 nowMs)
{
	if (player == nullptr || !player->hasPersistentIdentity || !_enabled)
		return;
	if (SavePlayerEconomy(player->objectInfo->object_id(), player->ExportEconomyState()))
		player->MarkEconomyPersisted(nowMs);
}

bool DatabaseManager::Execute(const string& sql)
{
	if (_impl == nullptr || _impl->connection == nullptr || _impl->mysqlRealQuery(_impl->connection, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
	{
		if (_impl != nullptr && _impl->connection != nullptr)
			cout << "[Database] SQL failed: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}
	return true;
}

string DatabaseManager::Escape(const string& value) const
{
	if (_impl == nullptr || _impl->connection == nullptr || value.empty())
		return value;
	string escaped(value.size() * 2 + 1, '\0');
	const unsigned long length = _impl->mysqlRealEscapeString(_impl->connection, escaped.data(), value.c_str(), static_cast<unsigned long>(value.size()));
	escaped.resize(length);
	return escaped;
}

void DatabaseManager::Disconnect()
{
	if (_impl == nullptr)
		return;
	if (_impl->connection != nullptr && _impl->mysqlClose != nullptr)
	{
		_impl->mysqlClose(_impl->connection);
		_impl->connection = nullptr;
	}
	if (_impl->library != nullptr)
	{
		FreeLibrary(_impl->library);
		_impl->library = nullptr;
	}
}
