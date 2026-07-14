#include "pch.h"
#include "BattleTemplateManager.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>

BattleTemplateManager GBattleTemplates;

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

	string ToUpper(string value)
	{
		transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
			{
				return static_cast<char>(toupper(ch));
			});
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
			else
			{
				if (ch == '"')
					inQuotes = true;
				else if (ch == ',')
				{
					cells.push_back(Trim(cell));
					cell.clear();
				}
				else
					cell.push_back(ch);
			}
		}

		cells.push_back(Trim(cell));
		return cells;
	}

	bool ReadCsv(const string& path, vector<vector<string>>& rows)
	{
		ifstream file(path);
		if (file.is_open() == false)
			return false;

		string line;
		while (getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			rows.push_back(ParseCsvLine(line));
		}
		return true;
	}

	string Cell(const vector<string>& row, const unordered_map<string, size_t>& header, const string& name)
	{
		auto it = header.find(name);
		if (it == header.end() || it->second >= row.size())
			return "";
		return row[it->second];
	}

	unordered_map<string, size_t> BuildHeader(const vector<string>& row)
	{
		unordered_map<string, size_t> header;
		for (size_t i = 0; i < row.size(); i++)
			header[row[i]] = i;
		return header;
	}

	int32 ToInt(const string& value, int32 fallback = 0)
	{
		if (value.empty())
			return fallback;

		try
		{
			return static_cast<int32>(stod(value));
		}
		catch (...)
		{
			return fallback;
		}
	}

	double ToDouble(const string& value, double fallback = 0.0)
	{
		if (value.empty())
			return fallback;

		try
		{
			return stod(value);
		}
		catch (...)
		{
			return fallback;
		}
	}

	bool ToBool(const string& value)
	{
		const string normalized = ToUpper(Trim(value));
		return normalized == "TRUE" || normalized == "1" || normalized == "YES";
	}

}

bool BattleTemplateManager::Load()
{
	if (_loaded)
		return true;

	_classKeyToPawnClass.clear();
	_pawnClassToClassKey.clear();
	_pawnClassTemplates.clear();
	_skillsByClassKey.clear();
	_effectsByGroupKey.clear();
	_battleMapTilesByMapId.clear();
	_effectParams.clear();

	const bool success =
		LoadClassKey(ResolveDataPath("ClassKey.csv")) &&
		LoadPawnTemplate(ResolveDataPath("PawnTemplate.csv")) &&
		LoadBattleSkill(ResolveDataPath("BattleSkill.csv")) &&
		LoadBattleSkillEffect(ResolveDataPath("BattleSkillEffect.csv")) &&
		LoadBattleSkillEffectParam(ResolveDataPath("BattleSkillEffectParam.csv")) &&
		LoadBattleMapTile(ResolveDataPath("BattleMapTile.csv")) &&
		ValidateTemplates();

	_loaded = success;
	cout << "[BattleTemplateManager] Load " << (success ? "success" : "failed")
		<< " pawn_class_templates=" << _pawnClassTemplates.size()
		<< " class_keys=" << _classKeyToPawnClass.size()
		<< " skill_class_count=" << _skillsByClassKey.size()
		<< " effect_group_count=" << _effectsByGroupKey.size()
		<< " battle_map_count=" << _battleMapTilesByMapId.size()
		<< " effect_params=" << _effectParams.size()
		<< endl;

	return success;
}

const BattlePawnClassTemplate* BattleTemplateManager::GetPawnClassTemplate(Protocol::PawnClass pawnClass)
{
	if (Load() == false)
		return nullptr;

	auto it = _pawnClassTemplates.find(pawnClass);
	if (it == _pawnClassTemplates.end())
		return nullptr;
	return &it->second;
}

const BattleSkillTemplate* BattleTemplateManager::GetSkillByActionSlot(Protocol::PawnClass pawnClass, int32 actionSlot)
{
	if (Load() == false)
		return nullptr;

	auto classKeyIt = _pawnClassToClassKey.find(pawnClass);
	if (classKeyIt == _pawnClassToClassKey.end())
		return nullptr;

	auto skillIt = _skillsByClassKey.find(classKeyIt->second);
	if (skillIt == _skillsByClassKey.end())
		return nullptr;

	for (const BattleSkillTemplate& skill : skillIt->second)
	{
		if (skill.actionSlot == actionSlot)
			return &skill;
	}

	return nullptr;
}

const vector<BattleEffectTemplate>* BattleTemplateManager::GetEffects(const string& effectGroupKey)
{
	if (Load() == false)
		return nullptr;

	auto it = _effectsByGroupKey.find(effectGroupKey);
	if (it == _effectsByGroupKey.end())
		return nullptr;
	return &it->second;
}

const vector<BattleMapTileTemplate>* BattleTemplateManager::GetBattleMapTiles(const string& mapId)
{
	if (Load() == false)
		return nullptr;

	auto it = _battleMapTilesByMapId.find(mapId);
	return it != _battleMapTilesByMapId.end() ? &it->second : nullptr;
}

bool BattleTemplateManager::TryParseBattleResourceType(const string& key, Protocol::BattleResourceType& resourceType) const
{
	const string enumName = "BATTLE_RESOURCE_TYPE_" + ToUpper(Trim(key));
	return Protocol::BattleResourceType_Parse(enumName, &resourceType) && resourceType != Protocol::BATTLE_RESOURCE_TYPE_NONE;
}

bool BattleTemplateManager::TryParseBattleTileType(const string& key, Protocol::BattleTileType& tileType) const
{
	const string enumName = "BATTLE_TILE_TYPE_" + ToUpper(Trim(key));
	return Protocol::BattleTileType_Parse(enumName, &tileType) && tileType != Protocol::BATTLE_TILE_TYPE_NONE;
}

bool BattleTemplateManager::TryParseBattleTileOverlayType(const string& key, Protocol::BattleTileOverlayType& overlayType) const
{
	const string enumName = "BATTLE_TILE_OVERLAY_TYPE_" + ToUpper(Trim(key));
	return Protocol::BattleTileOverlayType_Parse(enumName, &overlayType) && overlayType != Protocol::BATTLE_TILE_OVERLAY_TYPE_NONE;
}

bool BattleTemplateManager::LoadClassKey(const string& path)
{
	vector<vector<string>> rows;
	if (ReadCsv(path, rows) == false || rows.size() < 2)
	{
		cout << "[BattleTemplateManager] Failed to read ClassKey: " << path << endl;
		return false;
	}

	const unordered_map<string, size_t> header = BuildHeader(rows[0]);
	for (size_t i = 2; i < rows.size(); i++)
	{
		const string classKey = Cell(rows[i], header, "ClassKey");
		const string pawnClassName = Cell(rows[i], header, "PawnClass");
		if (classKey.empty() || pawnClassName.empty())
			continue;

		Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
		if (Protocol::PawnClass_Parse(pawnClassName, &pawnClass) == false)
		{
			cout << "[BattleTemplateManager] Invalid PawnClass: " << pawnClassName << endl;
			return false;
		}

		_classKeyToPawnClass[classKey] = pawnClass;
		_pawnClassToClassKey[pawnClass] = classKey;
	}

	return true;
}

bool BattleTemplateManager::LoadPawnTemplate(const string& path)
{
	vector<vector<string>> rows;
	if (ReadCsv(path, rows) == false || rows.size() < 2)
	{
		cout << "[BattleTemplateManager] Failed to read PawnTemplate: " << path << endl;
		return false;
	}

	const unordered_map<string, size_t> header = BuildHeader(rows[0]);
	for (size_t i = 2; i < rows.size(); i++)
	{
		BattlePawnClassTemplate pawnTemplate;
		pawnTemplate.classKey = Cell(rows[i], header, "ClassKey");
		if (pawnTemplate.classKey.empty())
			continue;

		auto classIt = _classKeyToPawnClass.find(pawnTemplate.classKey);
		if (classIt == _classKeyToPawnClass.end())
		{
			cout << "[BattleTemplateManager] PawnTemplate has unknown ClassKey: " << pawnTemplate.classKey << endl;
			return false;
		}

		const string roleName = "BATTLE_PAWN_ROLE_" + ToUpper(Cell(rows[i], header, "Role"));
		if (Protocol::BattlePawnRole_Parse(roleName, &pawnTemplate.role) == false)
		{
			cout << "[BattleTemplateManager] Invalid BattlePawnRole: " << roleName << endl;
			return false;
		}

		pawnTemplate.pawnClass = classIt->second;
		pawnTemplate.baseStr = ToInt(Cell(rows[i], header, "BaseStr"));
		pawnTemplate.baseCon = ToInt(Cell(rows[i], header, "BaseCon"));
		pawnTemplate.baseDex = ToInt(Cell(rows[i], header, "BaseDex"));
		pawnTemplate.baseSpell = ToInt(Cell(rows[i], header, "BaseSpell"));
		pawnTemplate.baseDefense = ToInt(Cell(rows[i], header, "BaseDefense"));
		pawnTemplate.baseFocus = ToInt(Cell(rows[i], header, "BaseFocus"));
		pawnTemplate.baseWill = ToInt(Cell(rows[i], header, "BaseWill"));

		_pawnClassTemplates[pawnTemplate.pawnClass] = pawnTemplate;
	}

	return true;
}

bool BattleTemplateManager::LoadBattleSkill(const string& path)
{
	vector<vector<string>> rows;
	if (ReadCsv(path, rows) == false || rows.size() < 2)
	{
		cout << "[BattleTemplateManager] Failed to read BattleSkill: " << path << endl;
		return false;
	}

	const unordered_map<string, size_t> header = BuildHeader(rows[0]);
	for (size_t i = 2; i < rows.size(); i++)
	{
		BattleSkillTemplate skill;
		skill.skillKey = Cell(rows[i], header, "SkillKey");
		skill.classKey = Cell(rows[i], header, "ClassKey");
		if (skill.skillKey.empty() || skill.classKey.empty())
			continue;

		if (_classKeyToPawnClass.find(skill.classKey) == _classKeyToPawnClass.end())
		{
			cout << "[BattleTemplateManager] BattleSkill has unknown ClassKey: " << skill.classKey << endl;
			return false;
		}

		skill.skillCategory = Cell(rows[i], header, "SkillCategory");
		skill.actionSlot = ToInt(Cell(rows[i], header, "ActionSlot"));
		skill.apCost = ToInt(Cell(rows[i], header, "ApCost"));
		skill.rangeMin = ToInt(Cell(rows[i], header, "RangeMin"));
		skill.rangeMax = ToInt(Cell(rows[i], header, "RangeMax"));
		skill.targetType = Cell(rows[i], header, "TargetType");
		skill.effectGroupKey = Cell(rows[i], header, "EffectGroupKey");

		_skillsByClassKey[skill.classKey].push_back(skill);
	}

	return true;
}

bool BattleTemplateManager::LoadBattleSkillEffect(const string& path)
{
	vector<vector<string>> rows;
	if (ReadCsv(path, rows) == false || rows.size() < 2)
	{
		cout << "[BattleTemplateManager] Failed to read BattleSkillEffect: " << path << endl;
		return false;
	}

	const unordered_map<string, size_t> header = BuildHeader(rows[0]);
	for (size_t i = 2; i < rows.size(); i++)
	{
		BattleEffectTemplate effect;
		effect.effectGroupKey = Cell(rows[i], header, "EffectGroupKey");
		effect.effectInstanceKey = Cell(rows[i], header, "EffectInstanceKey");
		if (effect.effectGroupKey.empty() || effect.effectInstanceKey.empty())
			continue;

		effect.effectOrder = ToInt(Cell(rows[i], header, "EffectOrder"));
		effect.effectKey = Cell(rows[i], header, "EffectKey");
		effect.trigger = Cell(rows[i], header, "Trigger");
		effect.effectTarget = Cell(rows[i], header, "EffectTarget");
		effect.targetSkillKey = Cell(rows[i], header, "TargetSkillKey");
		effect.exclusiveGroup = Cell(rows[i], header, "ExclusiveGroup");
		effect.exclusivePriority = ToInt(Cell(rows[i], header, "ExclusivePriority"));
		effect.stopOnMatch = ToBool(Cell(rows[i], header, "StopOnMatch"));

		_effectsByGroupKey[effect.effectGroupKey].push_back(effect);
	}

	for (auto& item : _effectsByGroupKey)
	{
		sort(item.second.begin(), item.second.end(), [](const BattleEffectTemplate& lhs, const BattleEffectTemplate& rhs)
			{
				return lhs.effectOrder < rhs.effectOrder;
			});
	}

	return true;
}

bool BattleTemplateManager::LoadBattleSkillEffectParam(const string& path)
{
	vector<vector<string>> rows;
	if (ReadCsv(path, rows) == false || rows.size() < 2)
	{
		cout << "[BattleTemplateManager] Failed to read BattleSkillEffectParam: " << path << endl;
		return false;
	}

	const unordered_map<string, size_t> header = BuildHeader(rows[0]);
	for (size_t i = 2; i < rows.size(); i++)
	{
		BattleEffectParamTemplate param;
		param.effectGroupKey = Cell(rows[i], header, "EffectGroupKey");
		param.effectInstanceKey = Cell(rows[i], header, "EffectInstanceKey");
		param.paramKey = Cell(rows[i], header, "ParamKey");
		param.paramValue = Cell(rows[i], header, "ParamValue");
		if (param.effectGroupKey.empty() || param.effectInstanceKey.empty() || param.paramKey.empty())
			continue;

		_effectParams.push_back(param);

		auto groupIt = _effectsByGroupKey.find(param.effectGroupKey);
		if (groupIt == _effectsByGroupKey.end())
			continue;

		for (BattleEffectTemplate& effect : groupIt->second)
		{
			if (effect.effectInstanceKey == param.effectInstanceKey)
			{
				effect.params[param.paramKey] = param.paramValue;
				break;
			}
		}
	}

	return true;
}

bool BattleTemplateManager::LoadBattleMapTile(const string& path)
{
	vector<vector<string>> rows;
	if (ReadCsv(path, rows) == false || rows.size() < 2)
	{
		cout << "[BattleTemplateManager] Failed to read BattleMapTile: " << path << endl;
		return false;
	}

	const unordered_map<string, size_t> header = BuildHeader(rows[0]);
	for (size_t i = 2; i < rows.size(); i++)
	{
		BattleMapTileTemplate tile;
		tile.mapId = Cell(rows[i], header, "MapId");
		if (tile.mapId.empty())
			continue;

		tile.q = ToInt(Cell(rows[i], header, "Q"));
		tile.r = ToInt(Cell(rows[i], header, "R"));
		if (TryParseBattleTileType(Cell(rows[i], header, "TileType"), tile.tileType) == false)
		{
			cout << "[BattleTemplateManager] Invalid BattleTileType map_id=" << tile.mapId << endl;
			return false;
		}

		_battleMapTilesByMapId[tile.mapId].push_back(tile);
	}

	return true;
}

bool BattleTemplateManager::ValidateTemplates()
{
	for (const auto& item : _skillsByClassKey)
	{
		unordered_set<int32> actionSlots;
		for (const BattleSkillTemplate& skill : item.second)
		{
			if (skill.actionSlot <= 0)
			{
				cout << "[BattleTemplateManager] Invalid ActionSlot skill_key=" << skill.skillKey << endl;
				return false;
			}

			if (actionSlots.insert(skill.actionSlot).second == false)
			{
				cout << "[BattleTemplateManager] Duplicate ActionSlot"
					<< " class_key=" << item.first
					<< " action_slot=" << skill.actionSlot
					<< endl;
				return false;
			}

			if (skill.effectGroupKey.empty() == false && _effectsByGroupKey.find(skill.effectGroupKey) == _effectsByGroupKey.end())
			{
				cout << "[BattleTemplateManager] Missing EffectGroupKey"
					<< " skill_key=" << skill.skillKey
					<< " effect_group_key=" << skill.effectGroupKey
					<< endl;
				return false;
			}
		}
	}

	for (const BattleEffectParamTemplate& param : _effectParams)
	{
		auto groupIt = _effectsByGroupKey.find(param.effectGroupKey);
		if (groupIt == _effectsByGroupKey.end())
		{
			cout << "[BattleTemplateManager] EffectParam has unknown EffectGroupKey"
				<< " effect_group_key=" << param.effectGroupKey
				<< " effect_instance_key=" << param.effectInstanceKey
				<< endl;
			return false;
		}

		const bool found = any_of(groupIt->second.begin(), groupIt->second.end(), [&param](const BattleEffectTemplate& effect)
			{
				return effect.effectInstanceKey == param.effectInstanceKey;
			});
		if (found == false)
		{
			cout << "[BattleTemplateManager] EffectParam has unknown EffectInstanceKey"
				<< " effect_group_key=" << param.effectGroupKey
				<< " effect_instance_key=" << param.effectInstanceKey
				<< endl;
			return false;
		}

		if (param.paramKey == "resource_key" || param.paramKey == "condition_resource_key")
		{
			Protocol::BattleResourceType resourceType = Protocol::BATTLE_RESOURCE_TYPE_NONE;
			if (TryParseBattleResourceType(param.paramValue, resourceType) == false)
			{
				cout << "[BattleTemplateManager] Invalid BattleResourceType"
					<< " effect_group_key=" << param.effectGroupKey
					<< " effect_instance_key=" << param.effectInstanceKey
					<< " value=" << param.paramValue
					<< endl;
				return false;
			}
		}

		if (param.paramKey == "tile_filter")
		{
			Protocol::BattleTileType tileType = Protocol::BATTLE_TILE_TYPE_NONE;
			if (TryParseBattleTileType(param.paramValue, tileType) == false)
			{
				cout << "[BattleTemplateManager] Invalid BattleTileType"
					<< " effect_group_key=" << param.effectGroupKey
					<< " effect_instance_key=" << param.effectInstanceKey
					<< " value=" << param.paramValue
					<< endl;
				return false;
			}
		}

		if (param.paramKey == "overlay_type")
		{
			Protocol::BattleTileOverlayType overlayType = Protocol::BATTLE_TILE_OVERLAY_TYPE_NONE;
			if (TryParseBattleTileOverlayType(param.paramValue, overlayType) == false)
			{
				cout << "[BattleTemplateManager] Invalid BattleTileOverlayType"
					<< " effect_group_key=" << param.effectGroupKey
					<< " effect_instance_key=" << param.effectInstanceKey
					<< " value=" << param.paramValue
					<< endl;
				return false;
			}
		}
	}

	return true;
}

string BattleTemplateManager::ResolveDataPath(const string& fileName)
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
