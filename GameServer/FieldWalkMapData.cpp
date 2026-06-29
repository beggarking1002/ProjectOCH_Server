#include "pch.h"
#include "FieldWalkMapData.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <regex>
#include <sstream>

FieldWalkMapData GFieldWalkMapData;

namespace
{
	constexpr double kHexRowStride = 0.75;

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
		const regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
		smatch match;
		if (regex_search(json, match, pattern) == false)
			return false;

		value = match[1].str();
		return true;
	}

	bool ExtractInt(const string& json, const string& key, int32& value)
	{
		const regex pattern("\"" + key + "\"\\s*:\\s*(-?\\d+)");
		smatch match;
		if (regex_search(json, match, pattern) == false)
			return false;

		value = stoi(match[1].str());
		return true;
	}

	bool ExtractDouble(const string& json, const string& key, double& value)
	{
		const regex pattern("\"" + key + "\"\\s*:\\s*(-?(?:\\d+)(?:\\.\\d+)?)");
		smatch match;
		if (regex_search(json, match, pattern) == false)
			return false;

		value = stod(match[1].str());
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
		for (size_t i = openPos; i < json.size(); i++)
		{
			if (json[i] == openChar)
				depth++;
			else if (json[i] == closeChar)
			{
				depth--;
				if (depth == 0)
				{
					block = json.substr(openPos, i - openPos + 1);
					return true;
				}
			}
		}

		return false;
	}

	bool IsOddRow(int32 cellY)
	{
		return (cellY & 1) != 0;
	}
}

bool FieldWalkMapData::LoadFromFile(const string& path)
{
	string json;
	if (ReadAllText(path, json) == false)
	{
		cout << "[FieldWalkMapData] Failed to open walkmap: " << path << endl;
		return false;
	}

	string mapId;
	int32 fixedPointScale = 0;
	string cellSizeBlock;
	string originWorldBlock;
	string walkableRangesBlock;

	if (ExtractString(json, "map_id", mapId) == false ||
		ExtractInt(json, "fixed_point_scale", fixedPointScale) == false ||
		ExtractBlock(json, "cell_size", '{', '}', cellSizeBlock) == false ||
		ExtractBlock(json, "origin_world", '{', '}', originWorldBlock) == false ||
		ExtractBlock(json, "walkable_ranges", '[', ']', walkableRangesBlock) == false)
	{
		cout << "[FieldWalkMapData] Invalid walkmap schema: " << path << endl;
		return false;
	}

	double cellSizeX = 0.0;
	double cellSizeY = 0.0;
	double originWorldX = 0.0;
	double originWorldY = 0.0;

	if (ExtractDouble(cellSizeBlock, "x", cellSizeX) == false ||
		ExtractDouble(cellSizeBlock, "y", cellSizeY) == false ||
		ExtractDouble(originWorldBlock, "x", originWorldX) == false ||
		ExtractDouble(originWorldBlock, "y", originWorldY) == false ||
		fixedPointScale <= 0 ||
		cellSizeX <= 0.0 ||
		cellSizeY <= 0.0)
	{
		cout << "[FieldWalkMapData] Invalid walkmap values: " << path << endl;
		return false;
	}

	unordered_map<int32, vector<Range>> rangesByY;
	const regex rangePattern(R"(\{\s*"y"\s*:\s*(-?\d+)\s*,\s*"x_min"\s*:\s*(-?\d+)\s*,\s*"x_max"\s*:\s*(-?\d+)\s*\})");

	for (sregex_iterator it(walkableRangesBlock.begin(), walkableRangesBlock.end(), rangePattern), end; it != end; ++it)
	{
		const smatch& match = *it;
		const int32 y = stoi(match[1].str());
		Range range;
		range.xMin = stoi(match[2].str());
		range.xMax = stoi(match[3].str());
		if (range.xMax < range.xMin)
			swap(range.xMin, range.xMax);

		rangesByY[y].push_back(range);
	}

	if (rangesByY.empty())
	{
		cout << "[FieldWalkMapData] Empty walkable_ranges: " << path << endl;
		return false;
	}

	for (auto& item : rangesByY)
	{
		sort(item.second.begin(), item.second.end(), [](const Range& lhs, const Range& rhs)
			{
				return lhs.xMin < rhs.xMin;
			});
	}

	_mapId = mapId;
	_fixedPointScale = fixedPointScale;
	_cellSizeX = cellSizeX;
	_cellSizeY = cellSizeY;
	_originWorldX = originWorldX;
	_originWorldY = originWorldY;
	_walkableRanges = move(rangesByY);
	_loaded = true;

	cout << "[FieldWalkMapData] Loaded " << _mapId
		<< " rows=" << _walkableRanges.size()
		<< " fixed_point_scale=" << _fixedPointScale
		<< " cell_size=(" << _cellSizeX << ", " << _cellSizeY << ")" << endl;

	return true;
}

bool FieldWalkMapData::IsWalkableFixed(const Protocol::Vec2Fixed& position) const
{
	int32 cellX = 0;
	int32 cellY = 0;
	return IsWalkableFixed(position, cellX, cellY);
}

bool FieldWalkMapData::IsWalkableFixed(const Protocol::Vec2Fixed& position, int32& cellX, int32& cellY) const
{
	if (_loaded == false)
		return false;

	FixedToCell(position, cellX, cellY);
	return IsWalkableCell(cellX, cellY);
}

bool FieldWalkMapData::TryGetRandomWalkablePosition(Protocol::Vec2Fixed& position) const
{
	if (_loaded == false || _walkableRanges.empty())
		return false;

	static thread_local mt19937 generator{ random_device{}() };

	vector<int32> rows;
	rows.reserve(_walkableRanges.size());
	for (const auto& item : _walkableRanges)
		rows.push_back(item.first);

	uniform_int_distribution<size_t> rowDist(0, rows.size() - 1);
	const int32 cellY = rows[rowDist(generator)];
	const vector<Range>& ranges = _walkableRanges.at(cellY);

	uniform_int_distribution<size_t> rangeDist(0, ranges.size() - 1);
	const Range& range = ranges[rangeDist(generator)];

	uniform_int_distribution<int32> cellXDist(range.xMin, range.xMax);
	const int32 cellX = cellXDist(generator);

	CellToFixed(cellX, cellY, position);
	return true;
}

bool FieldWalkMapData::IsWalkableCell(int32 cellX, int32 cellY) const
{
	auto findIt = _walkableRanges.find(cellY);
	if (findIt == _walkableRanges.end())
		return false;

	for (const Range& range : findIt->second)
	{
		if (cellX >= range.xMin && cellX <= range.xMax)
			return true;
	}

	return false;
}

void FieldWalkMapData::FixedToCell(const Protocol::Vec2Fixed& position, int32& cellX, int32& cellY) const
{
	const double worldX = static_cast<double>(position.x()) / _fixedPointScale;
	const double worldY = static_cast<double>(position.y()) / _fixedPointScale;
	const double localX = worldX - _originWorldX;
	const double localY = worldY - _originWorldY;
	const double rowStride = _cellSizeY * kHexRowStride;
	const int32 baseY = static_cast<int32>(floor((localY / rowStride) + 0.5));

	double bestDistanceSq = (numeric_limits<double>::max)();
	int32 bestCellX = 0;
	int32 bestCellY = 0;

	for (int32 y = baseY - 1; y <= baseY + 1; y++)
	{
		const double rowOffset = IsOddRow(y) ? 0.5 : 0.0;
		const int32 baseX = static_cast<int32>(floor((localX / _cellSizeX) - rowOffset + 0.5));

		for (int32 x = baseX - 1; x <= baseX + 1; x++)
		{
			const double centerX = _originWorldX + (static_cast<double>(x) + rowOffset) * _cellSizeX;
			const double centerY = _originWorldY + static_cast<double>(y) * rowStride;
			const double dx = worldX - centerX;
			const double dy = worldY - centerY;
			const double distanceSq = (dx * dx) + (dy * dy);

			if (distanceSq < bestDistanceSq)
			{
				bestDistanceSq = distanceSq;
				bestCellX = x;
				bestCellY = y;
			}
		}
	}

	cellX = bestCellX;
	cellY = bestCellY;
}

void FieldWalkMapData::CellToFixed(int32 cellX, int32 cellY, Protocol::Vec2Fixed& position) const
{
	const double rowOffset = IsOddRow(cellY) ? 0.5 : 0.0;
	const double rowStride = _cellSizeY * kHexRowStride;
	const double worldX = _originWorldX + (static_cast<double>(cellX) + rowOffset) * _cellSizeX;
	const double worldY = _originWorldY + static_cast<double>(cellY) * rowStride;

	position.set_x(static_cast<int32>(llround(worldX * _fixedPointScale)));
	position.set_y(static_cast<int32>(llround(worldY * _fixedPointScale)));
}
