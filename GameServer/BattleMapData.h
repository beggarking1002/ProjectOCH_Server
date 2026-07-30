#pragma once

#include "Struct.pb.h"

// Server-side representation of a Unity-exported battle walkmap.
// JSON cell ranges are converted to the axial coordinates used in battle.
class BattleMapData
{
public:
	bool LoadFromFile(const string& path);
	bool IsLoaded() const { return _loaded; }
	const string& MapId() const { return _mapId; }

	bool ContainsTile(const Protocol::AxialCoord& axial) const;
	const vector<Protocol::AxialCoord>& Tiles() const { return _tiles; }

private:
	static uint64 MakeTileKey(const Protocol::AxialCoord& axial);

private:
	bool _loaded = false;
	string _mapId;
	vector<Protocol::AxialCoord> _tiles;
	unordered_set<uint64> _tileKeys;
};

extern BattleMapData GBattleMapData;
