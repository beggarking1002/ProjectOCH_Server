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
	PKT_S_BATTLE_RESULT = 1026,
	PKT_C_BATTLE_RESULT_ACK = 1027,
	PKT_S_BATTLE_RESULT_ACK = 1028,
	PKT_S_BATTLE_CLASS_SELECTION_START = 1029,
	PKT_C_BATTLE_CLASS_SELECTION = 1030,
	PKT_S_BATTLE_CLASS_SELECTION_RESULT = 1031,
	PKT_C_ENTER_VILLAGE = 1032,
	PKT_S_ENTER_VILLAGE = 1033,
	PKT_S_EXPEDITION_STATE = 1034,
	PKT_C_VILLAGE_SHOP_OPEN = 1035,
	PKT_C_VILLAGE_SHOP_BUY = 1036,
	PKT_C_VILLAGE_SHOP_SELL = 1037,
	PKT_S_VILLAGE_SHOP_STATE = 1038,
	PKT_C_RESET_PLAYER_DATA = 1039,
	PKT_S_RESET_PLAYER_DATA = 1040,
	PKT_C_VILLAGE_QUEST_BOARD_OPEN = 1041,
	PKT_C_QUEST_ACCEPT = 1042,
	PKT_C_QUEST_CLAIM_REWARD = 1043,
	PKT_S_VILLAGE_QUEST_STATE = 1044,
	PKT_C_QUEST_TRACKER_OPEN = 1045,
	PKT_S_QUEST_TRACKER_STATE = 1046,
	PKT_C_QUEST_ABANDON = 1047,
	PKT_C_FIELD_PAWN_SELECT = 1048,
	PKT_S_FIELD_PAWN_SELECT = 1049,
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
bool Handle_C_BATTLE_RESULT_ACK(PacketSessionRef& session, Protocol::C_BATTLE_RESULT_ACK& pkt);
bool Handle_C_BATTLE_CLASS_SELECTION(PacketSessionRef& session, Protocol::C_BATTLE_CLASS_SELECTION& pkt);
bool Handle_C_ENTER_VILLAGE(PacketSessionRef& session, Protocol::C_ENTER_VILLAGE& pkt);
bool Handle_C_VILLAGE_SHOP_OPEN(PacketSessionRef& session, Protocol::C_VILLAGE_SHOP_OPEN& pkt);
bool Handle_C_VILLAGE_SHOP_BUY(PacketSessionRef& session, Protocol::C_VILLAGE_SHOP_BUY& pkt);
bool Handle_C_VILLAGE_SHOP_SELL(PacketSessionRef& session, Protocol::C_VILLAGE_SHOP_SELL& pkt);
bool Handle_C_RESET_PLAYER_DATA(PacketSessionRef& session, Protocol::C_RESET_PLAYER_DATA& pkt);
bool Handle_C_VILLAGE_QUEST_BOARD_OPEN(PacketSessionRef& session, Protocol::C_VILLAGE_QUEST_BOARD_OPEN& pkt);
bool Handle_C_QUEST_ACCEPT(PacketSessionRef& session, Protocol::C_QUEST_ACCEPT& pkt);
bool Handle_C_QUEST_CLAIM_REWARD(PacketSessionRef& session, Protocol::C_QUEST_CLAIM_REWARD& pkt);
bool Handle_C_QUEST_TRACKER_OPEN(PacketSessionRef& session, Protocol::C_QUEST_TRACKER_OPEN& pkt);
bool Handle_C_QUEST_ABANDON(PacketSessionRef& session, Protocol::C_QUEST_ABANDON& pkt);
bool Handle_C_FIELD_PAWN_SELECT(PacketSessionRef& session, Protocol::C_FIELD_PAWN_SELECT& pkt);

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
		GPacketHandler[PKT_C_BATTLE_RESULT_ACK] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_BATTLE_RESULT_ACK>(Handle_C_BATTLE_RESULT_ACK, session, buffer, len); };
		GPacketHandler[PKT_C_BATTLE_CLASS_SELECTION] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_BATTLE_CLASS_SELECTION>(Handle_C_BATTLE_CLASS_SELECTION, session, buffer, len); };
		GPacketHandler[PKT_C_ENTER_VILLAGE] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_ENTER_VILLAGE>(Handle_C_ENTER_VILLAGE, session, buffer, len); };
		GPacketHandler[PKT_C_VILLAGE_SHOP_OPEN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_VILLAGE_SHOP_OPEN>(Handle_C_VILLAGE_SHOP_OPEN, session, buffer, len); };
		GPacketHandler[PKT_C_VILLAGE_SHOP_BUY] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_VILLAGE_SHOP_BUY>(Handle_C_VILLAGE_SHOP_BUY, session, buffer, len); };
		GPacketHandler[PKT_C_VILLAGE_SHOP_SELL] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_VILLAGE_SHOP_SELL>(Handle_C_VILLAGE_SHOP_SELL, session, buffer, len); };
		GPacketHandler[PKT_C_RESET_PLAYER_DATA] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_RESET_PLAYER_DATA>(Handle_C_RESET_PLAYER_DATA, session, buffer, len); };
		GPacketHandler[PKT_C_VILLAGE_QUEST_BOARD_OPEN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_VILLAGE_QUEST_BOARD_OPEN>(Handle_C_VILLAGE_QUEST_BOARD_OPEN, session, buffer, len); };
		GPacketHandler[PKT_C_QUEST_ACCEPT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_QUEST_ACCEPT>(Handle_C_QUEST_ACCEPT, session, buffer, len); };
		GPacketHandler[PKT_C_QUEST_CLAIM_REWARD] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_QUEST_CLAIM_REWARD>(Handle_C_QUEST_CLAIM_REWARD, session, buffer, len); };
		GPacketHandler[PKT_C_QUEST_TRACKER_OPEN] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_QUEST_TRACKER_OPEN>(Handle_C_QUEST_TRACKER_OPEN, session, buffer, len); };
		GPacketHandler[PKT_C_QUEST_ABANDON] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_QUEST_ABANDON>(Handle_C_QUEST_ABANDON, session, buffer, len); };
		GPacketHandler[PKT_C_FIELD_PAWN_SELECT] = [](PacketSessionRef& session, BYTE* buffer, int32 len) { return HandlePacket<Protocol::C_FIELD_PAWN_SELECT>(Handle_C_FIELD_PAWN_SELECT, session, buffer, len); };
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
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_RESULT& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_RESULT); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_RESULT_ACK& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_RESULT_ACK); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_CLASS_SELECTION_START& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_CLASS_SELECTION_START); }
	static SendBufferRef MakeSendBuffer(Protocol::S_BATTLE_CLASS_SELECTION_RESULT& pkt) { return MakeSendBuffer(pkt, PKT_S_BATTLE_CLASS_SELECTION_RESULT); }
	static SendBufferRef MakeSendBuffer(Protocol::S_ENTER_VILLAGE& pkt) { return MakeSendBuffer(pkt, PKT_S_ENTER_VILLAGE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_EXPEDITION_STATE& pkt) { return MakeSendBuffer(pkt, PKT_S_EXPEDITION_STATE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_VILLAGE_SHOP_STATE& pkt) { return MakeSendBuffer(pkt, PKT_S_VILLAGE_SHOP_STATE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_RESET_PLAYER_DATA& pkt) { return MakeSendBuffer(pkt, PKT_S_RESET_PLAYER_DATA); }
	static SendBufferRef MakeSendBuffer(Protocol::S_VILLAGE_QUEST_STATE& pkt) { return MakeSendBuffer(pkt, PKT_S_VILLAGE_QUEST_STATE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_QUEST_TRACKER_STATE& pkt) { return MakeSendBuffer(pkt, PKT_S_QUEST_TRACKER_STATE); }
	static SendBufferRef MakeSendBuffer(Protocol::S_FIELD_PAWN_SELECT& pkt) { return MakeSendBuffer(pkt, PKT_S_FIELD_PAWN_SELECT); }

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