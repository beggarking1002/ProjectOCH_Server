#pragma once

struct VillageTemplate
{
	string villageId;
	string name;
	string description;
	string artworkAddress;
	bool shopEnabled = false;
	bool innEnabled = false;
	bool questEnabled = false;
};

class VillageDataManager
{
public:
	bool Load();
	const VillageTemplate* GetVillage(const string& villageId) const;

private:
	string ResolveDataPath(const string& fileName) const;

private:
	bool _loaded = false;
	unordered_map<string, VillageTemplate> _villagesById;
};

extern VillageDataManager GVillageData;
