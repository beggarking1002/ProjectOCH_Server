#include "pch.h"
#include "BattleMapData.h"

#include "BattleCoordinate.h"

#include <fstream>
#include <regex>
#include <sstream>

BattleMapData GBattleMapData;

namespace
{
	bool ReadAllText(const string& path, string& text)
	{
		ifstream file(path);
		if (file.is_open() == false)
			return false;

		stringstream buffer;
		buffer << file.rdbuf();
		text = buffer.str();
		return true;
	}

	bool ExtractString(const string& json, const string& key, string& value)
	{
		const regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
		smatch match;
		if (regex_search(json, match, pattern) == false)
			return false;

		value = match[1].str();
		return true;
	}

	bool ExtractBlock(const string& json, const string& key, char openChar, char closeChar, string& block)
	{
		const string quotedKey = "\"" + key + "\"";
		const size_t keyPos = json.find(quotedKey);
		if (keyPos == string::npos)
			return false;

		const size_t openPos = json.find(openChar, keyPos + quotedKey.size());
		if (openPos == string::npos)
			return false;

		int32 depth = 0;
		for (size_t i = openPos; i < json.size(); ++i)
		{
			if (json[i] == openChar)
				++depth;
			else if (json[i] == closeChar && --depth == 0)
			{
				block = json.substr(openPos, i - openPos + 1);
				return true;
			}
		}

		return false;
	}
}

uint64 BattleMapData::MakeTileKey(const Protocol::AxialCoord& axial)
{
	return (static_cast<uint64>(static_cast<uint32>(axial.q())) << 32) |
		static_cast<uint32>(axial.r());
}

bool BattleMapData::LoadFromFile(const string& path)
{
	string json;
	if (ReadAllText(path, json) == false)
	{
		cout << "[BattleMapData] Failed to open battle walkmap: " << path << endl;
		return false;
	}

	string mapId;
	string walkableRangesBlock;
	if (ExtractString(json, "map_id", mapId) == false ||
		ExtractBlock(json, "walkable_ranges", '[', ']', walkableRangesBlock) == false)
	{
		cout << "[BattleMapData] Invalid battle walkmap schema: " << path << endl;
		return false;
	}

	vector<Protocol::AxialCoord> tiles;
	unordered_set<uint64> tileKeys;
	const regex rangePattern(R"(\{\s*"y"\s*:\s*(-?\d+)\s*,\s*"x_min"\s*:\s*(-?\d+)\s*,\s*"x_max"\s*:\s*(-?\d+)\s*\})");
	for (sregex_iterator it(walkableRangesBlock.begin(), walkableRangesBlock.end(), rangePattern), end; it != end; ++it)
	{
		const smatch& match = *it;
		const int32 cellY = stoi(match[1].str());
		int32 xMin = stoi(match[2].str());
		int32 xMax = stoi(match[3].str());
		if (xMax < xMin)
			swap(xMin, xMax);

		for (int32 cellX = xMin; cellX <= xMax; ++cellX)
		{
			Protocol::AxialCoord axial = BattleCoordinate::CellToAxial(cellX, cellY);
			if (tileKeys.insert(MakeTileKey(axial)).second)
				tiles.push_back(move(axial));
		}
	}

	if (tiles.empty())
	{
		cout << "[BattleMapData] Empty walkable_ranges: " << path << endl;
		return false;
	}

	sort(tiles.begin(), tiles.end(), [](const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs)
		{
			return lhs.r() != rhs.r() ? lhs.r() < rhs.r() : lhs.q() < rhs.q();
		});

	_mapId = move(mapId);
	_tiles = move(tiles);
	_tileKeys = move(tileKeys);
	_loaded = true;

	cout << "[BattleMapData] Loaded " << _mapId << " tiles=" << _tiles.size() << endl;
	return true;
}

bool BattleMapData::ContainsTile(const Protocol::AxialCoord& axial) const
{
	return _loaded && _tileKeys.contains(MakeTileKey(axial));
}
