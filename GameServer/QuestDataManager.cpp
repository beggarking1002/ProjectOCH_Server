#include "pch.h"
#include "QuestDataManager.h"
#include "VillageDataManager.h"
#include "EconomyDataManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

QuestDataManager GQuestData;

namespace
{
	string Trim(string value)
	{
		if (value.size() >= 3 && static_cast<unsigned char>(value[0]) == 0xEF &&
			static_cast<unsigned char>(value[1]) == 0xBB && static_cast<unsigned char>(value[2]) == 0xBF)
			value.erase(0, 3);
		auto notSpace = [](unsigned char ch) { return isspace(ch) == 0; };
		value.erase(value.begin(), find_if(value.begin(), value.end(), notSpace));
		value.erase(find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
		return value;
	}

	vector<string> ParseCsvLine(const string& line)
	{
		vector<string> cells;
		string cell;
		bool quoted = false;
		for (size_t i = 0; i < line.size(); ++i)
		{
			const char ch = line[i];
			if (quoted && ch == '"' && i + 1 < line.size() && line[i + 1] == '"')
			{
				cell.push_back('"');
				++i;
			}
			else if (ch == '"') quoted = !quoted;
			else if (ch == ',' && !quoted) { cells.push_back(Trim(cell)); cell.clear(); }
			else cell.push_back(ch);
		}
		cells.push_back(Trim(cell));
		return cells;
	}

	bool ToBool(string value)
	{
		transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(toupper(ch)); });
		return value == "1" || value == "TRUE" || value == "YES";
	}

	bool ReadRows(const string& path, const vector<string>& required,
		function<bool(const unordered_map<string, size_t>&, const vector<string>&, int32)> visitor)
	{
		ifstream file(path);
		if (!file.is_open()) { cout << "[QuestDataManager] Failed to read " << path << endl; return false; }
		string line;
		if (!getline(file, line)) return false;
		const vector<string> header = ParseCsvLine(line);
		unordered_map<string, size_t> columns;
		for (size_t i = 0; i < header.size(); ++i) columns[header[i]] = i;
		for (const string& name : required)
			if (!columns.contains(name)) { cout << "[QuestDataManager] Missing column " << name << " in " << path << endl; return false; }
		getline(file, line); // type row
		int32 lineNumber = 2;
		while (getline(file, line))
		{
			++lineNumber;
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (Trim(line).empty()) continue;
			if (!visitor(columns, ParseCsvLine(line), lineNumber)) return false;
		}
		return true;
	}

	string Cell(const unordered_map<string, size_t>& columns, const vector<string>& row, const string& name)
	{
		const size_t index = columns.at(name);
		return index < row.size() ? row[index] : "";
	}

	bool ParseObjectiveType(const string& value, QuestObjectiveType& out)
	{
		if (value == "VISIT_VILLAGE") out = QuestObjectiveType::VisitVillage;
		else if (value == "OWN_ITEM") out = QuestObjectiveType::OwnItem;
		else if (value == "DELIVER_ITEM") out = QuestObjectiveType::DeliverItem;
		else if (value == "BUY_ITEM") out = QuestObjectiveType::BuyItem;
		else if (value == "SELL_TRADE_GOOD") out = QuestObjectiveType::SellTradeGood;
		else return false;
		return true;
	}

	bool ParseRewardType(const string& value, QuestRewardType& out)
	{
		if (value == "GOLD") out = QuestRewardType::Gold;
		else if (value == "FAME") out = QuestRewardType::Fame;
		else if (value == "ITEM") out = QuestRewardType::Item;
		else return false;
		return true;
	}
}

bool QuestDataManager::Load()
{
	if (_loaded) return true;
	_questsById.clear();
	_loaded = LoadQuests() && LoadObjectives() && LoadRewards() && Validate();
	cout << "[QuestDataManager] Load " << (_loaded ? "success" : "failed")
		<< " quests=" << _questsById.size() << endl;
	return _loaded;
}

const QuestTemplate* QuestDataManager::GetQuest(const string& questId) const
{
	const auto it = _questsById.find(questId);
	return it != _questsById.end() ? &it->second : nullptr;
}

vector<const QuestTemplate*> QuestDataManager::GetQuestsForVillage(const string& villageId) const
{
	vector<const QuestTemplate*> result;
	for (const auto& pair : _questsById)
	{
		const QuestTemplate& quest = pair.second;
		if (quest.enabled && (quest.startVillageId == villageId || quest.completionVillageId == villageId))
			result.push_back(&quest);
	}
	sort(result.begin(), result.end(), [](const QuestTemplate* lhs, const QuestTemplate* rhs)
		{ return lhs->sortOrder != rhs->sortOrder ? lhs->sortOrder < rhs->sortOrder : lhs->questId < rhs->questId; });
	return result;
}

bool QuestDataManager::LoadQuests()
{
	return ReadRows(ResolveDataPath("Quest.csv"),
		{ "QuestId", "DisplayName", "Description", "Category", "StartVillageId", "CompletionVillageId", "PrerequisiteQuestId", "Enabled", "SortOrder" },
		[this](const auto& columns, const auto& row, int32 line)
		{
			QuestTemplate quest;
			quest.questId = Cell(columns, row, "QuestId");
			quest.displayName = Cell(columns, row, "DisplayName");
			quest.description = Cell(columns, row, "Description");
			quest.category = Cell(columns, row, "Category");
			quest.startVillageId = Cell(columns, row, "StartVillageId");
			quest.completionVillageId = Cell(columns, row, "CompletionVillageId");
			quest.prerequisiteQuestId = Cell(columns, row, "PrerequisiteQuestId");
			quest.enabled = ToBool(Cell(columns, row, "Enabled"));
			try { quest.sortOrder = stoi(Cell(columns, row, "SortOrder")); } catch (...) { return false; }
			if (quest.questId.empty() || quest.displayName.empty() || !_questsById.emplace(quest.questId, move(quest)).second)
			{
				cout << "[QuestDataManager] Invalid or duplicate quest row " << line << endl;
				return false;
			}
			return true;
		});
}

bool QuestDataManager::LoadObjectives()
{
	return ReadRows(ResolveDataPath("QuestObjective.csv"),
		{ "QuestId", "ObjectiveIndex", "ObjectiveType", "TargetVillageId", "TargetItemId", "RequiredCount" },
		[this](const auto& columns, const auto& row, int32 line)
		{
			const string questId = Cell(columns, row, "QuestId");
			auto quest = _questsById.find(questId);
			QuestObjectiveTemplate objective;
			try { objective.objectiveIndex = static_cast<uint32>(stoul(Cell(columns, row, "ObjectiveIndex"))); objective.requiredCount = stoi(Cell(columns, row, "RequiredCount")); }
			catch (...) { return false; }
			objective.targetVillageId = Cell(columns, row, "TargetVillageId");
			objective.targetItemId = Cell(columns, row, "TargetItemId");
			if (quest == _questsById.end() || objective.objectiveIndex == 0 || objective.requiredCount <= 0 ||
				!ParseObjectiveType(Cell(columns, row, "ObjectiveType"), objective.type))
			{
				cout << "[QuestDataManager] Invalid objective row " << line << endl;
				return false;
			}
			quest->second.objectives.push_back(move(objective));
			return true;
		});
}

bool QuestDataManager::LoadRewards()
{
	return ReadRows(ResolveDataPath("QuestReward.csv"),
		{ "QuestId", "RewardIndex", "RewardType", "TargetId", "Amount" },
		[this](const auto& columns, const auto& row, int32 line)
		{
			const string questId = Cell(columns, row, "QuestId");
			auto quest = _questsById.find(questId);
			QuestRewardTemplate reward;
			try { reward.rewardIndex = static_cast<uint32>(stoul(Cell(columns, row, "RewardIndex"))); reward.amount = stoi(Cell(columns, row, "Amount")); }
			catch (...) { return false; }
			reward.targetId = Cell(columns, row, "TargetId");
			if (quest == _questsById.end() || reward.rewardIndex == 0 || reward.amount <= 0 ||
				!ParseRewardType(Cell(columns, row, "RewardType"), reward.type))
			{
				cout << "[QuestDataManager] Invalid reward row " << line << endl;
				return false;
			}
			quest->second.rewards.push_back(move(reward));
			return true;
		});
}

bool QuestDataManager::Validate() const
{
	auto isVillageToken = [](const string& value)
	{
		return value == "$ORIGIN" || value == "$DESTINATION";
	};
	auto isItemToken = [](const string& value)
	{
		return value == "$ORIGIN_FOOD" || value == "$ORIGIN_TRADE";
	};
	for (const auto& pair : _questsById)
	{
		const QuestTemplate& quest = pair.second;
		if ((!isVillageToken(quest.startVillageId) && GVillageData.GetVillage(quest.startVillageId) == nullptr) ||
			(!isVillageToken(quest.completionVillageId) && GVillageData.GetVillage(quest.completionVillageId) == nullptr) ||
			quest.objectives.empty() || quest.rewards.empty() ||
			(!quest.prerequisiteQuestId.empty() && !_questsById.contains(quest.prerequisiteQuestId))) return false;
		set<uint32> objectiveIndices;
		set<uint32> rewardIndices;
		set<string> dynamicItems;
		for (const QuestObjectiveTemplate& objective : quest.objectives)
		{
			if (!objectiveIndices.insert(objective.objectiveIndex).second) return false;
			if (!objective.targetVillageId.empty() && !isVillageToken(objective.targetVillageId) &&
				GVillageData.GetVillage(objective.targetVillageId) == nullptr) return false;
			if (!objective.targetItemId.empty() && !isItemToken(objective.targetItemId) &&
				GEconomyData.GetItem(objective.targetItemId) == nullptr) return false;
			if ((objective.type == QuestObjectiveType::VisitVillage && objective.targetVillageId.empty()) ||
				(objective.type != QuestObjectiveType::VisitVillage && objective.targetItemId.empty())) return false;
			if ((objective.type == QuestObjectiveType::OwnItem || objective.type == QuestObjectiveType::DeliverItem) &&
				!dynamicItems.insert(objective.targetItemId).second) return false;
		}
		for (const QuestRewardTemplate& reward : quest.rewards)
		{
			if (!rewardIndices.insert(reward.rewardIndex).second) return false;
			if (reward.type == QuestRewardType::Item && GEconomyData.GetItem(reward.targetId) == nullptr) return false;
		}
	}
	return true;
}

string QuestDataManager::ResolveDataPath(const string& fileName) const
{
	const vector<string> candidates = { "Data\\" + fileName, "..\\Data\\" + fileName, "..\\..\\Data\\" + fileName, "..\\..\\..\\Data\\" + fileName };
	for (const string& path : candidates) { ifstream file(path); if (file.is_open()) return path; }
	return candidates.front();
}
