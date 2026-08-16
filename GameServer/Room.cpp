#include "pch.h"
#include "Room.h"
#include "Player.h"
#include "GameSession.h"
#include "Monster.h"
#include "ObjectUtils.h"
#include "FieldWalkMapData.h"
#include "BattleRoom.h"
#include "VillageDataManager.h"
#include "EconomyService.h"
#include "QuestService.h"
#include "GameSessionManager.h"
#include "DatabaseManager.h"

RoomRef GRoom = make_shared<Room>();
namespace
{
	constexpr double kFieldMoveSpeedWorldPerSecond = 3.2;
	constexpr int32 kVillageInteractionRangeCells = 2;

	uint32 GetFieldMoveDurationMs(const Protocol::Vec2Fixed& start, const vector<Protocol::Vec2Fixed>& path)
	{
		Protocol::Vec2Fixed previous;
		previous.CopyFrom(start);
		double distanceInWorld = 0.0;
		for (const Protocol::Vec2Fixed& waypoint : path)
		{
			const double deltaX = static_cast<double>(waypoint.x()) - static_cast<double>(previous.x());
			const double deltaY = static_cast<double>(waypoint.y()) - static_cast<double>(previous.y());
			distanceInWorld += sqrt((deltaX * deltaX) + (deltaY * deltaY)) /
				static_cast<double>(GFieldWalkMapData.FixedPointScale());
			previous.CopyFrom(waypoint);
		}
		if (distanceInWorld <= 0.0)
			return 0;

		const double durationMs = ceil((distanceInWorld / kFieldMoveSpeedWorldPerSecond) * 1000.0);
		return durationMs >= static_cast<double>((numeric_limits<uint32>::max)())
			? (numeric_limits<uint32>::max)()
			: static_cast<uint32>(durationMs);
	}

	int32 GetBattleClassFamily(Protocol::PawnClass pawnClass)
	{
		switch (pawnClass)
		{
		case Protocol::PAWN_CLASS_SUEN_AXE_SWORD:
		case Protocol::PAWN_CLASS_SUEN_PARVIS:
			return 0;
		case Protocol::PAWN_CLASS_BEIGE_ICE:
		case Protocol::PAWN_CLASS_BEIGE_FIRE:
			return 1;
		case Protocol::PAWN_CLASS_ALEN_SPEAR:
		case Protocol::PAWN_CLASS_ALEN_SWORD_SHIELD:
			return 2;
		case Protocol::PAWN_CLASS_ZILLIAN_LONGBOW:
		case Protocol::PAWN_CLASS_ZILLIAN_MACE:
			return 3;
		default:
			return -1;
		}
	}
}

Room::Room()
{

}

Room::~Room()
{

}

bool Room::EnterRoom(ObjectRef object, bool randPos /*= true*/)
{
	bool success = AddObject(object);
	if (success == false)
	{
		if (auto player = dynamic_pointer_cast<Player>(object))
			SendEnterGame(player, false);

		return false;
	}

	if (randPos)
	{
		Protocol::Vec2Fixed spawnPosition;
		if (GFieldWalkMapData.TryGetRandomWalkablePosition(spawnPosition))
			object->position->CopyFrom(spawnPosition);
	}

	if (auto player = dynamic_pointer_cast<Player>(object))
	{
		SendEnterGame(player, true);
		SendExistingPlayers(player);
	}

	SendSpawn(object, object->objectInfo->object_id());

	return true;
}

bool Room::LeaveRoom(ObjectRef object)
{
	if (object == nullptr)
		return false;

	const uint64 objectId = object->objectInfo->object_id();
	bool success = RemoveObject(objectId);
	if (success == false)
		return false;

	// 퇴장 사실을 퇴장하는 플레이어에게 알린다
	if (auto player = dynamic_pointer_cast<Player>(object))
	{
		Protocol::S_LEAVE_GAME leaveGamePkt;

		SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(leaveGamePkt);
		if (auto session = player->session.lock())
		{
			session->Send(sendBuffer);
			if (session->player.load() == player)
				session->player.store(nullptr);
		}
	}

	// 퇴장 사실을 알린다
	{
		Protocol::S_DESPAWN despawnPkt;
		despawnPkt.add_object_ids(objectId);

		SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(despawnPkt);
		Broadcast(sendBuffer, objectId);

		if (auto player = dynamic_pointer_cast<Player>(object))
			if (auto session = player->session.lock())
				session->Send(sendBuffer);
	}

	return true;
}

bool Room::HandleEnterPlayer(PlayerRef player)
{
	if (player == nullptr)
		return false;

	GameSessionRef session = player->session.lock();
	if (session == nullptr || session->player.load() != player)
		return false;

	if (player->room.load().lock())
	{
		SendEnterGame(player, true);
		SendExistingPlayers(player);
		return true;
	}

	return EnterRoom(player, true);
}
bool Room::HandleEnterPlayerFromBattle(PlayerRef player, uint64 battleId)
{
	GameSessionRef session = player ? player->session.lock() : nullptr;
	if (player == nullptr || session == nullptr || session->player.load() != player)
	{
		SendBattleResultAck(session, false, battleId, "player session is invalid");
		return false;
	}

	bool alreadyInRoom = player->room.load().lock() != nullptr;
	bool success = alreadyInRoom;
	if (success == false)
	{
		success = AddObject(player);
		if (success)
		{
			Protocol::Vec2Fixed spawnPosition;
			if (GFieldWalkMapData.TryGetRandomWalkablePosition(spawnPosition))
				player->position->CopyFrom(spawnPosition);
		}
	}

	if (success == false)
	{
		SendBattleResultAck(session, false, battleId, "failed to enter field");
		return false;
	}

	cout << "BATTLE_RESULT_ACK_FIELD_ENTER"
		<< " battle_id=" << battleId
		<< " player_id=" << player->objectInfo->object_id()
		<< " already_in_room=" << alreadyInRoom
		<< endl;

	SendBattleResultAck(session, true, battleId, "");
	SendEnterGame(player, true);
	SendExistingPlayers(player);
	if (alreadyInRoom == false)
		SendSpawn(player, player->objectInfo->object_id());

	return true;
}

bool Room::HandleLeavePlayer(GameSessionRef session)
{
	PlayerRef player = GetPlayerInRoom(session);
	if (player == nullptr)
		return false;

	CancelBattleInvitesForPlayer(player->objectInfo->object_id(), "player left field");
	return LeaveRoom(player);
}

void Room::HandleMove(GameSessionRef session, Protocol::C_MOVE pkt)
{
	PlayerRef player = GetPlayerInRoom(session);
	if (player == nullptr)
		return;

	if (pkt.has_target() == false)
		return;

	const uint64 objectId = player->objectInfo->object_id();
	int32 cellX = 0;
	int32 cellY = 0;
	const bool walkable = GFieldWalkMapData.IsWalkableFixed(pkt.target(), cellX, cellY);

	Protocol::Vec2Fixed start;
	start.CopyFrom(*player->position);
	vector<Protocol::Vec2Fixed> movePath;
	const bool hasPath = walkable && GFieldWalkMapData.TryFindPathFixed(start, pkt.target(), movePath);

	cout << "C_MOVE object_id=" << objectId
		<< " fixed_x=" << pkt.target().x()
		<< " fixed_y=" << pkt.target().y()
		<< " cell_x=" << cellX
		<< " cell_y=" << cellY
		<< " walkable=" << walkable
		<< " path_found=" << hasPath
		<< " waypoint_count=" << movePath.size() << endl;

	if (hasPath == false)
	{
		Protocol::S_MOVE rejectPkt;
		rejectPkt.set_object_id(objectId);
		rejectPkt.mutable_start()->CopyFrom(start);
		rejectPkt.mutable_target()->CopyFrom(start);
		rejectPkt.set_duration_ms(0);

		SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(rejectPkt);
		if (auto playerSession = player->session.lock())
			playerSession->Send(sendBuffer);

		return;
	}

	player->position->CopyFrom(pkt.target());
	player->activeVillageId.clear();

	Protocol::S_MOVE movePkt;
	movePkt.set_object_id(objectId);
	movePkt.mutable_start()->CopyFrom(start);
	movePkt.mutable_target()->CopyFrom(*player->position);
	for (const Protocol::Vec2Fixed& waypoint : movePath)
		movePkt.add_path()->CopyFrom(waypoint);
	movePkt.set_duration_ms(GetFieldMoveDurationMs(start, movePath));

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
	Broadcast(sendBuffer);
}

void Room::HandleEnterVillage(GameSessionRef session, Protocol::C_ENTER_VILLAGE pkt)
{
	auto sendResult = [&session](bool success, const string& reason, const string& villageId,
		const string& villageName, const string& villageDescription)
		{
			Protocol::S_ENTER_VILLAGE resultPkt;
			resultPkt.set_success(success);
			resultPkt.set_reason(reason);
			resultPkt.set_village_id(villageId);
			resultPkt.set_village_name(villageName);
			resultPkt.set_village_description(villageDescription);
			session->Send(ServerPacketHandler::MakeSendBuffer(resultPkt));
		};

	PlayerRef player = GetPlayerInRoom(session);
	if (player == nullptr)
	{
		sendResult(false, "player is not in field", "", "", "");
		return;
	}

	if (pkt.map_id() != GFieldWalkMapData.MapId())
	{
		sendResult(false, "requested map is not active", "", "", "");
		return;
	}

	string villageId;
	if (GFieldWalkMapData.TryGetVillageIdAtCell(pkt.cell_x(), pkt.cell_y(), villageId) == false)
	{
		sendResult(false, "target cell is not a village", "", "", "");
		return;
	}

	int32 playerCellX = 0;
	int32 playerCellY = 0;
	if (GFieldWalkMapData.TryGetCellFromFixed(*player->position, playerCellX, playerCellY) == false)
	{
		sendResult(false, "player position is unavailable", "", "", "");
		return;
	}

	if (GFieldWalkMapData.GetHexDistanceCells(playerCellX, playerCellY, pkt.cell_x(), pkt.cell_y()) >
		kVillageInteractionRangeCells)
	{
		sendResult(false, "village is too far away", "", "", "");
		return;
	}

	const VillageTemplate* village = GVillageData.GetVillage(villageId);
	if (village == nullptr)
	{
		sendResult(false, "village content is not configured", villageId, "", "");
		return;
	}

	cout << "C_ENTER_VILLAGE player_id=" << player->objectInfo->object_id()
		<< " map_id=" << pkt.map_id()
		<< " cell_x=" << pkt.cell_x()
		<< " cell_y=" << pkt.cell_y()
		<< " village_id=" << villageId << endl;

	player->activeVillageId = villageId;
	GQuestService.OnVillageVisited(player, villageId);
	sendResult(true, "", village->villageId, village->name, village->description);
}

void Room::HandleVillageShopOpen(GameSessionRef session, Protocol::C_VILLAGE_SHOP_OPEN pkt)
{
	PlayerRef player = GetPlayerInRoom(session);
	GEconomyService.HandleShopOpen(session, player, pkt.village_id());
}

void Room::HandleVillageShopBuy(GameSessionRef session, Protocol::C_VILLAGE_SHOP_BUY pkt)
{
	PlayerRef player = GetPlayerInRoom(session);
	GEconomyService.HandleShopBuy(session, player, pkt.village_id(), pkt.item_id(), pkt.quantity());
}

void Room::HandleVillageShopSell(GameSessionRef session, Protocol::C_VILLAGE_SHOP_SELL pkt)
{
	PlayerRef player = GetPlayerInRoom(session);
	GEconomyService.HandleShopSell(session, player, pkt.village_id(), pkt.stack_id(), pkt.quantity());
}

void Room::HandleVillageQuestBoardOpen(GameSessionRef session, Protocol::C_VILLAGE_QUEST_BOARD_OPEN pkt)
{
	GQuestService.HandleBoardOpen(session, GetPlayerInRoom(session), pkt.village_id());
}

void Room::HandleQuestTrackerOpen(GameSessionRef session, Protocol::C_QUEST_TRACKER_OPEN pkt)
{
	GQuestService.HandleTrackerOpen(session, GetPlayerInRoom(session));
}

void Room::HandleQuestAbandon(GameSessionRef session, Protocol::C_QUEST_ABANDON pkt)
{
	GQuestService.HandleAbandon(session, GetPlayerInRoom(session), pkt.quest_id());
}

void Room::HandleQuestAccept(GameSessionRef session, Protocol::C_QUEST_ACCEPT pkt)
{
	GQuestService.HandleAccept(session, GetPlayerInRoom(session), pkt.quest_id());
}

void Room::HandleQuestClaimReward(GameSessionRef session, Protocol::C_QUEST_CLAIM_REWARD pkt)
{
	GQuestService.HandleClaimReward(session, GetPlayerInRoom(session), pkt.quest_id());
}

void Room::HandleResetPlayerData(GameSessionRef session, Protocol::C_RESET_PLAYER_DATA pkt)
{
	Protocol::S_RESET_PLAYER_DATA response;
	auto sendResponse = [&](bool success, const string& reason)
	{
		response.set_success(success);
		response.set_reason(reason);
		session->Send(ServerPacketHandler::MakeSendBuffer(response));
		cout << "PLAYER_DATA_RESET_RESULT success=" << (success ? 1 : 0)
			<< " reason=" << reason << endl;
	};

	if (!GDatabase.IsPlayerDataResetAllowed())
	{
		sendResponse(false, "player data reset is disabled by server configuration");
		return;
	}
	if (pkt.confirmation() != "RESET")
	{
		sendResponse(false, "reset confirmation is invalid");
		return;
	}

	PlayerRef player = GetPlayerInRoom(session);
	if (player == nullptr)
	{
		sendResponse(false, "player must be in the field");
		return;
	}
	if (!player->hasPersistentIdentity || !GDatabase.IsEnabled())
	{
		sendResponse(false, "persistent player data is unavailable");
		return;
	}

	const uint64 nowMs = ::GetTickCount64();
	const PersistentPlayerEconomyState previousState = player->ExportEconomyState();
	player->ResetEconomyProgress(nowMs);
	if (!GDatabase.SavePlayerEconomy(player->objectInfo->object_id(), player->ExportEconomyState()))
	{
		player->RestoreEconomyState(previousState, nowMs);
		player->MarkEconomyDirty();
		sendResponse(false, "failed to reset persistent player data");
		return;
	}

	player->MarkEconomyPersisted(nowMs);
	cout << "PLAYER_DATA_RESET player_id=" << player->objectInfo->object_id() << endl;
	sendResponse(true, "");
	GEconomyService.SendExpeditionState(player);
	if (!player->activeVillageId.empty())
		GQuestService.HandleBoardOpen(session, player, player->activeVillageId);
}
void Room::HandleBattleInvite(GameSessionRef session, Protocol::C_BATTLE_INVITE pkt)
{
	PlayerRef requester = GetPlayerInRoom(session);
	if (requester == nullptr)
	{
		SendBattleInviteRequest(session, false, 0, pkt.target_player_id(), "requester is not in field");
		return;
	}

	const uint64 requesterId = requester->objectInfo->object_id();
	const uint64 targetId = pkt.target_player_id();

	cout << "C_BATTLE_INVITE"
		<< " requester_id=" << requesterId
		<< " target_id=" << targetId
		<< endl;

	if (targetId == 0 || targetId == requesterId)
	{
		SendBattleInviteRequest(session, false, requesterId, targetId, "invalid target");
		return;
	}

	if (_battleInviteTargetByRequesterId.find(requesterId) != _battleInviteTargetByRequesterId.end())
	{
		SendBattleInviteRequest(session, false, requesterId, targetId, "already waiting battle invite");
		return;
	}

	if (_battleInvitesByTargetId.find(requesterId) != _battleInvitesByTargetId.end())
	{
		SendBattleInviteRequest(session, false, requesterId, targetId, "you have pending battle invite");
		return;
	}

	if (_battleClassSelectionRequesterByPlayerId.find(requesterId) != _battleClassSelectionRequesterByPlayerId.end())
	{
		SendBattleInviteRequest(session, false, requesterId, targetId, "you are selecting battle classes");
		return;
	}
	auto targetIt = _objects.find(targetId);
	if (targetIt == _objects.end())
	{
		SendBattleInviteRequest(session, false, requesterId, targetId, "target is not in field");
		return;
	}

	PlayerRef target = dynamic_pointer_cast<Player>(targetIt->second);
	if (target == nullptr)
	{
		SendBattleInviteRequest(session, false, requesterId, targetId, "target is not player");
		return;
	}

	GameSessionRef targetSession = target->session.lock();
	if (targetSession == nullptr)
	{
		SendBattleInviteRequest(session, false, requesterId, targetId, "target session is missing");
		return;
	}

	if (_battleInvitesByTargetId.find(targetId) != _battleInvitesByTargetId.end())
	{
		SendBattleInviteRequest(session, false, requesterId, targetId, "target has pending battle invite");
		return;
	}

	if (_battleClassSelectionRequesterByPlayerId.find(targetId) != _battleClassSelectionRequesterByPlayerId.end())
	{
		SendBattleInviteRequest(session, false, requesterId, targetId, "target is selecting battle classes");
		return;
	}
	PendingBattleInvite invite;
	invite.requesterId = requesterId;
	invite.targetId = targetId;
	invite.requesterSession = session;
	invite.targetSession = targetSession;

	_battleInvitesByTargetId[targetId] = invite;
	_battleInviteTargetByRequesterId[requesterId] = targetId;

	SendBattleInviteRequest(session, true, requesterId, targetId, "");
	SendBattleInviteReceived(targetSession, requesterId);

	cout << "BATTLE_INVITE_PENDING"
		<< " requester_id=" << requesterId
		<< " target_id=" << targetId
		<< endl;
}

void Room::HandleBattleInviteResponse(GameSessionRef session, Protocol::C_BATTLE_INVITE_RESPONSE pkt)
{
	PlayerRef target = GetPlayerInRoom(session);
	if (target == nullptr)
	{
		SendBattleInviteResult(session, false, pkt.requester_player_id(), 0, "target is not in field");
		return;
	}

	const uint64 targetId = target->objectInfo->object_id();
	const uint64 requesterId = pkt.requester_player_id();

	cout << "C_BATTLE_INVITE_RESPONSE"
		<< " requester_id=" << requesterId
		<< " target_id=" << targetId
		<< " accept=" << pkt.accept()
		<< endl;

	auto inviteIt = _battleInvitesByTargetId.find(targetId);
	if (inviteIt == _battleInvitesByTargetId.end() || inviteIt->second.requesterId != requesterId)
	{
		SendBattleInviteResult(session, false, requesterId, targetId, "battle invite not found");
		return;
	}

	PendingBattleInvite invite = inviteIt->second;
	_battleInvitesByTargetId.erase(inviteIt);
	_battleInviteTargetByRequesterId.erase(requesterId);

	GameSessionRef requesterSession = invite.requesterSession.lock();
	if (requesterSession == nullptr)
	{
		SendBattleInviteResult(session, false, requesterId, targetId, "requester session is missing");
		return;
	}

	auto requesterIt = _objects.find(requesterId);
	if (requesterIt == _objects.end())
	{
		SendBattleInviteResult(session, false, requesterId, targetId, "requester is not in field");
		return;
	}

	PlayerRef requester = dynamic_pointer_cast<Player>(requesterIt->second);
	if (requester == nullptr)
	{
		SendBattleInviteResult(session, false, requesterId, targetId, "requester is not player");
		return;
	}

	if (pkt.accept() == false)
	{
		SendBattleInviteResult(requesterSession, false, requesterId, targetId, "declined");
		SendBattleInviteResult(session, false, requesterId, targetId, "declined");

		cout << "BATTLE_INVITE_DECLINED"
			<< " requester_id=" << requesterId
			<< " target_id=" << targetId
			<< endl;
		return;
	}

	PendingBattleClassSelection selection;
	selection.requesterId = requesterId;
	selection.targetId = targetId;
	selection.requesterSession = requesterSession;
	selection.targetSession = session;
	_battleClassSelectionsByRequesterId[requesterId] = selection;
	_battleClassSelectionRequesterByPlayerId[requesterId] = requesterId;
	_battleClassSelectionRequesterByPlayerId[targetId] = requesterId;

	SendBattleInviteResult(requesterSession, true, requesterId, targetId, "accepted; choose battle classes");
	SendBattleInviteResult(session, true, requesterId, targetId, "accepted; choose battle classes");
	SendBattleClassSelectionStart(requesterSession, requesterId, targetId);
	SendBattleClassSelectionStart(session, requesterId, targetId);

	cout << "BATTLE_INVITE_ACCEPTED"
		<< " requester_id=" << requesterId
		<< " target_id=" << targetId
		<< endl;
}

void Room::HandleDebugBattleSelectionStart(GameSessionRef session)
{
	PlayerRef player = GetPlayerInRoom(session);
	if (player == nullptr)
	{
		SendBattleClassSelectionResult(session, false, false, 0, 0, "player is not in field");
		return;
	}

	const uint64 playerId = player->objectInfo->object_id();
	if (_debugBattleClassSelectionsByPlayerId.find(playerId) != _debugBattleClassSelectionsByPlayerId.end() ||
		_battleClassSelectionRequesterByPlayerId.find(playerId) != _battleClassSelectionRequesterByPlayerId.end() ||
		_battleInviteTargetByRequesterId.find(playerId) != _battleInviteTargetByRequesterId.end() ||
		_battleInvitesByTargetId.find(playerId) != _battleInvitesByTargetId.end())
	{
		SendBattleClassSelectionResult(session, false, false, playerId, 0, "battle class selection is already in progress");
		return;
	}

	_debugBattleClassSelectionsByPlayerId[playerId] = session;
	SendBattleClassSelectionStart(session, playerId, 0);

	cout << "DEBUG_BATTLE_CLASS_SELECTION_START"
		<< " player_id=" << playerId
		<< endl;
}

void Room::HandleBattleClassSelection(GameSessionRef session, Protocol::C_BATTLE_CLASS_SELECTION pkt)
{
	PlayerRef player = GetPlayerInRoom(session);
	if (player == nullptr)
	{
		SendBattleClassSelectionResult(session, false, false, 0, 0, "player is not in field");
		return;
	}

	const uint64 playerId = player->objectInfo->object_id();
	auto debugSelectionIt = _debugBattleClassSelectionsByPlayerId.find(playerId);
	if (debugSelectionIt != _debugBattleClassSelectionsByPlayerId.end())
	{
		vector<Protocol::PawnClass> selectedClasses;
		string reason;
		if (TryBuildBattleClassSelection(pkt, selectedClasses, reason) == false)
		{
			SendBattleClassSelectionResult(session, false, false, playerId, 0, reason);
			return;
		}

		player->SetBattlePawnClasses(selectedClasses);
		_debugBattleClassSelectionsByPlayerId.erase(debugSelectionIt);
		SendBattleClassSelectionResult(session, true, false, playerId, 0, "battle classes locked");
		RemovePlayersFromFieldForBattle({ player });
		GBattleRoom->DoAsync(&BattleRoom::HandleEnterBattle, session);

		cout << "DEBUG_BATTLE_CLASS_SELECTION_COMPLETE"
			<< " player_id=" << playerId
			<< " pawn_count=" << selectedClasses.size()
			<< endl;
		return;
	}

	auto selectionOwnerIt = _battleClassSelectionRequesterByPlayerId.find(playerId);
	if (selectionOwnerIt == _battleClassSelectionRequesterByPlayerId.end())
	{
		SendBattleClassSelectionResult(session, false, false, playerId, 0, "battle class selection not found");
		return;
	}

	auto selectionIt = _battleClassSelectionsByRequesterId.find(selectionOwnerIt->second);
	if (selectionIt == _battleClassSelectionsByRequesterId.end())
	{
		_battleClassSelectionRequesterByPlayerId.erase(selectionOwnerIt);
		SendBattleClassSelectionResult(session, false, false, playerId, 0, "battle class selection expired");
		return;
	}

	vector<Protocol::PawnClass> selectedClasses;
	string reason;
	if (TryBuildBattleClassSelection(pkt, selectedClasses, reason) == false)
	{
		SendBattleClassSelectionResult(session, false, false, selectionIt->second.requesterId, selectionIt->second.targetId, reason);
		return;
	}

	PendingBattleClassSelection& selection = selectionIt->second;
	if (playerId == selection.requesterId)
	{
		selection.requesterClasses = move(selectedClasses);
		selection.requesterSelected = true;
	}
	else if (playerId == selection.targetId)
	{
		selection.targetClasses = move(selectedClasses);
		selection.targetSelected = true;
	}
	else
	{
		SendBattleClassSelectionResult(session, false, false, selection.requesterId, selection.targetId, "player is not part of this selection");
		return;
	}

	if (selection.requesterSelected == false || selection.targetSelected == false)
	{
		SendBattleClassSelectionResult(session, true, true, selection.requesterId, selection.targetId, "waiting for opponent class selection");
		return;
	}

	PendingBattleClassSelection completedSelection = selection;
	GameSessionRef requesterSession = completedSelection.requesterSession.lock();
	GameSessionRef targetSession = completedSelection.targetSession.lock();
	auto requesterIt = _objects.find(completedSelection.requesterId);
	auto targetIt = _objects.find(completedSelection.targetId);
	PlayerRef requester = requesterIt != _objects.end() ? dynamic_pointer_cast<Player>(requesterIt->second) : nullptr;
	PlayerRef target = targetIt != _objects.end() ? dynamic_pointer_cast<Player>(targetIt->second) : nullptr;
	if (requesterSession == nullptr || targetSession == nullptr || requester == nullptr || target == nullptr)
	{
		CancelBattleClassSelectionForPlayer(playerId, "player left field before battle entry");
		return;
	}

	requester->SetBattlePawnClasses(completedSelection.requesterClasses);
	target->SetBattlePawnClasses(completedSelection.targetClasses);
	_battleClassSelectionRequesterByPlayerId.erase(completedSelection.requesterId);
	_battleClassSelectionRequesterByPlayerId.erase(completedSelection.targetId);
	_battleClassSelectionsByRequesterId.erase(completedSelection.requesterId);

	SendBattleClassSelectionResult(requesterSession, true, false, completedSelection.requesterId, completedSelection.targetId, "battle classes locked");
	SendBattleClassSelectionResult(targetSession, true, false, completedSelection.requesterId, completedSelection.targetId, "battle classes locked");
	RemovePlayersFromFieldForBattle({ requester, target });
	GBattleRoom->DoAsync(&BattleRoom::HandleEnterPvpBattle, requesterSession, targetSession);

	cout << "BATTLE_CLASS_SELECTION_COMPLETE"
		<< " requester_id=" << completedSelection.requesterId
		<< " target_id=" << completedSelection.targetId
		<< " pawn_count_per_player=" << completedSelection.requesterClasses.size()
		<< endl;
}
void Room::UpdateTick()
{
	const uint64 nowMs = ::GetTickCount64();
	GSessionManager.UpdateEconomy(nowMs);

	DoTimer(100, &Room::UpdateTick);
}

RoomRef Room::GetRoomRef()
{
	return static_pointer_cast<Room>(shared_from_this());
}

bool Room::AddObject(ObjectRef object)
{
	// 있다면 문제가 있다.
	if (_objects.find(object->objectInfo->object_id()) != _objects.end())
		return false;

	_objects.insert(make_pair(object->objectInfo->object_id(), object));

	object->room.store(GetRoomRef());

	return true;
}

bool Room::RemoveObject(uint64 objectId)
{
	// 없다면 문제가 있다.
	if (_objects.find(objectId) == _objects.end())
		return false;

	ObjectRef object = _objects[objectId];
	PlayerRef player = dynamic_pointer_cast<Player>(object);
	if (player)
	{
		player->activeVillageId.clear();
		player->room.store(weak_ptr<Room>());
	}

	_objects.erase(objectId);

	return true;
}

PlayerRef Room::GetPlayerInRoom(GameSessionRef session)
{
	if (session == nullptr)
		return nullptr;

	PlayerRef player = session->player.load();
	if (player == nullptr)
		return nullptr;

	if (player->session.lock() != session)
		return nullptr;

	RoomRef room = player->room.load().lock();
	if (room.get() != this)
		return nullptr;

	const uint64 objectId = player->objectInfo->object_id();
	auto findIt = _objects.find(objectId);
	if (findIt == _objects.end())
		return nullptr;

	PlayerRef roomPlayer = dynamic_pointer_cast<Player>(findIt->second);
	if (roomPlayer != player)
		return nullptr;

	return player;
}

void Room::SendEnterGame(PlayerRef player, bool success)
{
	if (player == nullptr)
		return;

	Protocol::S_ENTER_GAME enterGamePkt;
	enterGamePkt.set_success(success);
	if (success)
		enterGamePkt.mutable_player()->CopyFrom(*player->objectInfo);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(enterGamePkt);
	if (auto session = player->session.lock())
	{
		session->Send(sendBuffer);
		if (success)
			GEconomyService.SendExpeditionState(player);
	}
}

void Room::SendSpawn(ObjectRef object, uint64 exceptId)
{
	if (object == nullptr)
		return;

	Protocol::S_SPAWN spawnPkt;
	Protocol::ObjectInfo* objectInfo = spawnPkt.add_players();
	objectInfo->CopyFrom(*object->objectInfo);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(spawnPkt);
	Broadcast(sendBuffer, exceptId);
}

void Room::SendExistingPlayers(PlayerRef player)
{
	if (player == nullptr)
		return;

	Protocol::S_SPAWN spawnPkt;

	const uint64 objectId = player->objectInfo->object_id();
	for (auto& item : _objects)
	{
		if (item.second->IsPlayer() == false)
			continue;
		if (item.second->objectInfo->object_id() == objectId)
			continue;

		Protocol::ObjectInfo* playerInfo = spawnPkt.add_players();
		playerInfo->CopyFrom(*item.second->objectInfo);
	}

	if (spawnPkt.players_size() == 0)
		return;

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(spawnPkt);
	if (auto session = player->session.lock())
		session->Send(sendBuffer);
}

void Room::SendBattleInviteRequest(GameSessionRef session, bool success, uint64 requesterId, uint64 targetId, const string& reason)
{
	cout << "S_BATTLE_INVITE_REQUEST"
		<< " success=" << success
		<< " requester_id=" << requesterId
		<< " target_id=" << targetId
		<< " reason=\"" << reason << "\""
		<< endl;

	if (session == nullptr)
		return;

	Protocol::S_BATTLE_INVITE_REQUEST pkt;
	pkt.set_success(success);
	pkt.set_requester_player_id(requesterId);
	pkt.set_target_player_id(targetId);
	pkt.set_reason(reason);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(pkt);
	session->Send(sendBuffer);
}

void Room::SendBattleInviteReceived(GameSessionRef session, uint64 requesterId)
{
	cout << "S_BATTLE_INVITE_RECEIVED"
		<< " requester_id=" << requesterId
		<< endl;

	if (session == nullptr)
		return;

	Protocol::S_BATTLE_INVITE_RECEIVED pkt;
	pkt.set_requester_player_id(requesterId);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(pkt);
	session->Send(sendBuffer);
}

void Room::SendBattleInviteResult(GameSessionRef session, bool accepted, uint64 requesterId, uint64 targetId, const string& reason)
{
	cout << "S_BATTLE_INVITE_RESULT"
		<< " accepted=" << accepted
		<< " requester_id=" << requesterId
		<< " target_id=" << targetId
		<< " reason=\"" << reason << "\""
		<< endl;

	if (session == nullptr)
		return;

	Protocol::S_BATTLE_INVITE_RESULT pkt;
	pkt.set_accepted(accepted);
	pkt.set_requester_player_id(requesterId);
	pkt.set_target_player_id(targetId);
	pkt.set_reason(reason);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(pkt);
	session->Send(sendBuffer);
}

void Room::SendBattleClassSelectionStart(GameSessionRef session, uint64 requesterId, uint64 targetId)
{
	if (session == nullptr)
		return;

	Protocol::S_BATTLE_CLASS_SELECTION_START pkt;
	pkt.set_requester_player_id(requesterId);
	pkt.set_target_player_id(targetId);
	pkt.add_suen_options(Protocol::PAWN_CLASS_SUEN_AXE_SWORD);
	pkt.add_suen_options(Protocol::PAWN_CLASS_SUEN_PARVIS);
	pkt.add_beige_options(Protocol::PAWN_CLASS_BEIGE_ICE);
	pkt.add_beige_options(Protocol::PAWN_CLASS_BEIGE_FIRE);
	pkt.add_alen_options(Protocol::PAWN_CLASS_ALEN_SPEAR);
	pkt.add_alen_options(Protocol::PAWN_CLASS_ALEN_SWORD_SHIELD);
	pkt.add_zillian_options(Protocol::PAWN_CLASS_ZILLIAN_LONGBOW);
	pkt.add_zillian_options(Protocol::PAWN_CLASS_ZILLIAN_MACE);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(pkt);
	session->Send(sendBuffer);
}

void Room::SendBattleClassSelectionResult(GameSessionRef session, bool success, bool waitingForOpponent,
	uint64 requesterId, uint64 targetId, const string& reason)
{
	if (session == nullptr)
		return;

	Protocol::S_BATTLE_CLASS_SELECTION_RESULT pkt;
	pkt.set_success(success);
	pkt.set_waiting_for_opponent(waitingForOpponent);
	pkt.set_requester_player_id(requesterId);
	pkt.set_target_player_id(targetId);
	pkt.set_reason(reason);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(pkt);
	session->Send(sendBuffer);
}
void Room::SendBattleResultAck(GameSessionRef session, bool success, uint64 battleId, const string& reason)
{
	cout << "S_BATTLE_RESULT_ACK"
		<< " success=" << success
		<< " battle_id=" << battleId
		<< " reason=\"" << reason << "\""
		<< endl;

	if (session == nullptr)
		return;

	Protocol::S_BATTLE_RESULT_ACK ackPkt;
	ackPkt.set_success(success);
	ackPkt.set_battle_id(battleId);
	ackPkt.set_reason(reason);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(ackPkt);
	session->Send(sendBuffer);
}
void Room::RemovePlayersFromFieldForBattle(const vector<PlayerRef>& players)
{
	Protocol::S_DESPAWN despawnPkt;
	vector<GameSessionRef> sessions;

	for (const PlayerRef& player : players)
	{
		if (player == nullptr)
			continue;

		const uint64 objectId = player->objectInfo->object_id();
		if (_objects.find(objectId) == _objects.end())
			continue;

		RemoveObject(objectId);
		despawnPkt.add_object_ids(objectId);

		if (GameSessionRef session = player->session.lock())
			sessions.push_back(session);
	}

	if (despawnPkt.object_ids_size() == 0)
		return;

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(despawnPkt);
	Broadcast(sendBuffer);

	for (GameSessionRef session : sessions)
	{
		if (session != nullptr)
			session->Send(sendBuffer);
	}
}

void Room::CancelBattleInvitesForPlayer(uint64 playerId, const string& reason)
{
	CancelDebugBattleClassSelection(playerId);
	CancelBattleClassSelectionForPlayer(playerId, reason);
	auto outgoingIt = _battleInviteTargetByRequesterId.find(playerId);
	if (outgoingIt != _battleInviteTargetByRequesterId.end())
	{
		const uint64 targetId = outgoingIt->second;
		auto inviteIt = _battleInvitesByTargetId.find(targetId);
		if (inviteIt != _battleInvitesByTargetId.end())
		{
			if (GameSessionRef targetSession = inviteIt->second.targetSession.lock())
				SendBattleInviteResult(targetSession, false, playerId, targetId, reason);
			_battleInvitesByTargetId.erase(inviteIt);
		}
		_battleInviteTargetByRequesterId.erase(outgoingIt);
	}

	auto incomingIt = _battleInvitesByTargetId.find(playerId);
	if (incomingIt != _battleInvitesByTargetId.end())
	{
		const uint64 requesterId = incomingIt->second.requesterId;
		if (GameSessionRef requesterSession = incomingIt->second.requesterSession.lock())
			SendBattleInviteResult(requesterSession, false, requesterId, playerId, reason);
		_battleInviteTargetByRequesterId.erase(requesterId);
		_battleInvitesByTargetId.erase(incomingIt);
	}
}

void Room::CancelBattleClassSelectionForPlayer(uint64 playerId, const string& reason)
{
	auto selectionOwnerIt = _battleClassSelectionRequesterByPlayerId.find(playerId);
	if (selectionOwnerIt == _battleClassSelectionRequesterByPlayerId.end())
		return;

	auto selectionIt = _battleClassSelectionsByRequesterId.find(selectionOwnerIt->second);
	if (selectionIt == _battleClassSelectionsByRequesterId.end())
	{
		_battleClassSelectionRequesterByPlayerId.erase(selectionOwnerIt);
		return;
	}

	PendingBattleClassSelection selection = selectionIt->second;
	_battleClassSelectionRequesterByPlayerId.erase(selection.requesterId);
	_battleClassSelectionRequesterByPlayerId.erase(selection.targetId);
	_battleClassSelectionsByRequesterId.erase(selectionIt);

	if (selection.requesterId != playerId)
		SendBattleClassSelectionResult(selection.requesterSession.lock(), false, false, selection.requesterId, selection.targetId, reason);
	if (selection.targetId != playerId)
		SendBattleClassSelectionResult(selection.targetSession.lock(), false, false, selection.requesterId, selection.targetId, reason);
}

void Room::CancelDebugBattleClassSelection(uint64 playerId)
{
	_debugBattleClassSelectionsByPlayerId.erase(playerId);
}

bool Room::TryBuildBattleClassSelection(const Protocol::C_BATTLE_CLASS_SELECTION& pkt,
	vector<Protocol::PawnClass>& selectedClasses, string& reason) const
{
	if (pkt.selected_pawn_classes_size() != 4)
	{
		reason = "select exactly one class for Suen, Beige, Alen, and Zillian";
		return false;
	}

	vector<Protocol::PawnClass> classByFamily(4, Protocol::PAWN_CLASS_NONE);
	for (int32 index = 0; index < pkt.selected_pawn_classes_size(); index++)
	{
		const Protocol::PawnClass pawnClass = pkt.selected_pawn_classes(index);
		const int32 family = GetBattleClassFamily(pawnClass);
		if (family < 0)
		{
			reason = "selected class is not available for PvP";
			return false;
		}

		if (classByFamily[family] != Protocol::PAWN_CLASS_NONE)
		{
			reason = "select only one class from each character family";
			return false;
		}

		classByFamily[family] = pawnClass;
	}

	for (Protocol::PawnClass pawnClass : classByFamily)
	{
		if (pawnClass == Protocol::PAWN_CLASS_NONE)
		{
			reason = "missing character family selection";
			return false;
		}
	}

	selectedClasses = move(classByFamily);
	return true;
}
void Room::Broadcast(SendBufferRef sendBuffer, uint64 exceptId)
{
	for (auto& item : _objects)
	{
		PlayerRef player = dynamic_pointer_cast<Player>(item.second);
		if (player == nullptr)
			continue;
		if (player->objectInfo->object_id() == exceptId)
			continue;

		if (GameSessionRef session = player->session.lock())
			session->Send(sendBuffer);
	}
}
