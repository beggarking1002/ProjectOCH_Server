#include "pch.h"
#include "ClientPacketHandler.h"
#include "BufferReader.h"
#include <iostream>
#include <unordered_map>

PacketHandlerFunc GPacketHandler[UINT16_MAX];

namespace
{
	unordered_map<uint64, Protocol::ObjectInfo> GObjects;
	uint64 GMyObjectId = 0;

	float ToWorld(int32 value)
	{
		return static_cast<float>(value) / 100.f;
	}

	void PrintPosition(const Protocol::Vec2Fixed& position)
	{
		cout << "fixed=(" << position.x() << ", " << position.y() << ")"
			<< " world=(" << ToWorld(position.x()) << ", " << ToWorld(position.y()) << ")";
	}

	void PrintObjectInfo(const Protocol::ObjectInfo& info)
	{
		cout << "object_id=" << info.object_id()
			<< " object_type=" << info.object_type()
			<< " creature_type=" << info.creature_type();

		if (info.has_position())
		{
			cout << " ";
			PrintPosition(info.position());
		}
	}
}

bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len)
{
	return false;
}

bool Handle_S_LOGIN(PacketSessionRef& session, Protocol::S_LOGIN& pkt)
{
	cout << "S_LOGIN success=" << pkt.success() << endl;
	return true;
}

bool Handle_S_ENTER_GAME(PacketSessionRef& session, Protocol::S_ENTER_GAME& pkt)
{
	cout << "S_ENTER_GAME success=" << pkt.success();

	if (pkt.success() && pkt.has_player())
	{
		const Protocol::ObjectInfo& player = pkt.player();
		GMyObjectId = player.object_id();
		GObjects[GMyObjectId] = player;

		cout << " my_player={ ";
		PrintObjectInfo(player);
		cout << " }";
	}

	cout << endl;

	return true;
}

bool Handle_S_LEAVE_GAME(PacketSessionRef& session, Protocol::S_LEAVE_GAME& pkt)
{
	cout << "S_LEAVE_GAME my_object_id=" << GMyObjectId << endl;

	if (GMyObjectId != 0)
		GObjects.erase(GMyObjectId);

	GMyObjectId = 0;

	return true;
}

bool Handle_S_SPAWN(PacketSessionRef& session, Protocol::S_SPAWN& pkt)
{
	cout << "S_SPAWN count=" << pkt.players_size() << endl;

	for (const Protocol::ObjectInfo& player : pkt.players())
	{
		GObjects[player.object_id()] = player;

		cout << "  spawn { ";
		PrintObjectInfo(player);
		cout << " }" << endl;
	}

	return true;
}

bool Handle_S_DESPAWN(PacketSessionRef& session, Protocol::S_DESPAWN& pkt)
{
	cout << "S_DESPAWN count=" << pkt.object_ids_size() << endl;

	for (uint64 objectId : pkt.object_ids())
	{
		GObjects.erase(objectId);
		if (GMyObjectId == objectId)
			GMyObjectId = 0;

		cout << "  despawn object_id=" << objectId << endl;
	}

	return true;
}

bool Handle_S_MOVE(PacketSessionRef& session, Protocol::S_MOVE& pkt)
{
	cout << "S_MOVE object_id=" << pkt.object_id();

	if (pkt.has_start())
	{
		cout << " start=";
		PrintPosition(pkt.start());
	}

	if (pkt.has_target())
	{
		cout << " target=";
		PrintPosition(pkt.target());
	}

	cout << " duration_ms=" << pkt.duration_ms() << endl;

	auto findIt = GObjects.find(pkt.object_id());
	if (findIt != GObjects.end() && pkt.has_target())
		findIt->second.mutable_position()->CopyFrom(pkt.target());

	return true;
}

bool Handle_S_CHAT(PacketSessionRef& session, Protocol::S_CHAT& pkt)
{
	cout << "S_CHAT player_id=" << pkt.playerid() << " msg=" << pkt.msg() << endl;
	return true;
}
