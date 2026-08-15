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

	vector<string> ExtractObjectBlocks(const string& arrayBlock)
	{
		vector<string> objects;
		int32 depth = 0;
		size_t objectStart = string::npos;

		for (size_t i = 0; i < arrayBlock.size(); i++)
		{
			if (arrayBlock[i] == '{')
			{
				if (depth == 0)
					objectStart = i;
				depth++;
			}
			else if (arrayBlock[i] == '}')
			{
				depth--;
				if (depth == 0 && objectStart != string::npos)
				{
					objects.push_back(arrayBlock.substr(objectStart, i - objectStart + 1));
					objectStart = string::npos;
				}
			}
		}

		return objects;
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
	string villageAreasBlock;

	if (ExtractString(json, "map_id", mapId) == false ||
		ExtractInt(json, "fixed_point_scale", fixedPointScale) == false ||
		ExtractBlock(json, "cell_size", '{', '}', cellSizeBlock) == false ||
		ExtractBlock(json, "origin_world", '{', '}', originWorldBlock) == false ||
		ExtractBlock(json, "walkable_ranges", '[', ']', walkableRangesBlock) == false ||
		ExtractBlock(json, "village_areas", '[', ']', villageAreasBlock) == false)
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
		range.y = y;
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

	vector<VillageArea> villageAreas;
	for (const string& villageBlock : ExtractObjectBlocks(villageAreasBlock))
	{
		VillageArea villageArea;
		string tileRangesBlock;
		if (ExtractString(villageBlock, "village_id", villageArea.villageId) == false ||
			ExtractBlock(villageBlock, "tile_ranges", '[', ']', tileRangesBlock) == false ||
			villageArea.villageId.empty())
		{
			cout << "[FieldWalkMapData] Invalid village_area: " << path << endl;
			return false;
		}

		for (sregex_iterator it(tileRangesBlock.begin(), tileRangesBlock.end(), rangePattern), end; it != end; ++it)
		{
			const smatch& match = *it;
			Range range;
			range.xMin = stoi(match[2].str());
			range.xMax = stoi(match[3].str());
			if (range.xMax < range.xMin)
				swap(range.xMin, range.xMax);

			villageArea.tileRanges.push_back(range);
			villageArea.tileRanges.back().y = stoi(match[1].str());
		}

		if (villageArea.tileRanges.empty())
		{
			cout << "[FieldWalkMapData] Empty village tile_ranges: " << villageArea.villageId << endl;
			return false;
		}

		villageAreas.push_back(move(villageArea));
	}

	_mapId = mapId;
	_fixedPointScale = fixedPointScale;
	_cellSizeX = cellSizeX;
	_cellSizeY = cellSizeY;
	_originWorldX = originWorldX;
	_originWorldY = originWorldY;
	_walkableRanges = move(rangesByY);
	_villageAreas = move(villageAreas);
	_loaded = true;

	cout << "[FieldWalkMapData] Loaded " << _mapId
		<< " rows=" << _walkableRanges.size()
		<< " village_areas=" << _villageAreas.size()
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

bool FieldWalkMapData::TryGetCellFromFixed(const Protocol::Vec2Fixed& position, int32& cellX, int32& cellY) const
{
	if (_loaded == false)
		return false;

	FixedToCell(position, cellX, cellY);
	return true;
}

bool FieldWalkMapData::TryGetVillageIdAtCell(int32 cellX, int32 cellY, string& outVillageId) const
{
	outVillageId.clear();
	if (_loaded == false)
		return false;

	for (const VillageArea& villageArea : _villageAreas)
	{
		for (const Range& range : villageArea.tileRanges)
		{
			if (range.y == cellY && cellX >= range.xMin && cellX <= range.xMax)
			{
				outVillageId = villageArea.villageId;
				return true;
			}
		}
	}

	return false;
}

int32 FieldWalkMapData::GetHexDistanceCells(int32 fromCellX, int32 fromCellY, int32 toCellX, int32 toCellY) const
{
	const FieldCell from{ fromCellX, fromCellY };
	const FieldCell to{ toCellX, toCellY };
	return EstimateHexDistance(from, to);
}

bool FieldWalkMapData::TryGetRandomWalkablePosition(Protocol::Vec2Fixed& position) const
{
	if (_loaded == false || _walkableRanges.empty())
		return false;

	static thread_local mt19937 generator{ random_device{}() };
	vector<FieldCell> spawnableCells;
	for (const auto& [cellY, ranges] : _walkableRanges)
	{
		for (const Range& range : ranges)
		{
			for (int32 cellX = range.xMin; cellX <= range.xMax; cellX++)
			{
				if (IsVillageCell(cellX, cellY) == false)
					spawnableCells.push_back({ cellX, cellY });
			}
		}
	}

	if (spawnableCells.empty())
		return false;

	uniform_int_distribution<size_t> cellDist(0, spawnableCells.size() - 1);
	const FieldCell& spawnCell = spawnableCells[cellDist(generator)];
	CellToFixed(spawnCell.x, spawnCell.y, position);
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

	vector<Protocol::Vec2Fixed> rawWaypoints;
	for (size_t index = 0; index + 1 < cells.size(); index++)
	{
		Protocol::Vec2Fixed waypoint;
		CellToFixed(cells[index].x, cells[index].y, waypoint);
		rawWaypoints.push_back(waypoint);
	}
	rawWaypoints.push_back(target);

	// A* visits each hex center. Rendering every center makes even an open route
	// appear as a tile-by-tile zigzag, so greedily pull the string taut between
	// the farthest pairs that retain a fully walkable line of sight.
	const int32 sampleSpacing = (max)(1, _fixedPointScale / 10);
	auto hasWalkableLineOfSight = [this, sampleSpacing](const Protocol::Vec2Fixed& from, const Protocol::Vec2Fixed& to)
		{
			const double deltaX = static_cast<double>(to.x()) - static_cast<double>(from.x());
			const double deltaY = static_cast<double>(to.y()) - static_cast<double>(from.y());
			const int32 sampleCount = (max)(1, static_cast<int32>(ceil(sqrt((deltaX * deltaX) + (deltaY * deltaY)) / sampleSpacing)));
			for (int32 sampleIndex = 0; sampleIndex <= sampleCount; sampleIndex++)
			{
				const double t = static_cast<double>(sampleIndex) / sampleCount;
				Protocol::Vec2Fixed sample;
				sample.set_x(static_cast<int32>(llround(static_cast<double>(from.x()) + (deltaX * t))));
				sample.set_y(static_cast<int32>(llround(static_cast<double>(from.y()) + (deltaY * t))));
				if (IsWalkableFixed(sample) == false)
					return false;
			}
			return true;
		};

	Protocol::Vec2Fixed routeStart;
	routeStart.CopyFrom(start);
	for (size_t currentIndex = 0; currentIndex < rawWaypoints.size();)
	{
		size_t nextIndex = currentIndex;
		for (size_t candidateIndex = rawWaypoints.size(); candidateIndex > currentIndex;)
		{
			candidateIndex--;
			if (hasWalkableLineOfSight(routeStart, rawWaypoints[candidateIndex]))
			{
				nextIndex = candidateIndex;
				break;
			}
		}

		outWaypoints.push_back(rawWaypoints[nextIndex]);
		routeStart.CopyFrom(rawWaypoints[nextIndex]);
		currentIndex = nextIndex + 1;
	}
	return true;
}

bool FieldWalkMapData::IsWalkableCell(int32 cellX, int32 cellY) const
{
	if (IsVillageCell(cellX, cellY))
		return false;

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

bool FieldWalkMapData::IsVillageCell(int32 cellX, int32 cellY) const
{
	for (const VillageArea& villageArea : _villageAreas)
	{
		for (const Range& range : villageArea.tileRanges)
		{
			if (range.y == cellY && cellX >= range.xMin && cellX <= range.xMax)
				return true;
		}
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
