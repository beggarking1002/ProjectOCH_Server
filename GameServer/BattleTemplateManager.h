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
};

struct BattleSkillTemplate
{
	string skillKey;
	string classKey;
	string skillCategory;
	int32 actionSlot = 0;
	int32 apCost = 0;
	int32 rangeMin = 0;
	int32 rangeMax = 0;
	string targetType;
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

class BattleTemplateManager
{
public:
	bool Load();

	const BattlePawnClassTemplate* GetPawnClassTemplate(Protocol::PawnClass pawnClass);
	const BattleSkillTemplate* GetSkillByActionSlot(Protocol::PawnClass pawnClass, int32 actionSlot);
	const vector<BattleEffectTemplate>* GetEffects(const string& effectGroupKey);
	bool TryParseBattleResourceType(const string& key, Protocol::BattleResourceType& resourceType) const;

private:
	bool LoadClassKey(const string& path);
	bool LoadPawnTemplate(const string& path);
	bool LoadBattleSkill(const string& path);
	bool LoadBattleSkillEffect(const string& path);
	bool LoadBattleSkillEffectParam(const string& path);
	bool ValidateTemplates();

	string ResolveDataPath(const string& fileName);

private:
	bool _loaded = false;
	unordered_map<string, Protocol::PawnClass> _classKeyToPawnClass;
	unordered_map<Protocol::PawnClass, string> _pawnClassToClassKey;
	unordered_map<Protocol::PawnClass, BattlePawnClassTemplate> _pawnClassTemplates;
	unordered_map<string, vector<BattleSkillTemplate>> _skillsByClassKey;
	unordered_map<string, vector<BattleEffectTemplate>> _effectsByGroupKey;
	vector<BattleEffectParamTemplate> _effectParams;
};

extern BattleTemplateManager GBattleTemplates;
