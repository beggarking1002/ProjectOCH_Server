#pragma once

enum class QuestObjectiveType
{
	VisitVillage,
	OwnItem,
	DeliverItem,
	BuyItem,
	SellTradeGood,
};

enum class QuestRewardType
{
	Gold,
	Fame,
	Item,
};

struct QuestObjectiveTemplate
{
	uint32 objectiveIndex = 0;
	QuestObjectiveType type = QuestObjectiveType::VisitVillage;
	string targetVillageId;
	string targetItemId;
	int32 requiredCount = 1;
};

struct QuestRewardTemplate
{
	uint32 rewardIndex = 0;
	QuestRewardType type = QuestRewardType::Gold;
	string targetId;
	int32 amount = 0;
};

struct QuestTemplate
{
	string questId;
	string displayName;
	string description;
	string category;
	string startVillageId;
	string completionVillageId;
	string prerequisiteQuestId;
	bool enabled = false;
	int32 sortOrder = 0;
	vector<QuestObjectiveTemplate> objectives;
	vector<QuestRewardTemplate> rewards;
};

class QuestDataManager
{
public:
	bool Load();
	const QuestTemplate* GetQuest(const string& questId) const;
	vector<const QuestTemplate*> GetQuestsForVillage(const string& villageId) const;
	const unordered_map<string, QuestTemplate>& GetAllQuests() const { return _questsById; }

private:
	bool LoadQuests();
	bool LoadObjectives();
	bool LoadRewards();
	bool Validate() const;
	string ResolveDataPath(const string& fileName) const;

private:
	bool _loaded = false;
	unordered_map<string, QuestTemplate> _questsById;
};

extern QuestDataManager GQuestData;
