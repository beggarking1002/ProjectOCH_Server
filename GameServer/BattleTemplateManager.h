#pragma once

struct BattleEffectParamTemplate
{
	string effectGroupKey;
	string effectInstanceKey;
	string paramKey;
	string paramValue;
};

struct BattlePawnClassTemplate
{
	string classKey;
	Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
	Protocol::BattlePawnRole role = Protocol::BATTLE_PAWN_ROLE_NONE;
	int32 baseStr = 0;
	int32 baseCon = 0;
	int32 baseDex = 0;
	int32 baseSpell = 0;
	int32 baseDefense = 0;
	int32 baseFocus = 0;
	int32 baseWill = 0;
	int32 counterSkillSlot = 0;
};

struct BattleSkillTemplate
{
	string skillKey;
	string classKey;
	string skillCategory;
	string combatType;
	string damageType;
	string slotVariantKey;
	int32 actionSlot = 0;
	int32 apCost = 0;
	int32 rangeMin = 0;
	int32 rangeMax = 0;
	string targetType;
	string targetShape;
	Protocol::BattleTileOverlayType requiredOverlayType = Protocol::BATTLE_TILE_OVERLAY_TYPE_NONE;
	string effectGroupKey;
};

struct BattleEffectTemplate
{
	string effectGroupKey;
	string effectInstanceKey;
	int32 effectOrder = 0;
	string effectKey;
	string trigger;
	string effectTarget;
	string targetSkillKey;
	string exclusiveGroup;
	int32 exclusivePriority = 0;
	bool stopOnMatch = false;
	unordered_map<string, string> params;
};

struct BattleMapTileTemplate
{
	string mapId;
	int32 axialQ = 0;
	int32 axialR = 0;
	Protocol::BattleTileType tileType = Protocol::BATTLE_TILE_TYPE_NORMAL;
};

struct BattleZocTemplate
{
	Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
	bool enabled = false;
	int32 range = 1;
	int32 frontArcWidth = 3;
	int32 reactionLimitPerTurn = 1;
	int32 reactionSkillSlot = 2;
	unordered_set<string> triggers;
};

class BattleTemplateManager
{
public:
	bool Load();

	const BattlePawnClassTemplate* GetPawnClassTemplate(Protocol::PawnClass pawnClass);
	const BattleSkillTemplate* GetSkillByActionSlot(Protocol::PawnClass pawnClass, int32 actionSlot);
	const vector<BattleSkillTemplate>* GetSkills(Protocol::PawnClass pawnClass);
	const BattleSkillTemplate* GetSkillByKey(const string& skillKey);
	const vector<BattleEffectTemplate>* GetEffects(const string& effectGroupKey);
	const vector<BattleMapTileTemplate>* GetBattleMapTiles(const string& mapId);
	const BattleZocTemplate* GetZocTemplate(Protocol::PawnClass pawnClass);
	// Shared battle-rule tuning values. Character/skill data stays in its own tables.
	double GetConfigDouble(const string& configKey, double fallback = 0.0);
	int32 GetConfigInt(const string& configKey, int32 fallback = 0);
	bool TryParseBattleResourceType(const string& key, Protocol::BattleResourceType& resourceType) const;
	bool TryParseBattleTileType(const string& key, Protocol::BattleTileType& tileType) const;
	bool TryParseBattleTileOverlayType(const string& key, Protocol::BattleTileOverlayType& overlayType) const;

private:
	bool LoadClassKey(const string& path);
	bool LoadPawnTemplate(const string& path);
	bool LoadBattleSkill(const string& path);
	bool LoadBattleSkillVariant(const string& path);
	bool LoadBattleSkillEffect(const string& path);
	bool LoadBattleSkillEffectParam(const string& path);
	bool LoadBattleMapTile(const string& path);
	bool LoadBattleZoc(const string& path);
	bool LoadBattleConfig(const string& path);
	bool ValidateTemplates();

	string ResolveDataPath(const string& fileName);

private:
	bool _loaded = false;
	unordered_map<string, Protocol::PawnClass> _classKeyToPawnClass;
	unordered_map<Protocol::PawnClass, string> _pawnClassToClassKey;
	unordered_map<Protocol::PawnClass, BattlePawnClassTemplate> _pawnClassTemplates;
	unordered_map<string, vector<BattleSkillTemplate>> _skillsByClassKey;
	unordered_map<string, vector<BattleEffectTemplate>> _effectsByGroupKey;
	unordered_map<string, vector<BattleMapTileTemplate>> _battleMapTilesByMapId;
	unordered_map<Protocol::PawnClass, BattleZocTemplate> _zocTemplates;
	unordered_map<string, double> _battleConfigValues;
	vector<BattleEffectParamTemplate> _effectParams;
};

extern BattleTemplateManager GBattleTemplates;
