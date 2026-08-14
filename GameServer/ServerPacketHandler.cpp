#include "pch.h"
#include "ServerPacketHandler.h"
#include "BufferReader.h"
#include "BufferWriter.h"
#include "Protocol.pb.h"
#include "Room.h"
#include "BattleRoom.h"
#include "ObjectUtils.h"
#include "Player.h"
#include "GameSession.h"

PacketHandlerFunc GPacketHandler[UINT16_MAX];

bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	// TODO : Log
	return false;
}

bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	// TODO: Load account and user data from DB.
	Protocol::S_LOGIN loginPkt;
	loginPkt.set_success(true);

	SEND_PACKET(loginPkt);

	return true;
}

bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);

	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		player = ObjectUtils::CreatePlayer(gameSession);

	GRoom->DoAsync(&Room::HandleEnterPlayer, player);

	return true;
}

bool Handle_C_LEAVE_GAME(PacketSessionRef& session, Protocol::C_LEAVE_GAME& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);

	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;

	GBattleRoom->DoAsync(&BattleRoom::HandleLeaveBattle, player->objectInfo->object_id(), string("leave game"));

	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;

	room->DoAsync(&Room::HandleLeavePlayer, gameSession);

	return true;
}

bool Handle_C_MOVE(PacketSessionRef& session, Protocol::C_MOVE& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);

	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;

	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;

	room->DoAsync(&Room::HandleMove, gameSession, pkt);

	return true;
}

bool Handle_C_ENTER_VILLAGE(PacketSessionRef& session, Protocol::C_ENTER_VILLAGE& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);

	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;

	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;

	room->DoAsync(&Room::HandleEnterVillage, gameSession, pkt);

	return true;
}

bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt)
{
	return true;
}

bool Handle_C_ENTER_BATTLE(PacketSessionRef& session, Protocol::C_ENTER_BATTLE& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;

	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;

	room->DoAsync(&Room::HandleDebugBattleSelectionStart, gameSession);

	return true;
}

bool Handle_C_BATTLE_MOVE(PacketSessionRef& session, Protocol::C_BATTLE_MOVE& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	GBattleRoom->DoAsync(&BattleRoom::HandleBattleMove, gameSession, pkt);

	return true;
}

bool Handle_C_BATTLE_SKILL(PacketSessionRef& session, Protocol::C_BATTLE_SKILL& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	GBattleRoom->DoAsync(&BattleRoom::HandleBattleSkill, gameSession, pkt);

	return true;
}

bool Handle_C_BATTLE_END_TURN(PacketSessionRef& session, Protocol::C_BATTLE_END_TURN& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	GBattleRoom->DoAsync(&BattleRoom::HandleBattleEndTurn, gameSession, pkt);

	return true;
}

bool Handle_C_BATTLE_INVITE(PacketSessionRef& session, Protocol::C_BATTLE_INVITE& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;

	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;

	room->DoAsync(&Room::HandleBattleInvite, gameSession, pkt);

	return true;
}

bool Handle_C_BATTLE_INVITE_RESPONSE(PacketSessionRef& session, Protocol::C_BATTLE_INVITE_RESPONSE& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;

	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;

	room->DoAsync(&Room::HandleBattleInviteResponse, gameSession, pkt);

	return true;
}

bool Handle_C_BATTLE_CLASS_SELECTION(PacketSessionRef& session, Protocol::C_BATTLE_CLASS_SELECTION& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;

	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;

	room->DoAsync(&Room::HandleBattleClassSelection, gameSession, pkt);

	return true;
}

bool Handle_C_BATTLE_RESULT_ACK(PacketSessionRef& session, Protocol::C_BATTLE_RESULT_ACK& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	GBattleRoom->DoAsync(&BattleRoom::HandleBattleResultAck, gameSession, pkt);

	return true;
}
