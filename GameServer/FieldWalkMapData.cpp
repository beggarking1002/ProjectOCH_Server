#include "pch.h"
#include "FieldWalkMapData.h"

#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <queue>
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

	struct FieldCell
	{
		int32 x = 0;
		int32 y = 0;

		bool operator==(const FieldCell& other) const { return x == other.x && y == other.y; }
		bool operator<(const FieldCell& other) const
		{
			return y != other.y ? y < other.y : x < other.x;
		}
	};

	struct OpenNode
	{
		FieldCell cell;
		int32 costFromStart = 0;
		int32 estimatedTotalCost = 0;

		bool operator<(const OpenNode& other) const
		{
			if (estimatedTotalCost != other.estimatedTotalCost)
				return estimatedTotalCost > other.estimatedTotalCost;
			if (costFromStart != other.costFromStart)
				return costFromStart > other.costFromStart;
			return other.cell < cell;
		}
	};

	int32 OddRToAxialQ(const FieldCell& cell)
	{
		return cell.x - ((cell.y - (cell.y & 1)) / 2);
	}

	int32 EstimateHexDistance(const FieldCell& from, const FieldCell& to)
	{
		const int32 fromQ = OddRToAxialQ(from);
		const int32 toQ = OddRToAxialQ(to);
		const int32 deltaQ = toQ - fromQ;
		const int32 deltaR = to.y - from.y;
		return (abs(deltaQ) + abs(deltaR) + abs(deltaQ + deltaR)) / 2;
	}

	array<FieldCell, 6> GetNeighbors(const FieldCell& cell)
	{
		const int32 diagonalX = IsOddRow(cell.y) ? cell.x + 1 : cell.x - 1;
		return
		{
			FieldCell{ cell.x - 1, cell.y },
			FieldCell{ cell.x + 1, cell.y },
			FieldCell{ cell.x, cell.y - 1 },
			FieldCell{ diagonalX, cell.y - 1 },
			FieldCell{ cell.x, cell.y + 1 },
			FieldCell{ diagonalX, cell.y + 1 },
		};
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

bool FieldWalkMapData::TryFindPathFixed(const Protocol::Vec2Fixed& start, const Protocol::Vec2Fixed& target,
	vector<Protocol::Vec2Fixed>& outWaypoints) const
{
	outWaypoints.clear();
	if (_loaded == false)
		return false;

	int32 startX = 0;
	int32 startY = 0;
	int32 targetX = 0;
	int32 targetY = 0;
	if (IsWalkableFixed(start, startX, startY) == false || IsWalkableFixed(target, targetX, targetY) == false)
		return false;

	const FieldCell startCell{ startX, startY };
	const FieldCell targetCell{ targetX, targetY };
	if (startCell == targetCell)
	{
		outWaypoints.push_back(target);
		return true;
	}

	priority_queue<OpenNode> openNodes;
	map<FieldCell, int32> costs;
	map<FieldCell, FieldCell> predecessors;
	openNodes.push(OpenNode{ startCell, 0, EstimateHexDistance(startCell, targetCell) });
	costs.emplace(startCell, 0);

	bool found = false;
	while (openNodes.empty() == false)
	{
		const OpenNode current = openNodes.top();
		openNodes.pop();

		auto currentCostIt = costs.find(current.cell);
		if (currentCostIt == costs.end() || current.costFromStart != currentCostIt->second)
			continue;
		if (current.cell == targetCell)
		{
			found = true;
			break;
		}

		for (const FieldCell& neighbor : GetNeighbors(current.cell))
		{
			if (IsWalkableCell(neighbor.x, neighbor.y) == false)
				continue;

			const int32 nextCost = current.costFromStart + 1;
			auto knownCostIt = costs.find(neighbor);
			if (knownCostIt != costs.end() && knownCostIt->second <= nextCost)
				continue;

			costs[neighbor] = nextCost;
			predecessors[neighbor] = current.cell;
			openNodes.push(OpenNode{ neighbor, nextCost, nextCost + EstimateHexDistance(neighbor, targetCell) });
		}
	}

	if (found == false)
		return false;

	vector<FieldCell> cells;
	FieldCell current = targetCell;
	while ((current == startCell) == false)
	{
		cells.push_back(current);
		current = predecessors.at(current);
	}
	reverse(cells.begin(), cells.end());

	for (size_t index = 0; index + 1 < cells.size(); index++)
	{
		Protocol::Vec2Fixed waypoint;
		CellToFixed(cells[index].x, cells[index].y, waypoint);
		outWaypoints.push_back(waypoint);
	}
	outWaypoints.push_back(target);
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
