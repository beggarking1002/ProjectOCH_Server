#include "pch.h"
#include "QuestService.h"
#include "QuestDataManager.h"
#include "VillageDataManager.h"
#include "EconomyDataManager.h"
#include "DatabaseManager.h"
#include "EconomyService.h"
#include "Player.h"
#include "GameSession.h"
#include "ServerPacketHandler.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

QuestService GQuestService;

namespace
{
	constexpr int32 kVisitEvent = 1;
	constexpr int32 kBuyEvent = 2;
	constexpr int32 kSellEvent = 3;
	constexpr int32 kBoardSlotCount = 3;
	constexpr int32 kMaxActiveQuestCount = 3;

	uint64 UnixTimeMs()
	{
		return static_cast<uint64>(chrono::duration_cast<chrono::milliseconds>(
			chrono::system_clock::now().time_since_epoch()).count());
	}

	mt19937_64& RandomEngine()
	{
		static thread_local mt19937_64 engine(random_device{}());
		return engine;
	}

	string MakeQuestInstanceId(const PlayerRef& player)
	{
		ostringstream stream;
		stream << "qi_" << hex << UnixTimeMs() << '_' << RandomEngine()();
		if (player != nullptr && player->FindQuestState(stream.str()) != nullptr)
			stream << '_' << RandomEngine()();
		return stream.str();
	}

	void ReplaceAll(string& value, const string& token, const string& replacement)
	{
		if (token.empty()) return;
		size_t position = 0;
		while ((position = value.find(token, position)) != string::npos)
		{
			value.replace(position, token.size(), replacement);
			position += replacement.size();
		}
	}

	const char* ObjectiveTypeName(QuestObjectiveType type)
	{
		switch (type)
		{
		case QuestObjectiveType::VisitVillage: return "VISIT_VILLAGE";
		case QuestObjectiveType::OwnItem: return "OWN_ITEM";
		case QuestObjectiveType::DeliverItem: return "DELIVER_ITEM";
		case QuestObjectiveType::BuyItem: return "BUY_ITEM";
		case QuestObjectiveType::SellTradeGood: return "SELL_TRADE_GOOD";
		default: return "";
		}
	}

	const char* RewardTypeName(QuestRewardType type)
	{
		if (type == QuestRewardType::Item) return "ITEM";
		if (type == QuestRewardType::Fame) return "FAME";
		return "GOLD";
	}

	PlayerQuestObjectiveState* FindObjective(PlayerQuestState& state, uint32 objectiveIndex)
	{
		auto it = find_if(state.objectives.begin(), state.objectives.end(), [objectiveIndex](const PlayerQuestObjectiveState& objective)
			{ return objective.objectiveIndex == objectiveIndex; });
		return it != state.objectives.end() ? &*it : nullptr;
	}

	string StatusName(PlayerQuestStatus status)
	{
		if (status == PlayerQuestStatus::Available) return "AVAILABLE";
		if (status == PlayerQuestStatus::Completed) return "COMPLETED";
		if (status == PlayerQuestStatus::Ready) return "READY";
		return "ACTIVE";
	}

	string FirstTargetItemId(const PlayerQuestState& state)
	{
		for (const PlayerQuestObjectiveState& objective : state.objectives)
			if (!objective.targetItemId.empty()) return objective.targetItemId;
		return "";
	}

	int32 CountActiveQuests(const PlayerRef& player)
	{
		if (player == nullptr) return 0;
		return static_cast<int32>(count_if(player->QuestStates().begin(), player->QuestStates().end(), [](const PlayerQuestState& quest)
		{
			return quest.status == PlayerQuestStatus::Active || quest.status == PlayerQuestStatus::Ready;
		}));
	}

	bool TryCalculateAbandonPenalty(const PlayerQuestState& quest, int32& outGold, int32& outFame)
	{
		int64 gold = 0;
		int64 fame = 0;
		for (const PlayerQuestRewardState& reward : quest.rewards)
		{
			if (reward.rewardType == "GOLD") gold += static_cast<int64>(reward.amount) * 2;
			else if (reward.rewardType == "FAME") fame += static_cast<int64>(reward.amount) * 2;
		}
		if (gold < 0 || fame < 0 || gold > (numeric_limits<int32>::max)() || fame > (numeric_limits<int32>::max)())
			return false;
		outGold = static_cast<int32>(gold);
		outFame = static_cast<int32>(fame);
		return true;
	}
}

void QuestService::HandleBoardOpen(GameSessionRef session, const PlayerRef& player, const string& villageId)
{
	string reason;
	if (!ValidateBoardAccess(player, villageId, reason))
	{
		SendBoardState(session, player, villageId, "open", false, reason);
		return;
	}

	const uint64 nowTickMs = ::GetTickCount64();
	const PersistentPlayerEconomyState previous = player->ExportEconomyState();
	const bool changed = EnsureBoardFilled(player, villageId) | ReevaluateInventoryObjectives(player);
	if (changed && player->hasPersistentIdentity && GDatabase.IsEnabled() &&
		!GDatabase.SavePlayerEconomy(player->objectInfo->object_id(), player->ExportEconomyState()))
	{
		player->RestoreEconomyState(previous, nowTickMs);
		player->MarkEconomyDirty();
		SendBoardState(session, player, villageId, "open", false, "failed to persist quest board");
		return;
	}
	if (changed) player->MarkEconomyPersisted(nowTickMs);
	SendBoardState(session, player, villageId, "open", true, "");
}

void QuestService::HandleTrackerOpen(GameSessionRef session, const PlayerRef& player)
{
	if (player == nullptr)
	{
		SendTrackerState(session, player, false, "player must be in the field");
		return;
	}

	SendTrackerState(session, player, true, "");
}

void QuestService::HandleAccept(GameSessionRef session, const PlayerRef& player, const string& questId)
{
	const string villageId = player != nullptr ? player->activeVillageId : "";
	PlayerQuestState* quest = player != nullptr ? player->FindQuestState(questId) : nullptr;
	string reason;
	if (quest == nullptr || quest->status != PlayerQuestStatus::Available)
		reason = "quest is unavailable";
	else if (!ValidateBoardAccess(player, quest->startVillageId, reason)) {}
	else if (CountActiveQuests(player) >= kMaxActiveQuestCount)
		reason = "active quest limit reached (3)";
	else if (!IsPrerequisiteComplete(player, quest->prerequisiteTemplateId))
		reason = "quest prerequisite is not complete";
	if (!reason.empty())
	{
		SendBoardState(session, player, villageId, "accept", false, reason);
		return;
	}

	const uint64 nowTickMs = ::GetTickCount64();
	const PersistentPlayerEconomyState previous = player->ExportEconomyState();
	quest->status = PlayerQuestStatus::Active;
	quest->acceptedAtMs = UnixTimeMs();
	player->MarkEconomyDirty();
	ReevaluateQuest(player, *quest);

	if (player->hasPersistentIdentity && GDatabase.IsEnabled() &&
		!GDatabase.SavePlayerEconomy(player->objectInfo->object_id(), player->ExportEconomyState()))
	{
		player->RestoreEconomyState(previous, nowTickMs);
		player->MarkEconomyDirty();
		SendBoardState(session, player, villageId, "accept", false, "failed to persist accepted quest");
		return;
	}
	player->MarkEconomyPersisted(nowTickMs);
	cout << "QUEST_ACCEPT player_id=" << player->objectInfo->object_id() << " quest_id=" << questId << endl;
	SendBoardState(session, player, villageId, "accept", true, "");
}

void QuestService::HandleClaimReward(GameSessionRef session, const PlayerRef& player, const string& questId)
{
	const string villageId = player != nullptr ? player->activeVillageId : "";
	PlayerQuestState* quest = player != nullptr ? player->FindQuestState(questId) : nullptr;
	string reason;
	if (quest == nullptr || quest->status == PlayerQuestStatus::Available || quest->status == PlayerQuestStatus::Completed)
		reason = "quest is not awaiting a reward";
	else if (!ValidateBoardAccess(player, quest->completionVillageId, reason)) {}
	if (!reason.empty())
	{
		SendBoardState(session, player, villageId, "claim", false, reason);
		return;
	}

	const uint64 nowTickMs = ::GetTickCount64();
	const PersistentPlayerEconomyState previous = player->ExportEconomyState();
	ReevaluateQuest(player, *quest);
	if (quest->status != PlayerQuestStatus::Ready)
	{
		SendBoardState(session, player, villageId, "claim", false, "quest objectives are incomplete");
		return;
	}

	for (const PlayerQuestObjectiveState& objective : quest->objectives)
	{
		if (objective.objectiveType == "DELIVER_ITEM" &&
			!player->RemoveInventoryItemFefo(objective.targetItemId, objective.requiredCount))
		{
			player->RestoreEconomyState(previous, nowTickMs);
			SendBoardState(session, player, villageId, "claim", false, "delivery items are unavailable");
			return;
		}
	}

	vector<string> autoConsumedItemIds;
	for (const PlayerQuestRewardState& reward : quest->rewards)
	{
		if (reward.rewardType == "GOLD")
			player->AddGold(reward.amount);
		else if (reward.rewardType == "FAME")
			player->ModifyFame(reward.amount);
		else
		{
			const EconomyItemTemplate* item = GEconomyData.GetItem(reward.targetId);
			if (item == nullptr || !player->AddInventoryItem(*item, reward.amount, autoConsumedItemIds))
			{
				player->RestoreEconomyState(previous, nowTickMs);
				SendBoardState(session, player, villageId, "claim", false, "failed to grant quest reward");
				return;
			}
		}
	}

	quest = player->FindQuestState(questId);
	const string originVillageId = quest->startVillageId;
	quest->status = PlayerQuestStatus::Completed;
	quest->completedAtMs = UnixTimeMs();
	quest->boardSlot = -1;
	player->MarkEconomyDirty();
	ReevaluateInventoryObjectives(player);
	EnsureBoardFilled(player, originVillageId);

	if (player->hasPersistentIdentity && GDatabase.IsEnabled() &&
		!GDatabase.SavePlayerEconomy(player->objectInfo->object_id(), player->ExportEconomyState()))
	{
		player->RestoreEconomyState(previous, nowTickMs);
		player->MarkEconomyDirty();
		SendBoardState(session, player, villageId, "claim", false, "failed to persist quest reward");
		return;
	}
	player->MarkEconomyPersisted(nowTickMs);
	cout << "QUEST_COMPLETE player_id=" << player->objectInfo->object_id() << " quest_id=" << questId << endl;
	GEconomyService.SendExpeditionState(player, autoConsumedItemIds);
	SendBoardState(session, player, villageId, "claim", true, "");
}

void QuestService::HandleAbandon(GameSessionRef session, const PlayerRef& player, const string& questId)
{
	PlayerQuestState* quest = player != nullptr ? player->FindQuestState(questId) : nullptr;
	int32 goldPenalty = 0;
	int32 famePenalty = 0;
	string reason;
	if (player == nullptr)
		reason = "player must be in the field";
	else if (quest == nullptr || (quest->status != PlayerQuestStatus::Active && quest->status != PlayerQuestStatus::Ready))
		reason = "quest cannot be abandoned";
	else if (!TryCalculateAbandonPenalty(*quest, goldPenalty, famePenalty))
		reason = "quest abandon penalty is invalid";
	else if (player->Gold() < goldPenalty)
		reason = "not enough gold to pay the abandon penalty";
	if (!reason.empty())
	{
		SendTrackerState(session, player, false, reason, "abandon", goldPenalty, famePenalty);
		return;
	}

	const uint64 nowTickMs = ::GetTickCount64();
	const PersistentPlayerEconomyState previous = player->ExportEconomyState();
	const string originVillageId = quest->startVillageId;
	if (!player->SpendGold(goldPenalty))
	{
		SendTrackerState(session, player, false, "failed to pay the abandon penalty", "abandon", goldPenalty, famePenalty);
		return;
	}
	player->ModifyFame(-famePenalty);
	quest = player->FindQuestState(questId);
	quest->status = PlayerQuestStatus::Abandoned;
	quest->boardSlot = -1;
	player->MarkEconomyDirty();
	EnsureBoardFilled(player, originVillageId);

	if (player->hasPersistentIdentity && GDatabase.IsEnabled() &&
		!GDatabase.SavePlayerEconomy(player->objectInfo->object_id(), player->ExportEconomyState()))
	{
		player->RestoreEconomyState(previous, nowTickMs);
		player->MarkEconomyDirty();
		SendTrackerState(session, player, false, "failed to persist abandoned quest", "abandon", goldPenalty, famePenalty);
		return;
	}
	player->MarkEconomyPersisted(nowTickMs);
	cout << "QUEST_ABANDON player_id=" << player->objectInfo->object_id() << " quest_id=" << questId
		<< " gold_penalty=" << goldPenalty << " fame_penalty=" << famePenalty << endl;
	GEconomyService.SendExpeditionState(player);
	SendTrackerState(session, player, true, "", "abandon", goldPenalty, famePenalty);
}

bool QuestService::OnVillageVisited(const PlayerRef& player, const string& villageId)
{
	return AdvanceMatchingObjectives(player, villageId, "", 1, kVisitEvent);
}

bool QuestService::OnItemPurchased(const PlayerRef& player, const string& villageId, const string& itemId, int32 quantity)
{
	const bool progressChanged = AdvanceMatchingObjectives(player, villageId, itemId, quantity, kBuyEvent);
	return ReevaluateInventoryObjectives(player) || progressChanged;
}

bool QuestService::OnTradeGoodSold(const PlayerRef& player, const string& villageId, const string& itemId, int32 quantity)
{
	const bool progressChanged = AdvanceMatchingObjectives(player, villageId, itemId, quantity, kSellEvent);
	return ReevaluateInventoryObjectives(player) || progressChanged;
}

bool QuestService::ReevaluateInventoryObjectives(const PlayerRef& player)
{
	if (player == nullptr) return false;
	bool changed = false;
	for (const PlayerQuestState& value : player->QuestStates())
	{
		PlayerQuestState* state = player->FindQuestState(value.questId);
		if (state != nullptr && state->status != PlayerQuestStatus::Available && state->status != PlayerQuestStatus::Completed &&
			state->status != PlayerQuestStatus::Abandoned)
			changed = ReevaluateQuest(player, *state) || changed;
	}
	return changed;
}

bool QuestService::ValidateBoardAccess(const PlayerRef& player, const string& villageId, string& reason) const
{
	if (player == nullptr) { reason = "player is unavailable"; return false; }
	if (villageId.empty() || player->activeVillageId != villageId) { reason = "player is not interacting with this village"; return false; }
	const VillageTemplate* village = GVillageData.GetVillage(villageId);
	if (village == nullptr || !village->questEnabled) { reason = "village quest board is unavailable"; return false; }
	return true;
}

bool QuestService::IsPrerequisiteComplete(const PlayerRef& player, const string& prerequisiteTemplateId) const
{
	if (prerequisiteTemplateId.empty()) return true;
	if (player == nullptr) return false;
	for (const PlayerQuestState& state : player->QuestStates())
		if (state.templateId == prerequisiteTemplateId && state.status == PlayerQuestStatus::Completed) return true;
	return false;
}

bool QuestService::EnsureBoardFilled(const PlayerRef& player, const string& villageId)
{
	if (player == nullptr || GVillageData.GetVillage(villageId) == nullptr) return false;
	bool changed = false;
	for (int32 slot = 0; slot < kBoardSlotCount; ++slot)
	{
		const bool occupied = any_of(player->QuestStates().begin(), player->QuestStates().end(), [&villageId, slot](const PlayerQuestState& quest)
			{ return quest.status != PlayerQuestStatus::Completed && quest.status != PlayerQuestStatus::Abandoned &&
				quest.startVillageId == villageId && quest.boardSlot == slot; });
		if (!occupied)
			changed = GenerateQuestInstance(player, villageId, slot) || changed;
	}
	return changed;
}

bool QuestService::GenerateQuestInstance(const PlayerRef& player, const string& villageId, int32 boardSlot)
{
	vector<const QuestTemplate*> templates;
	for (const auto& pair : GQuestData.GetAllQuests())
		if (pair.second.enabled && (pair.second.startVillageId == "$ORIGIN" || pair.second.startVillageId == villageId))
			templates.push_back(&pair.second);
	if (templates.empty()) return false;

	vector<string> destinations;
	for (const auto& pair : GVillageData.GetAllVillages())
		if (pair.first != villageId && pair.second.questEnabled) destinations.push_back(pair.first);
	if (destinations.empty()) return false;

	shuffle(templates.begin(), templates.end(), RandomEngine());
	shuffle(destinations.begin(), destinations.end(), RandomEngine());
	for (int32 attempt = 0; attempt < 64; ++attempt)
	{
		const QuestTemplate& source = *templates[static_cast<size_t>(attempt) % templates.size()];
		const string destinationId = destinations[static_cast<size_t>(attempt / (max)(size_t{ 1 }, templates.size())) % destinations.size()];
		PlayerQuestState generated;
		generated.questId = MakeQuestInstanceId(player);
		generated.templateId = source.questId;
		generated.displayName = source.displayName;
		generated.description = source.description;
		generated.category = source.category;
		generated.startVillageId = source.startVillageId == "$ORIGIN" ? villageId : source.startVillageId;
		generated.completionVillageId = source.completionVillageId == "$DESTINATION" ? destinationId :
			(source.completionVillageId == "$ORIGIN" ? villageId : source.completionVillageId);
		generated.prerequisiteTemplateId = source.prerequisiteQuestId;
		generated.boardSlot = boardSlot;
		generated.status = PlayerQuestStatus::Available;

		string displayItemName;
		int32 displayCount = 0;
		bool valid = generated.startVillageId != generated.completionVillageId;
		for (const QuestObjectiveTemplate& objective : source.objectives)
		{
			PlayerQuestObjectiveState resolved;
			resolved.objectiveIndex = objective.objectiveIndex;
			resolved.objectiveType = ObjectiveTypeName(objective.type);
			resolved.targetVillageId = objective.targetVillageId == "$DESTINATION" ? generated.completionVillageId :
				(objective.targetVillageId == "$ORIGIN" ? generated.startVillageId : objective.targetVillageId);
			resolved.targetItemId = objective.targetItemId;
			resolved.requiredCount = objective.requiredCount;
			if (objective.targetItemId == "$ORIGIN_FOOD" || objective.targetItemId == "$ORIGIN_TRADE")
			{
				vector<string> candidates;
				const vector<VillageShopStockTemplate>* stock = GEconomyData.GetVillageShopStock(generated.startVillageId);
				if (stock != nullptr)
				{
					const EconomyItemType wanted = objective.targetItemId == "$ORIGIN_FOOD" ? EconomyItemType::Food : EconomyItemType::Trade;
					for (const VillageShopStockTemplate& entry : *stock)
					{
						const EconomyItemTemplate* item = GEconomyData.GetItem(entry.itemId);
						if (item != nullptr && item->itemType == wanted && entry.maxStock >= objective.requiredCount)
							candidates.push_back(entry.itemId);
					}
				}
				if (candidates.empty()) { valid = false; break; }
				shuffle(candidates.begin(), candidates.end(), RandomEngine());
				resolved.targetItemId = candidates.front();
			}
			const EconomyItemTemplate* item = GEconomyData.GetItem(resolved.targetItemId);
			if (item != nullptr && displayItemName.empty())
			{
				displayItemName = item->displayName;
				displayCount = resolved.requiredCount;
			}
			generated.objectives.push_back(move(resolved));
		}
		for (const QuestRewardTemplate& reward : source.rewards)
			generated.rewards.push_back({ reward.rewardIndex, RewardTypeName(reward.type), reward.targetId, reward.amount });
		if (!valid || generated.objectives.empty() || generated.rewards.empty()) continue;

		const string targetItemId = FirstTargetItemId(generated);
		const bool duplicate = any_of(player->QuestStates().begin(), player->QuestStates().end(), [&generated, &targetItemId](const PlayerQuestState& existing)
		{
			return existing.status != PlayerQuestStatus::Completed && existing.status != PlayerQuestStatus::Abandoned &&
				existing.templateId == generated.templateId &&
				existing.startVillageId == generated.startVillageId && existing.completionVillageId == generated.completionVillageId &&
				FirstTargetItemId(existing) == targetItemId;
		});
		if (duplicate) continue;

		const VillageTemplate* origin = GVillageData.GetVillage(generated.startVillageId);
		const VillageTemplate* destination = GVillageData.GetVillage(generated.completionVillageId);
		const string originName = origin != nullptr ? origin->name : generated.startVillageId;
		const string destinationName = destination != nullptr ? destination->name : generated.completionVillageId;
		ReplaceAll(generated.displayName, "{origin}", originName);
		ReplaceAll(generated.displayName, "{destination}", destinationName);
		ReplaceAll(generated.displayName, "{item}", displayItemName);
		ReplaceAll(generated.displayName, "{count}", to_string(displayCount));
		ReplaceAll(generated.description, "{origin}", originName);
		ReplaceAll(generated.description, "{destination}", destinationName);
		ReplaceAll(generated.description, "{item}", displayItemName);
		ReplaceAll(generated.description, "{count}", to_string(displayCount));
		if (player->AddQuestState(move(generated))) return true;
	}
	return false;
}

bool QuestService::ReevaluateQuest(const PlayerRef& player, PlayerQuestState& state)
{
	if (state.status == PlayerQuestStatus::Available || state.status == PlayerQuestStatus::Completed ||
		state.status == PlayerQuestStatus::Abandoned) return false;
	bool changed = false;
	bool allComplete = true;
	for (PlayerQuestObjectiveState& objective : state.objectives)
	{
		int32 value = (max)(0, (min)(objective.progress, objective.requiredCount));
		if (objective.objectiveType == "OWN_ITEM" || objective.objectiveType == "DELIVER_ITEM")
			value = (min)(player->CountInventoryItem(objective.targetItemId), objective.requiredCount);
		if (objective.progress != value) { objective.progress = value; changed = true; }
		if (objective.progress < objective.requiredCount) allComplete = false;
	}
	const PlayerQuestStatus desired = allComplete ? PlayerQuestStatus::Ready : PlayerQuestStatus::Active;
	if (state.status != desired)
	{
		state.status = desired;
		state.readyAtMs = desired == PlayerQuestStatus::Ready ? UnixTimeMs() : 0;
		changed = true;
	}
	if (changed) player->MarkEconomyDirty();
	return changed;
}

bool QuestService::AdvanceMatchingObjectives(const PlayerRef& player, const string& villageId, const string& itemId,
	int32 quantity, int32 eventKind)
{
	if (player == nullptr || quantity <= 0) return false;
	bool changed = false;
	for (const PlayerQuestState& value : player->QuestStates())
	{
		PlayerQuestState* state = player->FindQuestState(value.questId);
		if (state == nullptr || state->status == PlayerQuestStatus::Available || state->status == PlayerQuestStatus::Completed ||
			state->status == PlayerQuestStatus::Abandoned) continue;
		for (PlayerQuestObjectiveState& objective : state->objectives)
		{
			const bool typeMatches = (eventKind == kVisitEvent && objective.objectiveType == "VISIT_VILLAGE") ||
				(eventKind == kBuyEvent && objective.objectiveType == "BUY_ITEM") ||
				(eventKind == kSellEvent && objective.objectiveType == "SELL_TRADE_GOOD");
			if (!typeMatches || (!objective.targetVillageId.empty() && objective.targetVillageId != villageId) ||
				(eventKind != kVisitEvent && objective.targetItemId != itemId)) continue;
			const int64 next = static_cast<int64>(objective.progress) + quantity;
			const int32 clamped = static_cast<int32>((min)(static_cast<int64>(objective.requiredCount), next));
			if (clamped != objective.progress) { objective.progress = clamped; changed = true; }
		}
		changed = ReevaluateQuest(player, *state) || changed;
	}
	if (changed) player->MarkEconomyDirty();
	return changed;
}

void QuestService::SendBoardState(GameSessionRef session, const PlayerRef& player, const string& villageId,
	const string& action, bool success, const string& reason) const
{
	if (session == nullptr) return;
	Protocol::S_VILLAGE_QUEST_STATE packet;
	packet.set_success(success);
	packet.set_reason(reason);
	packet.set_action(action);
	packet.set_village_id(villageId);
	if (player != nullptr) player->FillExpeditionState(*packet.mutable_expedition());

	vector<const PlayerQuestState*> visible;
	const bool activeQuestLimitReached = CountActiveQuests(player) >= kMaxActiveQuestCount;
	if (player != nullptr)
	{
		for (const PlayerQuestState& quest : player->QuestStates())
		{
			if (quest.status == PlayerQuestStatus::Completed || quest.status == PlayerQuestStatus::Abandoned) continue;
			const bool atOrigin = quest.startVillageId == villageId;
			const bool atDestination = quest.status != PlayerQuestStatus::Available && quest.completionVillageId == villageId;
			if (atOrigin || atDestination) visible.push_back(&quest);
		}
	}
	auto displayPriority = [&villageId](const PlayerQuestState* quest)
	{
		if (quest->completionVillageId == villageId && quest->status == PlayerQuestStatus::Ready) return 0;
		if (quest->completionVillageId == villageId && quest->status == PlayerQuestStatus::Active) return 1;
		if (quest->startVillageId == villageId && quest->status != PlayerQuestStatus::Available) return 2;
		return 3;
	};
	sort(visible.begin(), visible.end(), [&displayPriority](const PlayerQuestState* left, const PlayerQuestState* right)
	{
		const int32 leftPriority = displayPriority(left);
		const int32 rightPriority = displayPriority(right);
		if (leftPriority != rightPriority) return leftPriority < rightPriority;
		if (left->startVillageId == right->startVillageId && left->boardSlot != right->boardSlot)
			return left->boardSlot < right->boardSlot;
		return left->questId < right->questId;
	});

	for (const PlayerQuestState* quest : visible)
	{
		Protocol::VillageQuestInfo* info = packet.add_quests();
		info->set_quest_id(quest->questId);
		info->set_display_name(quest->displayName);
		info->set_description(quest->description);
		info->set_status(quest->status == PlayerQuestStatus::Available && activeQuestLimitReached
			? "LIMIT_REACHED" : StatusName(quest->status));
		info->set_start_village_id(quest->startVillageId);
		info->set_completion_village_id(quest->completionVillageId);
		const VillageTemplate* completionVillage = GVillageData.GetVillage(quest->completionVillageId);
		info->set_completion_village_name(completionVillage != nullptr ? completionVillage->name : quest->completionVillageId);
		info->set_can_accept(!activeQuestLimitReached && quest->status == PlayerQuestStatus::Available && quest->startVillageId == villageId &&
			IsPrerequisiteComplete(player, quest->prerequisiteTemplateId));
		info->set_can_claim(quest->status == PlayerQuestStatus::Ready && quest->completionVillageId == villageId);
		int32 abandonGoldPenalty = 0;
		int32 abandonFamePenalty = 0;
		if (TryCalculateAbandonPenalty(*quest, abandonGoldPenalty, abandonFamePenalty))
		{
			info->set_abandon_gold_penalty(abandonGoldPenalty);
			info->set_abandon_fame_penalty(abandonFamePenalty);
		}
		for (const PlayerQuestObjectiveState& objective : quest->objectives)
		{
			const VillageTemplate* targetVillage = GVillageData.GetVillage(objective.targetVillageId);
			const EconomyItemTemplate* targetItem = GEconomyData.GetItem(objective.targetItemId);
			string description;
			if (objective.objectiveType == "VISIT_VILLAGE") description = "Visit " + (targetVillage ? targetVillage->name : objective.targetVillageId);
			else if (objective.objectiveType == "OWN_ITEM") description = "Own " + (targetItem ? targetItem->displayName : objective.targetItemId);
			else if (objective.objectiveType == "DELIVER_ITEM") description = "Deliver " + (targetItem ? targetItem->displayName : objective.targetItemId) + " to " + (targetVillage ? targetVillage->name : objective.targetVillageId);
			else if (objective.objectiveType == "BUY_ITEM") description = "Buy " + (targetItem ? targetItem->displayName : objective.targetItemId);
			else description = "Sell " + (targetItem ? targetItem->displayName : objective.targetItemId) + " at " + (targetVillage ? targetVillage->name : objective.targetVillageId);
			Protocol::QuestObjectiveProgressInfo* objectiveInfo = info->add_objectives();
			objectiveInfo->set_objective_index(objective.objectiveIndex);
			objectiveInfo->set_description(description);
			objectiveInfo->set_progress(objective.progress);
			objectiveInfo->set_required_count(objective.requiredCount);
			objectiveInfo->set_completed(objective.progress >= objective.requiredCount);
			objectiveInfo->set_objective_type(objective.objectiveType);
			objectiveInfo->set_target_village_name(targetVillage != nullptr ? targetVillage->name : objective.targetVillageId);
			objectiveInfo->set_target_item_name(targetItem != nullptr ? targetItem->displayName : objective.targetItemId);
		}
		for (const PlayerQuestRewardState& reward : quest->rewards)
		{
			if (reward.rewardType == "GOLD") info->add_reward_descriptions(to_string(reward.amount) + "G");
			else if (reward.rewardType == "FAME") info->add_reward_descriptions("Fame +" + to_string(reward.amount));
			else
			{
				const EconomyItemTemplate* item = GEconomyData.GetItem(reward.targetId);
				info->add_reward_descriptions((item ? item->displayName : reward.targetId) + " x" + to_string(reward.amount));
			}
		}
	}
	session->Send(ServerPacketHandler::MakeSendBuffer(packet));
}

void QuestService::SendTrackerState(GameSessionRef session, const PlayerRef& player, bool success, const string& reason,
	const string& action, int32 goldPenalty, int32 famePenalty) const
{
	if (session == nullptr) return;
	Protocol::S_QUEST_TRACKER_STATE packet;
	packet.set_success(success);
	packet.set_reason(reason);
	packet.set_action(action);
	packet.set_gold_penalty(goldPenalty);
	packet.set_fame_penalty(famePenalty);
	if (player != nullptr) player->FillExpeditionState(*packet.mutable_expedition());

	if (success && player != nullptr)
	{
		for (const PlayerQuestState& quest : player->QuestStates())
		{
			if (quest.status != PlayerQuestStatus::Active && quest.status != PlayerQuestStatus::Ready)
				continue;

			Protocol::VillageQuestInfo* info = packet.add_quests();
			info->set_quest_id(quest.questId);
			info->set_display_name(quest.displayName);
			info->set_description(quest.description);
			info->set_status(StatusName(quest.status));
			info->set_start_village_id(quest.startVillageId);
			info->set_completion_village_id(quest.completionVillageId);
			const VillageTemplate* completionVillage = GVillageData.GetVillage(quest.completionVillageId);
			info->set_completion_village_name(completionVillage != nullptr ? completionVillage->name : quest.completionVillageId);
			// Claims remain village-authoritative; the tracker only exposes abandonment.
			info->set_can_accept(false);
			info->set_can_claim(false);
			int32 abandonGoldPenalty = 0;
			int32 abandonFamePenalty = 0;
			if (TryCalculateAbandonPenalty(quest, abandonGoldPenalty, abandonFamePenalty))
			{
				info->set_abandon_gold_penalty(abandonGoldPenalty);
				info->set_abandon_fame_penalty(abandonFamePenalty);
			}
			for (const PlayerQuestObjectiveState& objective : quest.objectives)
			{
				const VillageTemplate* targetVillage = GVillageData.GetVillage(objective.targetVillageId);
				const EconomyItemTemplate* targetItem = GEconomyData.GetItem(objective.targetItemId);
				string description;
				if (objective.objectiveType == "VISIT_VILLAGE") description = "Visit " + (targetVillage ? targetVillage->name : objective.targetVillageId);
				else if (objective.objectiveType == "OWN_ITEM") description = "Own " + (targetItem ? targetItem->displayName : objective.targetItemId);
				else if (objective.objectiveType == "DELIVER_ITEM") description = "Deliver " + (targetItem ? targetItem->displayName : objective.targetItemId) + " to " + (targetVillage ? targetVillage->name : objective.targetVillageId);
				else if (objective.objectiveType == "BUY_ITEM") description = "Buy " + (targetItem ? targetItem->displayName : objective.targetItemId);
				else description = "Sell " + (targetItem ? targetItem->displayName : objective.targetItemId) + " at " + (targetVillage ? targetVillage->name : objective.targetVillageId);
				Protocol::QuestObjectiveProgressInfo* objectiveInfo = info->add_objectives();
				objectiveInfo->set_objective_index(objective.objectiveIndex);
				objectiveInfo->set_description(description);
				objectiveInfo->set_progress(objective.progress);
				objectiveInfo->set_required_count(objective.requiredCount);
				objectiveInfo->set_completed(objective.progress >= objective.requiredCount);
				objectiveInfo->set_objective_type(objective.objectiveType);
				objectiveInfo->set_target_village_name(targetVillage != nullptr ? targetVillage->name : objective.targetVillageId);
				objectiveInfo->set_target_item_name(targetItem != nullptr ? targetItem->displayName : objective.targetItemId);
			}
			for (const PlayerQuestRewardState& reward : quest.rewards)
			{
				if (reward.rewardType == "GOLD") info->add_reward_descriptions(to_string(reward.amount) + "G");
				else if (reward.rewardType == "FAME") info->add_reward_descriptions("Fame +" + to_string(reward.amount));
				else
				{
					const EconomyItemTemplate* item = GEconomyData.GetItem(reward.targetId);
					info->add_reward_descriptions((item ? item->displayName : reward.targetId) + " x" + to_string(reward.amount));
				}
			}
		}
	}

	session->Send(ServerPacketHandler::MakeSendBuffer(packet));
}
