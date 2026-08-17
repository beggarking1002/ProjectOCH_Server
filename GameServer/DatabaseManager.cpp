#include "pch.h"
#include "DatabaseManager.h"

#include <fstream>
#include <filesystem>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <wincrypt.h>

#include <mysql.h>

namespace
{
	constexpr const char* kDatabaseConfigPath = "C:\\ProjectOCH\\Server\\Data\\Database.json";
	constexpr const char* kMigrationDirectoryName = "Database\\Migrations";

	struct MigrationFile
	{
		uint32 version = 0;
		string name;
		string sql;
		string checksum;
	};

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

	string Trim(string value)
	{
		const size_t first = value.find_first_not_of(" \t\r\n");
		if (first == string::npos)
			return {};
		const size_t last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	}

	filesystem::path ResolveMigrationDirectory()
	{
		array<wchar_t, 32768> executablePath{};
		const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
		if (length > 0 && length < executablePath.size())
		{
			const filesystem::path serverRoot = filesystem::path(executablePath.data()).parent_path().parent_path().parent_path();
			const filesystem::path candidate = serverRoot / kMigrationDirectoryName;
			if (filesystem::is_directory(candidate))
				return candidate;
		}

		return filesystem::current_path() / kMigrationDirectoryName;
	}

	bool ComputeSha256(const string& value, string& outChecksum)
	{
		HCRYPTPROV provider = 0;
		HCRYPTHASH hash = 0;
		if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
			!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash))
		{
			if (hash != 0) CryptDestroyHash(hash);
			if (provider != 0) CryptReleaseContext(provider, 0);
			return false;
		}

		bool success = CryptHashData(hash, reinterpret_cast<const BYTE*>(value.data()),
			static_cast<DWORD>(value.size()), 0) != FALSE;
		array<BYTE, 32> digest{};
		DWORD digestSize = static_cast<DWORD>(digest.size());
		if (success)
			success = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digestSize, 0) != FALSE;

		CryptDestroyHash(hash);
		CryptReleaseContext(provider, 0);
		if (!success)
			return false;

		ostringstream stream;
		stream << hex << setfill('0');
		for (DWORD i = 0; i < digestSize; ++i)
			stream << setw(2) << static_cast<int>(digest[i]);
		outChecksum = stream.str();
		return true;
	}

	bool SplitSqlStatements(const string& script, vector<string>& outStatements, string& outReason)
	{
		string current;
		char quote = '\0';
		bool lineComment = false;
		bool blockComment = false;

		for (size_t i = 0; i < script.size(); ++i)
		{
			const char ch = script[i];
			const char next = i + 1 < script.size() ? script[i + 1] : '\0';

			if (lineComment)
			{
				if (ch == '\n')
				{
					lineComment = false;
					current.push_back(' ');
				}
				continue;
			}
			if (blockComment)
			{
				if (ch == '*' && next == '/')
				{
					blockComment = false;
					current.push_back(' ');
					++i;
				}
				continue;
			}
			if (quote != '\0')
			{
				current.push_back(ch);
				if (ch == '\\' && next != '\0')
				{
					current.push_back(next);
					++i;
				}
				else if (ch == quote)
				{
					if (next == quote)
					{
						current.push_back(next);
						++i;
					}
					else
					{
						quote = '\0';
					}
				}
				continue;
			}

			if ((ch == '-' && next == '-' && (i + 2 >= script.size() || isspace(static_cast<unsigned char>(script[i + 2])))) || ch == '#')
			{
				lineComment = true;
				if (ch == '-') ++i;
				continue;
			}
			if (ch == '/' && next == '*')
			{
				blockComment = true;
				++i;
				continue;
			}
			if (ch == '\'' || ch == '"' || ch == '`')
			{
				quote = ch;
				current.push_back(ch);
				continue;
			}
			if (ch == ';')
			{
				string statement = Trim(move(current));
				current.clear();
				if (!statement.empty())
					outStatements.push_back(move(statement));
				continue;
			}
			current.push_back(ch);
		}

		if (quote != '\0' || blockComment)
		{
			outReason = "unterminated quote or block comment";
			return false;
		}
		string statement = Trim(move(current));
		if (!statement.empty())
			outStatements.push_back(move(statement));
		return true;
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
	if (!Connect() || !RunMigrations())
	{
		Disconnect();
		return false;
	}
	cout << "[Database] MySQL persistence initialized database=" << _impl->database
		<< " player_data_reset_allowed=" << (_allowPlayerDataReset ? 1 : 0) << endl;
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
	ExtractBool(json, "allow_player_data_reset", _allowPlayerDataReset);
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

bool DatabaseManager::EnsureMigrationTable()
{
	return Execute(
		"CREATE TABLE IF NOT EXISTS schema_migrations ("
		"version INT UNSIGNED NOT NULL PRIMARY KEY, name VARCHAR(255) NOT NULL, checksum CHAR(64) NOT NULL, "
		"applied_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
}

bool DatabaseManager::RunMigrations()
{
	const filesystem::path migrationDirectory = ResolveMigrationDirectory();
	if (!filesystem::is_directory(migrationDirectory))
	{
		cout << "[Database] Migration directory is unavailable: " << migrationDirectory.string() << endl;
		return false;
	}

	const regex filePattern(R"(^(\d+)_([A-Za-z0-9][A-Za-z0-9_-]*)\.sql$)");
	vector<MigrationFile> migrations;
	for (const filesystem::directory_entry& entry : filesystem::directory_iterator(migrationDirectory))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".sql")
			continue;

		const string fileName = entry.path().filename().string();
		smatch match;
		if (!regex_match(fileName, match, filePattern))
		{
			cout << "[Database] Invalid migration file name: " << fileName << endl;
			return false;
		}

		MigrationFile migration;
		try
		{
			const unsigned long parsedVersion = stoul(match[1].str());
			if (parsedVersion == 0 || parsedVersion > (numeric_limits<uint32>::max)())
				throw out_of_range("migration version");
			migration.version = static_cast<uint32>(parsedVersion);
		}
		catch (const exception&)
		{
			cout << "[Database] Invalid migration version: " << fileName << endl;
			return false;
		}
		migration.name = fileName;
		if (!ReadAllText(entry.path().string(), migration.sql) || !ComputeSha256(migration.sql, migration.checksum))
		{
			cout << "[Database] Failed to read or hash migration: " << fileName << endl;
			return false;
		}
		migrations.push_back(move(migration));
	}

	sort(migrations.begin(), migrations.end(), [](const MigrationFile& left, const MigrationFile& right)
		{
			return left.version < right.version;
		});
	if (migrations.empty())
	{
		cout << "[Database] No migration files were found." << endl;
		return false;
	}
	for (size_t i = 1; i < migrations.size(); ++i)
	{
		if (migrations[i - 1].version == migrations[i].version)
		{
			cout << "[Database] Duplicate migration version=" << migrations[i].version << endl;
			return false;
		}
		if (migrations[i].version != migrations[i - 1].version + 1)
		{
			cout << "[Database] Migration version gap after version=" << migrations[i - 1].version << endl;
			return false;
		}
	}
	if (migrations.front().version != 1)
	{
		cout << "[Database] Migration history must begin at version=1" << endl;
		return false;
	}

	if (!EnsureMigrationTable() || !Execute("SELECT version,name,checksum FROM schema_migrations ORDER BY version"))
		return false;
	MYSQL_RES* result = _impl->mysqlStoreResult(_impl->connection);
	if (result == nullptr)
	{
		cout << "[Database] Failed to read migration history: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}
	map<uint32, pair<string, string>> applied;
	while (MYSQL_ROW row = _impl->mysqlFetchRow(result))
	{
		const uint64 version = ToUInt64(row[0]);
		if (version == 0 || version > (numeric_limits<uint32>::max)())
		{
			_impl->mysqlFreeResult(result);
			cout << "[Database] Migration history contains an invalid version." << endl;
			return false;
		}
		applied[static_cast<uint32>(version)] = {
			row[1] != nullptr ? row[1] : "",
			row[2] != nullptr ? row[2] : ""
		};
	}
	_impl->mysqlFreeResult(result);
	uint32 expectedAppliedVersion = 1;
	for (const auto& [version, history] : applied)
	{
		if (version != expectedAppliedVersion++)
		{
			cout << "[Database] Applied migration history has a gap before version=" << version << endl;
			return false;
		}
	}

	for (const auto& [version, history] : applied)
	{
		const auto migrationIt = find_if(migrations.begin(), migrations.end(), [version](const MigrationFile& migration)
			{
				return migration.version == version;
			});
		if (migrationIt == migrations.end())
		{
			cout << "[Database] Applied migration file is missing version=" << version << endl;
			return false;
		}
		if (history.first != migrationIt->name || history.second != migrationIt->checksum)
		{
			cout << "[Database] Applied migration was modified version=" << version
				<< " expected_name=" << history.first << " actual_name=" << migrationIt->name << endl;
			return false;
		}
	}

	uint32 currentVersion = applied.empty() ? 0 : applied.rbegin()->first;
	for (const MigrationFile& migration : migrations)
	{
		if (applied.contains(migration.version))
			continue;

		vector<string> statements;
		string splitReason;
		if (!SplitSqlStatements(migration.sql, statements, splitReason) || statements.empty())
		{
			cout << "[Database] Invalid migration version=" << migration.version
				<< " reason=" << (splitReason.empty() ? "no statements" : splitReason) << endl;
			return false;
		}

		cout << "[Database] Applying migration version=" << migration.version
			<< " name=" << migration.name << endl;
		for (const string& statement : statements)
		{
			if (!Execute(statement))
			{
				cout << "[Database] Migration failed version=" << migration.version << endl;
				return false;
			}
		}
		const string historySql = "INSERT INTO schema_migrations (version,name,checksum) VALUES (" +
			to_string(migration.version) + ",'" + Escape(migration.name) + "','" + migration.checksum + "')";
		if (!Execute(historySql))
			return false;
		currentVersion = migration.version;
		cout << "[Database] Applied migration version=" << migration.version << endl;
	}

	cout << "[Database] Migrations current_version=" << currentVersion << endl;
	return true;
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

	const string profileSql = "SELECT gold,fame,field_pawn_class,satiety,max_satiety,happiness,max_happiness,thirst,max_thirst,"
		"satiety_drain_numerator,happiness_drain_numerator,thirst_drain_numerator,next_inventory_stack_id,next_acquired_sequence "
		"FROM player_profiles WHERE player_id=" + to_string(playerId);
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
	outState.fame = static_cast<int32>(ToInt64(profile[1]));
	outState.fieldPawnClass = static_cast<Protocol::PawnClass>(ToInt64(profile[2]));
	outState.satiety = static_cast<int32>(ToInt64(profile[3]));
	outState.maxSatiety = static_cast<int32>(ToInt64(profile[4]));
	outState.happiness = static_cast<int32>(ToInt64(profile[5]));
	outState.maxHappiness = static_cast<int32>(ToInt64(profile[6]));
	outState.thirst = static_cast<int32>(ToInt64(profile[7]));
	outState.maxThirst = static_cast<int32>(ToInt64(profile[8]));
	outState.satietyDrainNumerator = ToInt64(profile[9]);
	outState.happinessDrainNumerator = ToInt64(profile[10]);
	outState.thirstDrainNumerator = ToInt64(profile[11]);
	outState.nextInventoryStackId = ToUInt64(profile[12]);
	outState.nextAcquiredSequence = ToUInt64(profile[13]);
	_impl->mysqlFreeResult(profileResult);

	const string inventorySql = "SELECT stack_id,item_id,quantity,water_charge,remaining_shelf_life_ms,acquired_sequence "
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
		stack.waterCharge = static_cast<int32>(ToInt64(row[3]));
		stack.remainingShelfLifeMs = ToInt64(row[4]);
		stack.acquiredSequence = ToUInt64(row[5]);
		outState.inventory.push_back(move(stack));
	}
	_impl->mysqlFreeResult(inventoryResult);

	const string shopStateSql = "SELECT active_elapsed_ms,stock_generation FROM player_shop_states WHERE player_id=" +
		to_string(playerId);
	if (!Execute(shopStateSql))
		return false;
	MYSQL_RES* shopStateResult = _impl->mysqlStoreResult(_impl->connection);
	if (shopStateResult == nullptr)
	{
		cout << "[Database] Shop state query returned no result: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}
	if (MYSQL_ROW shopStateRow = _impl->mysqlFetchRow(shopStateResult))
	{
		outState.shopRestockElapsedMs = ToUInt64(shopStateRow[0]);
		outState.shopStockGeneration = (max)(uint64{ 1 }, ToUInt64(shopStateRow[1]));
	}
	_impl->mysqlFreeResult(shopStateResult);

	const string shopStockSql = "SELECT village_id,item_id,stock,stock_generation FROM player_village_shop_stock WHERE player_id=" +
		to_string(playerId) + " ORDER BY village_id,item_id";
	if (!Execute(shopStockSql))
		return false;
	MYSQL_RES* shopStockResult = _impl->mysqlStoreResult(_impl->connection);
	if (shopStockResult == nullptr)
	{
		cout << "[Database] Shop stock query returned no result: " << _impl->mysqlError(_impl->connection) << endl;
		return false;
	}
	outState.shopStock.clear();
	while (MYSQL_ROW row = _impl->mysqlFetchRow(shopStockResult))
	{
		PlayerVillageShopStockState stock;
		stock.villageId = row[0] != nullptr ? row[0] : "";
		stock.itemId = row[1] != nullptr ? row[1] : "";
		stock.stock = static_cast<int32>(ToInt64(row[2]));
		stock.stockGeneration = (max)(uint64{ 1 }, ToUInt64(row[3]));
		outState.shopStock.push_back(move(stock));
	}
	_impl->mysqlFreeResult(shopStockResult);

	const string questSql = "SELECT quest_id,template_id,status,display_name,description,category,start_village_id,completion_village_id,"
		"prerequisite_template_id,board_slot,accepted_at_ms,ready_at_ms,completed_at_ms FROM player_quest_instances WHERE player_id=" +
		to_string(playerId) + " ORDER BY quest_id";
	if (!Execute(questSql))
		return false;
	MYSQL_RES* questResult = _impl->mysqlStoreResult(_impl->connection);
	if (questResult == nullptr)
		return false;
	outState.quests.clear();
	while (MYSQL_ROW row = _impl->mysqlFetchRow(questResult))
	{
		PlayerQuestState quest;
		quest.questId = row[0] != nullptr ? row[0] : "";
		quest.templateId = row[1] != nullptr ? row[1] : "";
		const string status = row[2] != nullptr ? row[2] : "AVAILABLE";
		quest.status = status == "COMPLETED" ? PlayerQuestStatus::Completed :
			(status == "READY" ? PlayerQuestStatus::Ready :
				(status == "ACTIVE" ? PlayerQuestStatus::Active :
					(status == "ABANDONED" ? PlayerQuestStatus::Abandoned : PlayerQuestStatus::Available)));
		quest.displayName = row[3] != nullptr ? row[3] : "";
		quest.description = row[4] != nullptr ? row[4] : "";
		quest.category = row[5] != nullptr ? row[5] : "";
		quest.startVillageId = row[6] != nullptr ? row[6] : "";
		quest.completionVillageId = row[7] != nullptr ? row[7] : "";
		quest.prerequisiteTemplateId = row[8] != nullptr ? row[8] : "";
		quest.boardSlot = static_cast<int32>(ToInt64(row[9]));
		quest.acceptedAtMs = ToUInt64(row[10]);
		quest.readyAtMs = ToUInt64(row[11]);
		quest.completedAtMs = ToUInt64(row[12]);
		outState.quests.push_back(move(quest));
	}
	_impl->mysqlFreeResult(questResult);

	const string objectiveSql = "SELECT quest_id,objective_index,objective_type,target_village_id,target_item_id,required_count,progress "
		"FROM player_quest_instance_objectives WHERE player_id=" +
		to_string(playerId) + " ORDER BY quest_id,objective_index";
	if (!Execute(objectiveSql))
		return false;
	MYSQL_RES* objectiveResult = _impl->mysqlStoreResult(_impl->connection);
	if (objectiveResult == nullptr)
		return false;
	while (MYSQL_ROW row = _impl->mysqlFetchRow(objectiveResult))
	{
		const string questId = row[0] != nullptr ? row[0] : "";
		auto quest = find_if(outState.quests.begin(), outState.quests.end(), [&questId](const PlayerQuestState& state)
			{ return state.questId == questId; });
		if (quest == outState.quests.end())
			continue;
		PlayerQuestObjectiveState objective;
		objective.objectiveIndex = static_cast<uint32>(ToUInt64(row[1]));
		objective.objectiveType = row[2] != nullptr ? row[2] : "";
		objective.targetVillageId = row[3] != nullptr ? row[3] : "";
		objective.targetItemId = row[4] != nullptr ? row[4] : "";
		objective.requiredCount = static_cast<int32>(ToInt64(row[5]));
		objective.progress = static_cast<int32>(ToInt64(row[6]));
		quest->objectives.push_back(objective);
	}
	_impl->mysqlFreeResult(objectiveResult);

	const string rewardSql = "SELECT quest_id,reward_index,reward_type,target_id,amount "
		"FROM player_quest_instance_rewards WHERE player_id=" + to_string(playerId) + " ORDER BY quest_id,reward_index";
	if (!Execute(rewardSql))
		return false;
	MYSQL_RES* rewardResult = _impl->mysqlStoreResult(_impl->connection);
	if (rewardResult == nullptr)
		return false;
	while (MYSQL_ROW row = _impl->mysqlFetchRow(rewardResult))
	{
		const string questId = row[0] != nullptr ? row[0] : "";
		auto quest = find_if(outState.quests.begin(), outState.quests.end(), [&questId](const PlayerQuestState& state)
			{ return state.questId == questId; });
		if (quest == outState.quests.end())
			continue;
		PlayerQuestRewardState reward;
		reward.rewardIndex = static_cast<uint32>(ToUInt64(row[1]));
		reward.rewardType = row[2] != nullptr ? row[2] : "";
		reward.targetId = row[3] != nullptr ? row[3] : "";
		reward.amount = static_cast<int32>(ToInt64(row[4]));
		quest->rewards.push_back(move(reward));
	}
	_impl->mysqlFreeResult(rewardResult);
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

	bool success = WritePlayerEconomyState(playerId, state);
	if (success)
		success = _impl->mysqlCommit(_impl->connection) == 0;
	if (!success)
		_impl->mysqlRollback(_impl->connection);
	_impl->mysqlAutocommit(_impl->connection, true);
	if (!success)
		cout << "[Database] Failed to save player economy: " << _impl->mysqlError(_impl->connection) << endl;
	return success;
}

bool DatabaseManager::SavePlayerEconomiesAtomically(uint64 firstPlayerId, const PersistentPlayerEconomyState& firstState,
	uint64 secondPlayerId, const PersistentPlayerEconomyState& secondState)
{
	if (firstPlayerId == 0 || secondPlayerId == 0 || firstPlayerId == secondPlayerId)
		return false;

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

	bool success = WritePlayerEconomyState(firstPlayerId, firstState) &&
		WritePlayerEconomyState(secondPlayerId, secondState);
	if (success)
		success = _impl->mysqlCommit(_impl->connection) == 0;
	if (!success)
		_impl->mysqlRollback(_impl->connection);
	_impl->mysqlAutocommit(_impl->connection, true);
	if (!success)
		cout << "[Database] Failed to save two player economies atomically: " << _impl->mysqlError(_impl->connection) << endl;
	return success;
}

bool DatabaseManager::WritePlayerEconomyState(uint64 playerId, const PersistentPlayerEconomyState& state)
{

	const string profileSql = "INSERT INTO player_profiles (player_id,gold,fame,field_pawn_class,satiety,max_satiety,happiness,max_happiness,thirst,max_thirst,"
		"satiety_drain_numerator,happiness_drain_numerator,thirst_drain_numerator,next_inventory_stack_id,next_acquired_sequence) VALUES (" +
		to_string(playerId) + "," + to_string(state.gold) + "," + to_string(state.fame) + "," + to_string(static_cast<int32>(state.fieldPawnClass)) + "," + to_string(state.satiety) + "," + to_string(state.maxSatiety) + "," +
		to_string(state.happiness) + "," + to_string(state.maxHappiness) + "," + to_string(state.thirst) + "," + to_string(state.maxThirst) + "," +
		to_string(state.satietyDrainNumerator) + "," + to_string(state.happinessDrainNumerator) + "," + to_string(state.thirstDrainNumerator) + "," +
		to_string(state.nextInventoryStackId) + "," + to_string(state.nextAcquiredSequence) + ") ON DUPLICATE KEY UPDATE " +
		"gold=VALUES(gold),fame=VALUES(fame),field_pawn_class=VALUES(field_pawn_class),satiety=VALUES(satiety),max_satiety=VALUES(max_satiety)," +
		"happiness=VALUES(happiness),max_happiness=VALUES(max_happiness),thirst=VALUES(thirst),max_thirst=VALUES(max_thirst)," +
		"satiety_drain_numerator=VALUES(satiety_drain_numerator),happiness_drain_numerator=VALUES(happiness_drain_numerator)," +
		"thirst_drain_numerator=VALUES(thirst_drain_numerator),next_inventory_stack_id=VALUES(next_inventory_stack_id),next_acquired_sequence=VALUES(next_acquired_sequence)";
	const string shopStateSql = "INSERT INTO player_shop_states (player_id,active_elapsed_ms,stock_generation) VALUES (" +
		to_string(playerId) + "," + to_string(state.shopRestockElapsedMs) + "," +
		to_string((max)(uint64{ 1 }, state.shopStockGeneration)) + ") ON DUPLICATE KEY UPDATE " +
		"active_elapsed_ms=VALUES(active_elapsed_ms),stock_generation=VALUES(stock_generation)";
	bool success = Execute(profileSql) && Execute(shopStateSql) &&
		Execute("DELETE FROM player_inventory_stacks WHERE player_id=" + to_string(playerId));
	for (const ExpeditionItemStackState& stack : state.inventory)
	{
		if (!success)
			break;
		const string inventorySql = "INSERT INTO player_inventory_stacks (player_id,stack_id,item_id,quantity,water_charge,remaining_shelf_life_ms,acquired_sequence) VALUES (" +
			to_string(playerId) + "," + to_string(stack.stackId) + ",\'" + Escape(stack.itemId) + "\'," + to_string(stack.quantity) + "," +
			to_string(stack.waterCharge) + "," + to_string(stack.remainingShelfLifeMs) + "," + to_string(stack.acquiredSequence) + ")";
		success = Execute(inventorySql);
	}
	if (success)
		success = Execute("DELETE FROM player_village_shop_stock WHERE player_id=" + to_string(playerId));
	for (const PlayerVillageShopStockState& stock : state.shopStock)
	{
		if (!success)
			break;
		if (stock.villageId.empty() || stock.itemId.empty() || stock.stock < 0)
		{
			success = false;
			break;
		}
		const string stockSql = "INSERT INTO player_village_shop_stock (player_id,village_id,item_id,stock,stock_generation) VALUES (" +
			to_string(playerId) + ",'" + Escape(stock.villageId) + "','" + Escape(stock.itemId) + "'," +
			to_string(stock.stock) + "," + to_string((max)(uint64{ 1 }, stock.stockGeneration)) + ")";
		success = Execute(stockSql);
	}
	if (success)
		success = Execute("DELETE FROM player_quest_instances WHERE player_id=" + to_string(playerId));
	for (const PlayerQuestState& quest : state.quests)
	{
		if (!success)
			break;
		if (quest.questId.empty())
		{
			success = false;
			break;
		}
		const string status = quest.status == PlayerQuestStatus::Completed ? "COMPLETED" :
			(quest.status == PlayerQuestStatus::Ready ? "READY" :
				(quest.status == PlayerQuestStatus::Active ? "ACTIVE" :
					(quest.status == PlayerQuestStatus::Abandoned ? "ABANDONED" : "AVAILABLE")));
		const string playerQuestSql = "INSERT INTO player_quest_instances (player_id,quest_id,template_id,status,display_name,description,category,"
			"start_village_id,completion_village_id,prerequisite_template_id,board_slot,accepted_at_ms,ready_at_ms,completed_at_ms) VALUES (" +
			to_string(playerId) + ",'" + Escape(quest.questId) + "','" + Escape(quest.templateId) + "','" + status + "','" +
			Escape(quest.displayName) + "','" + Escape(quest.description) + "','" + Escape(quest.category) + "','" +
			Escape(quest.startVillageId) + "','" + Escape(quest.completionVillageId) + "','" +
			Escape(quest.prerequisiteTemplateId) + "'," + to_string(quest.boardSlot) + "," + to_string(quest.acceptedAtMs) + "," +
			to_string(quest.readyAtMs) + "," + to_string(quest.completedAtMs) + ")";
		success = Execute(playerQuestSql);
		for (const PlayerQuestObjectiveState& objective : quest.objectives)
		{
			if (!success || objective.objectiveIndex == 0 || objective.requiredCount <= 0 || objective.progress < 0)
			{
				success = false;
				break;
			}
			const string playerObjectiveSql = "INSERT INTO player_quest_instance_objectives (player_id,quest_id,objective_index,objective_type,"
				"target_village_id,target_item_id,required_count,progress) VALUES (" + to_string(playerId) + ",'" +
				Escape(quest.questId) + "'," + to_string(objective.objectiveIndex) + ",'" + Escape(objective.objectiveType) + "','" +
				Escape(objective.targetVillageId) + "','" + Escape(objective.targetItemId) + "'," +
				to_string(objective.requiredCount) + "," + to_string(objective.progress) + ")";
			success = Execute(playerObjectiveSql);
		}
		for (const PlayerQuestRewardState& reward : quest.rewards)
		{
			if (!success || reward.rewardIndex == 0 || reward.amount <= 0)
			{
				success = false;
				break;
			}
			const string playerRewardSql = "INSERT INTO player_quest_instance_rewards (player_id,quest_id,reward_index,reward_type,target_id,amount) VALUES (" +
				to_string(playerId) + ",'" + Escape(quest.questId) + "'," + to_string(reward.rewardIndex) + ",'" +
				Escape(reward.rewardType) + "','" + Escape(reward.targetId) + "'," + to_string(reward.amount) + ")";
			success = Execute(playerRewardSql);
		}
	}
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
