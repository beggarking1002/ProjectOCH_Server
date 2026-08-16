#pragma once

class QuestService
{
public:
	void HandleBoardOpen(GameSessionRef session, const PlayerRef& player, const string& villageId);
	void HandleTrackerOpen(GameSessionRef session, const PlayerRef& player);
	void HandleAccept(GameSessionRef session, const PlayerRef& player, const string& questId);
	void HandleClaimReward(GameSessionRef session, const PlayerRef& player, const string& questId);

	bool OnVillageVisited(const PlayerRef& player, const string& villageId);
	bool OnItemPurchased(const PlayerRef& player, const string& villageId, const string& itemId, int32 quantity);
	bool OnTradeGoodSold(const PlayerRef& player, const string& villageId, const string& itemId, int32 quantity);
	bool ReevaluateInventoryObjectives(const PlayerRef& player);

private:
	bool ValidateBoardAccess(const PlayerRef& player, const string& villageId, string& reason) const;
	bool IsPrerequisiteComplete(const PlayerRef& player, const string& prerequisiteTemplateId) const;
	bool EnsureBoardFilled(const PlayerRef& player, const string& villageId);
	bool GenerateQuestInstance(const PlayerRef& player, const string& villageId, int32 boardSlot);
	bool ReevaluateQuest(const PlayerRef& player, struct PlayerQuestState& state);
	bool AdvanceMatchingObjectives(const PlayerRef& player, const string& villageId, const string& itemId,
		int32 quantity, int32 eventKind);
	void SendBoardState(GameSessionRef session, const PlayerRef& player, const string& villageId,
		const string& action, bool success, const string& reason) const;
	void SendTrackerState(GameSessionRef session, const PlayerRef& player, bool success, const string& reason) const;
};

extern QuestService GQuestService;
