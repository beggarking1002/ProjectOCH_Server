#include "pch.h"
#include "GameSession.h"
#include "GameSessionManager.h"
#include "ServerPacketHandler.h"
#include "Room.h"
#include "BattleRoom.h"
#include "Player.h"

void GameSession::OnConnected()
{
	GameSessionRef session = static_pointer_cast<GameSession>(shared_from_this());
	GSessionManager.Add(session);
}

void GameSession::OnDisconnected()
{
	GameSessionRef session = static_pointer_cast<GameSession>(shared_from_this());
	PlayerRef player = session->player.load();
	if (player)
	{
		GBattleRoom->DoAsync(&BattleRoom::HandleLeaveBattle, player->objectInfo->object_id(), string("disconnect"));

		RoomRef room = player->room.load().lock();
		if (room)
			room->DoAsync(&Room::HandleLeavePlayer, session);
		else
			session->player.store(nullptr);
	}

	GSessionManager.Remove(session);
}

void GameSession::OnRecvPacket(BYTE* buffer, int32 len)
{
	PacketSessionRef session = GetPacketSessionRef();
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);

	// TODO: Validate packet id range.
	ServerPacketHandler::HandlePacket(session, buffer, len);
}

void GameSession::OnSend(int32 len)
{
}
