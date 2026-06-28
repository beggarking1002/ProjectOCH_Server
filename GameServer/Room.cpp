#include "pch.h"
#include "Room.h"
#include "Player.h"
#include "GameSession.h"
#include "Monster.h"
#include "ObjectUtils.h"

RoomRef GRoom = make_shared<Room>();

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

	// 랜덤 위치
	if (randPos)
	{
		object->position->set_x(Utils::GetRandom<int32>(0, 20));
		object->position->set_y(Utils::GetRandom<int32>(0, 20));
	}

	// 입장 사실을 신입 플레이어에게 알린다
	if (auto player = dynamic_pointer_cast<Player>(object))
		SendEnterGame(player, true);

	// 입장 사실을 다른 플레이어에게 알린다
	{
		Protocol::S_SPAWN spawnPkt;

		Protocol::ObjectInfo* objectInfo = spawnPkt.add_players();
		objectInfo->CopyFrom(*object->objectInfo);

		SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(spawnPkt);
		Broadcast(sendBuffer, object->objectInfo->object_id());
	}

	// 기존 입장한 플레이어 목록을 신입 플레이어한테 전송해준다
	if (auto player = dynamic_pointer_cast<Player>(object))
	{
		Protocol::S_SPAWN spawnPkt;

		const uint64 objectId = object->objectInfo->object_id();
		for (auto& item : _objects)
		{
			if (item.second->IsPlayer() == false)
				continue;
			if (item.second->objectInfo->object_id() == objectId)
				continue;

			Protocol::ObjectInfo* playerInfo = spawnPkt.add_players();
			playerInfo->CopyFrom(*item.second->objectInfo);
		}

		if (spawnPkt.players_size() > 0)
		{
			SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(spawnPkt);
			if (auto session = player->session.lock())
				session->Send(sendBuffer);
		}
	}

	return success;
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
		return true;
	}

	return EnterRoom(player, true);
}

bool Room::HandleLeavePlayer(GameSessionRef session)
{
	PlayerRef player = GetPlayerInRoom(session);
	if (player == nullptr)
		return false;

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
	cout << "C_MOVE object_id=" << objectId
		<< " x=" << pkt.target().x()
		<< " y=" << pkt.target().y() << endl;

	// 이동 사실을 알린다 (본인 포함? 빼고?)
	{
		Protocol::S_MOVE movePkt;
		movePkt.set_object_id(objectId);
		movePkt.mutable_start()->set_x(player->position->x());
		movePkt.mutable_start()->set_y(player->position->y());
		movePkt.mutable_target()->CopyFrom(pkt.target());
		movePkt.set_duration_ms(300);

		player->position->set_x(pkt.target().x());
		player->position->set_y(pkt.target().y());

		SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
		Broadcast(sendBuffer);
	}
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