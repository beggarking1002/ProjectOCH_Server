#include "pch.h"
#include "VillageDataManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

VillageDataManager GVillageData;

namespace
{
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

	bool ToBool(const string& value)
	{
		string normalized = Trim(value);
		transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch)
			{
				return static_cast<char>(toupper(ch));
			});
		return normalized == "1" || normalized == "TRUE" || normalized == "YES";
	}
}

bool VillageDataManager::Load()
{
	if (_loaded)
		return true;

	const string path = ResolveDataPath("Village.csv");
	ifstream file(path);
	if (file.is_open() == false)
	{
		cout << "[VillageDataManager] Failed to read Village.csv: " << path << endl;
		return false;
	}

	string line;
	if (!getline(file, line))
	{
		cout << "[VillageDataManager] Village.csv is empty: " << path << endl;
		return false;
	}

	const vector<string> header = ParseCsvLine(line);
	unordered_map<string, size_t> columns;
	for (size_t index = 0; index < header.size(); index++)
		columns[header[index]] = index;

	const array<string, 7> requiredColumns =
	{
		"VillageId", "Name", "Description", "ArtworkAddress", "ShopEnabled", "InnEnabled", "QuestEnabled"
	};
	for (const string& column : requiredColumns)
	{
		if (columns.find(column) == columns.end())
		{
			cout << "[VillageDataManager] Missing column " << column << " in " << path << endl;
			return false;
		}
	}

	// The second row documents field types and is intentionally not a village record.
	getline(file, line);

	_villagesById.clear();
	int32 lineNumber = 2;
	while (getline(file, line))
	{
		lineNumber++;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (Trim(line).empty())
			continue;

		const vector<string> row = ParseCsvLine(line);
		auto cell = [&row, &columns](const string& column) -> string
			{
				const size_t index = columns.at(column);
				return index < row.size() ? row[index] : "";
			};

		VillageTemplate village;
		village.villageId = cell("VillageId");
		village.name = cell("Name");
		village.description = cell("Description");
		village.artworkAddress = cell("ArtworkAddress");
		village.shopEnabled = ToBool(cell("ShopEnabled"));
		village.innEnabled = ToBool(cell("InnEnabled"));
		village.questEnabled = ToBool(cell("QuestEnabled"));

		if (village.villageId.empty() || village.name.empty())
		{
			cout << "[VillageDataManager] Invalid village row " << lineNumber << endl;
			return false;
		}
		if (_villagesById.emplace(village.villageId, move(village)).second == false)
		{
			cout << "[VillageDataManager] Duplicate VillageId at row " << lineNumber << endl;
			return false;
		}
	}

	_loaded = _villagesById.empty() == false;
	cout << "[VillageDataManager] Load " << (_loaded ? "success" : "failed")
		<< " villages=" << _villagesById.size() << endl;
	return _loaded;
}

const VillageTemplate* VillageDataManager::GetVillage(const string& villageId) const
{
	auto it = _villagesById.find(villageId);
	return it != _villagesById.end() ? &it->second : nullptr;
}

string VillageDataManager::ResolveDataPath(const string& fileName) const
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
