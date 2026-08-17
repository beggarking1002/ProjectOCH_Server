#pragma once

#include "Struct.pb.h"

class FieldWalkMapData
{
public:
	struct Range
	{
		int32 y = 0;
		int32 xMin = 0;
		int32 xMax = 0;
	};

	bool LoadFromFile(const string& path);
	bool IsLoaded() const { return _loaded; }
	const string& MapId() const { return _mapId; }

	bool IsWalkableFixed(const Protocol::Vec2Fixed& position) const;
	bool IsWalkableFixed(const Protocol::Vec2Fixed& position, int32& cellX, int32& cellY) const;
	bool TryGetCellFromFixed(const Protocol::Vec2Fixed& position, int32& cellX, int32& cellY) const;
	bool TryGetVillageIdAtCell(int32 cellX, int32 cellY, string& outVillageId) const;
	bool IsWaterCell(int32 cellX, int32 cellY) const;
	int32 GetHexDistanceCells(int32 fromCellX, int32 fromCellY, int32 toCellX, int32 toCellY) const;
	bool TryGetRandomWalkablePosition(Protocol::Vec2Fixed& position) const;
	// Builds a shortest traversable route. The returned waypoints exclude start and include target.
	bool TryFindPathFixed(const Protocol::Vec2Fixed& start, const Protocol::Vec2Fixed& target,
		vector<Protocol::Vec2Fixed>& outWaypoints) const;
	int32 FixedPointScale() const { return _fixedPointScale; }

private:
	struct VillageArea
	{
		string villageId;
		vector<Range> tileRanges;
	};

	bool IsWalkableCell(int32 cellX, int32 cellY) const;
	bool IsVillageCell(int32 cellX, int32 cellY) const;
	void FixedToCell(const Protocol::Vec2Fixed& position, int32& cellX, int32& cellY) const;
	void CellToFixed(int32 cellX, int32 cellY, Protocol::Vec2Fixed& position) const;

private:
	bool _loaded = false;
	string _mapId;
	int32 _fixedPointScale = 100;
	double _cellSizeX = 1.0;
	double _cellSizeY = 1.0;
	double _originWorldX = 0.0;
	double _originWorldY = 0.0;
	unordered_map<int32, vector<Range>> _walkableRanges;
	unordered_map<int32, vector<Range>> _waterRanges;
	vector<VillageArea> _villageAreas;
};

extern FieldWalkMapData GFieldWalkMapData;
