#pragma once

#include "Struct.pb.h"

class FieldWalkMapData
{
public:
	struct Range
	{
		int32 xMin = 0;
		int32 xMax = 0;
	};

	bool LoadFromFile(const string& path);
	bool IsLoaded() const { return _loaded; }
	const string& MapId() const { return _mapId; }

	bool IsWalkableFixed(const Protocol::Vec2Fixed& position) const;
	bool IsWalkableFixed(const Protocol::Vec2Fixed& position, int32& cellX, int32& cellY) const;
	bool TryGetRandomWalkablePosition(Protocol::Vec2Fixed& position) const;

private:
	bool IsWalkableCell(int32 cellX, int32 cellY) const;
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
};

extern FieldWalkMapData GFieldWalkMapData;
