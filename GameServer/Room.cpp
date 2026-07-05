#include "pch.h"
#include "Room.h"
#include "Player.h"
#include "GameSession.h"
#include "Monster.h"
#include "ObjectUtils.h"
#include "FieldWalkMapData.h"
#include "BattleRoom.h"

RoomRef GRoom = make_shared<Room>();
namespace
{
	constexpr uint32 kMoveDurationMs = 300;
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

	cout << "C_MOVE object_id=" << objectId
		<< " fixed_x=" << pkt.target().x()
		<< " fixed_y=" << pkt.target().y()
		<< " cell_x=" << cellX
		<< " cell_y=" << cellY
		<< " walkable=" << walkable << endl;

	Protocol::Vec2Fixed start;
	start.CopyFrom(*player->position);

	if (walkable == false)
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

	Protocol::S_MOVE movePkt;
	movePkt.set_object_id(objectId);
	movePkt.mutable_start()->CopyFrom(start);
	movePkt.mutable_target()->CopyFrom(*player->position);
	movePkt.set_duration_ms(kMoveDurationMs);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
	Broadcast(sendBuffer);
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

	SendBattleInviteResult(requesterSession, true, requesterId, targetId, "accepted");
	SendBattleInviteResult(session, true, requesterId, targetId, "accepted");
	RemovePlayersFromFieldForBattle({ requester, target });
	GBattleRoom->DoAsync(&BattleRoom::HandleEnterPvpBattle, requesterSession, session);

	cout << "BATTLE_INVITE_ACCEPTED"
		<< " requester_id=" << requesterId
		<< " target_id=" << targetId
		<< endl;
}

void Room::UpdateTick()
{
	//cout << "Update Room" << endl;

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
		player->room.store(weak_ptr<Room>());

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
		session->Send(sendBuffer);
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