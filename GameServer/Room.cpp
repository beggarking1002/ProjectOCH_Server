#include "pch.h"
#include "Room.h"
#include "Player.h"
#include "GameSession.h"
#include "Monster.h"
#include "ObjectUtils.h"

RoomRef GRoom = make_shared<Room>();
namespace
{
	constexpr int32 kFieldFixedPointScale = 100;
	constexpr int32 kSpawnMinWorld = 0;
	constexpr int32 kSpawnMaxWorld = 20;
	constexpr uint32 kMoveDurationMs = 300;

	int32 ToFixed(int32 worldValue)
	{
		return worldValue * kFieldFixedPointScale;
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
		object->position->set_x(ToFixed(Utils::GetRandom<int32>(kSpawnMinWorld, kSpawnMaxWorld)));
		object->position->set_y(ToFixed(Utils::GetRandom<int32>(kSpawnMinWorld, kSpawnMaxWorld)));
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
		<< " fixed_x=" << pkt.target().x()
		<< " fixed_y=" << pkt.target().y()
		<< " world_x=" << static_cast<float>(pkt.target().x()) / kFieldFixedPointScale
		<< " world_y=" << static_cast<float>(pkt.target().y()) / kFieldFixedPointScale << endl;

	Protocol::Vec2Fixed start;
	start.CopyFrom(*player->position);

	player->position->CopyFrom(pkt.target());

	Protocol::S_MOVE movePkt;
	movePkt.set_object_id(objectId);
	movePkt.mutable_start()->CopyFrom(start);
	movePkt.mutable_target()->CopyFrom(*player->position);
	movePkt.set_duration_ms(kMoveDurationMs);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
	Broadcast(sendBuffer);
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