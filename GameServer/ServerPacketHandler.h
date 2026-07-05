#pragma once
#include "Protocol.pb.h"

using PacketHandlerFunc = std::function<bool(PacketSessionRef&, BYTE*, int32)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX];

enum : uint16
{
	PKT_C_LOGIN = 1000,
	PKT_S_LOGIN = 1001,
	PKT_C_ENTER_GAME = 1002,
	PKT_S_ENTER_GAME = 1003,
	PKT_C_LEAVE_GAME = 1004,
	PKT_S_LEAVE_GAME = 1005,
	PKT_S_SPAWN = 1006,
	PKT_S_DESPAWN = 1007,
	PKT_C_MOVE = 1008,
	PKT_S_MOVE = 1009,
	PKT_C_CHAT = 1010,
	PKT_S_CHAT = 1011,
	PKT_C_ENTER_BATTLE = 1012,
	PKT_S_ENTER_BATTLE = 1013,
	PKT_C_BATTLE_MOVE = 1014,
	PKT_S_BATTLE_MOVE = 1015,
	PKT_C_BATTLE_SKILL = 1016,
	PKT_S_BATTLE_SKILL = 1017,
	PKT_C_BATTLE_END_TURN = 1018,
	PKT_S_BATTLE_END_TURN = 1019,
	PKT_C_BATTLE_INVITE = 1020,
	PKT_S_BATTLE_INVITE_REQUEST = 1021,
	PKT_S_BATTLE_INVITE_RECEIVED = 1022,
	PKT_C_BATTLE_INVITE_RESPONSE = 1023,
	PKT_S_BATTLE_INVITE_RESULT = 1024,
	PKT_S_BATTLE_PAWN_DEAD = 1025,
};

// Custom Handlers
bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len);
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt);
bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt);
bool Handle_C_LEAVE_GAME(PacketSessionRef& session, Protocol::C_LEAVE_GAME& pkt);
bool Handle_C_MOVE(PacketSessionRef& session, Protocol::C_MOVE& pkt);
bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt);
bool Handle_C_ENTER_BATTLE(PacketSessionRef& session, Protocol::C_ENTER_BATTLE& pkt);
bool Handle_C_BATTLE_MOVE(PacketSessionRef& session, Protocol::C_BATTLE_MOVE& pkt);
bool Handle_C_BATTLE_SKILL(PacketSessionRef& session, Protocol::C_BATTLE_SKILL& pkt);
bool Handle_C_BATTLE_END_TURN(PacketSessionRef& session, Protocol::C_BATTLE_END_TURN& pkt);
bool Handle_C_BATTLE_INVITE(PacketSessionRef& session, Protocol::C_BATTLE_INVITE& pkt);
bool Handle_C_BATTLE_INVITE_RESPONSE(PacketSessionRef& session, Protocol::C_BATTLE_INVITE_RESPONSE& pkt);

class ServerPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;
		GPacketHandler[PKT_C_LOGIN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_LOGIN>(Handle_C_LOGIN, session, buffer, len); };
		GPacketHandler[PKT_C_ENTER_GAME] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_ENTER_GAME>(Handle_C_ENTER_GAME, session, buffer, len); };
		GPacketHandler[PKT_C_LEAVE_GAME] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_LEAVE_GAME>(Handle_C_LEAVE_GAME, session, buffer, len); };
		GPacketHandler[PKT_C_MOVE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_MOVE>(Handle_C_MOVE, session, buffer, len); };
		GPacketHandler[PKT_C_CHAT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_CHAT>(Handle_C_CHAT, session, buffer, len); };
		GPacketHandler[PKT_C_ENTER_BATTLE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_ENTER_BATTLE>(Handle_C_ENTER_BATTLE, session, buffer, len); };
		GPacketHandler[PKT_C_BATTLE_MOVE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_BATTLE_MOVE>(Handle_C_BATTLE_MOVE, session, buffer, len); };
		GPacketHandler[PKT_C_BATTLE_SKILL] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_BATTLE_SKILL>(Handle_C_BATTLE_SKILL, session, buffer, len); };
		GPacketHandler[PKT_C_BATTLE_END_TURN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_BATTLE_END_TURN>(Handle_C_BATTLE_END_TURN, session, buffer, len); };
		GPacketHandler[PKT_C_BATTLE_INVITE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_BATTLE_INVITE>(Handle_C_BATTLE_INVITE, session, buffer, len); };
		GPacketHandler[PKT_C_BATTLE_INVITE_RESPONSE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_BATTLE_INVITE_RESPONSE>(Handle_C_BATTLE_INVITE_RESPONSE, session, buffer, len); };
	}

	static bool HandlePacket(PacketSessionRef& session, BYTE* buffer, int32 len)
	{
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
		return GPacketHandler[header->id](session, buffer, len);
	}
	static SendBufferRef MakeSendBuffer(Protocol::S_LOGIN& pkt) { return MakeSendBuffer(pkt, PKT_S_LOGIN); }
	static SendBufferRef MakeSendBuffer(Protocol::S_ENTER_GAME& pkt) { return MakeSendBuffer(pkt, PKT_S_ENTER_GAME); }
	static SendBufferRef MakeSendBuffer(Protocol::S_LEAVE_GAME& pkt) { return MakeSendBuffer(pkt, PKT_S_LEAVE_GAME); }
	static SendBufferRef MakeSendBuffer(Protocol::S_SPAWN& pkt) { return MakeSendBuffer(pkt, PKT_S_SPAWN); }
	static SendBufferRef MakeSendBuffer(Protocol::S_DESPAWN& pkt) { return MakeSendBuffer(pkt, PKT_S_DESPAWN); }
	static SendBufferRef MakeSendBuffer(Protocol::S_MOVE& pkt) { return MakeSendBuffer(pkt, PKT_S_MOVE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_CHAT& pkt) { return MakeSendBuffer(pkt, PKT_S_CHAT); }
	static SendBufferRef MakeSendBuffer(Protocol::S_ENTER_BATTLE& pkt) { return MakeSendBuffer(pkt, PKT_S_ENTER_BATTLE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_MOVE& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_MOVE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_SKILL& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_SKILL); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_END_TURN& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_END_TURN); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_INVITE_REQUEST& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_INVITE_REQUEST); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_INVITE_RECEIVED& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_INVITE_RECEIVED); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_INVITE_RESULT& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_INVITE_RESULT); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_PAWN_DEAD& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_PAWN_DEAD); }

private:
	template<typename PacketType, typename ProcessFunc>
	static bool HandlePacket(ProcessFunc func, PacketSessionRef& session, BYTE* buffer, int32 len)
	{
		PacketType pkt;
		if (pkt.ParseFromArray(buffer + sizeof(PacketHeader), len - sizeof(PacketHeader)) == false)
			return false;

		return func(session, pkt);
	}

	template<typename T>
	static SendBufferRef MakeSendBuffer(T& pkt, uint16 pktId)
	{
		const uint16 dataSize = static_cast<uint16>(pkt.ByteSizeLong());
		const uint16 packetSize = dataSize + sizeof(PacketHeader);

		SendBufferRef sendBuffer = make_shared<SendBuffer>(packetSize);

		PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
		header->size = packetSize;
		header->id = pktId;
		pkt.SerializeToArray(&header[1], dataSize);
		sendBuffer->Close(packetSize);

		return sendBuffer;
	}
};