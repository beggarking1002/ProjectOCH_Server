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
#include "GameSessionManager.h"
#include "DatabaseManager.h"
#include "GoogleAuthService.h"

PacketHandlerFunc GPacketHandler[UINT16_MAX];

bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	// TODO : Log
	return false;
}

bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	Protocol::S_LOGIN loginPkt;
	if (!GGoogleAuth.IsEnabled())
	{
		const bool requestedGoogleLogin = !pkt.google_authorization_code().empty() ||
			!pkt.google_code_verifier().empty() || !pkt.google_redirect_uri().empty();
		const bool developmentLoginAccepted = !requestedGoogleLogin && GGoogleAuth.IsDevelopmentLoginAllowed();
		gameSession->developmentAuthenticated.store(developmentLoginAccepted);
		loginPkt.set_success(developmentLoginAccepted);
		if (requestedGoogleLogin)
			loginPkt.set_reason("google authentication is not configured on the server");
		else if (developmentLoginAccepted)
			loginPkt.set_reason("development login");
		else
			loginPkt.set_reason("google login is required; start the server with -developmentLogin only for local testing");
		SEND_PACKET(loginPkt);
		return true;
	}
	if (gameSession->player.load() != nullptr)
	{
		loginPkt.set_success(false);
		loginPkt.set_reason("cannot change account after entering the game");
		SEND_PACKET(loginPkt);
		return true;
	}

	GoogleIdentity identity;
	string reason;
	uint64 accountId = 0;
	const bool authenticated = GGoogleAuth.ExchangeAuthorizationCode(pkt.google_authorization_code(),
		pkt.google_code_verifier(), pkt.google_redirect_uri(), identity, reason) &&
		GDatabase.FindOrCreateGoogleAccount(identity.subject, identity.email, identity.displayName, accountId);
	if (!authenticated || accountId == 0)
	{
		loginPkt.set_success(false);
		loginPkt.set_reason(reason.empty() ? "failed to create or load account" : reason);
		SEND_PACKET(loginPkt);
		return true;
	}

	if (!GSessionManager.TryBindAuthenticatedAccount(gameSession, accountId))
	{
		loginPkt.set_success(false);
		loginPkt.set_reason("this account is already connected");
		SEND_PACKET(loginPkt);
		cout << "GOOGLE_LOGIN_REJECTED account_id=" << accountId << " reason=already_connected" << endl;
		return true;
	}

	loginPkt.set_success(true);
	loginPkt.set_account_id(accountId);
	loginPkt.set_display_name(identity.displayName);
	cout << "GOOGLE_LOGIN account_id=" << accountId << endl;

	SEND_PACKET(loginPkt);

	return true;
}

bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	uint64 persistentPlayerId = gameSession->authenticatedAccountId.load();
	if (GGoogleAuth.IsEnabled() && persistentPlayerId == 0)
	{
		Protocol::S_ENTER_GAME enterGamePkt;
		enterGamePkt.set_success(false);
		SEND_PACKET(enterGamePkt);
		return true;
	}
	if (!GGoogleAuth.IsEnabled() && !gameSession->developmentAuthenticated.load())
	{
		Protocol::S_ENTER_GAME enterGamePkt;
		enterGamePkt.set_success(false);
		SEND_PACKET(enterGamePkt);
		return true;
	}
	if (!GGoogleAuth.IsEnabled())
		persistentPlayerId = pkt.playerindex();
	if (persistentPlayerId == 0 || !GSessionManager.TryBindAuthenticatedAccount(gameSession, persistentPlayerId))
	{
		Protocol::S_ENTER_GAME enterGamePkt;
		enterGamePkt.set_success(false);
		SEND_PACKET(enterGamePkt);
		cout << "ENTER_GAME_REJECTED player_id=" << persistentPlayerId
			<< " reason=\"identity is invalid or already connected\"" << endl;
		return true;
	}

	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		player = ObjectUtils::CreatePlayer(gameSession, persistentPlayerId);

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

bool Handle_C_FIELD_PAWN_SELECT(PacketSessionRef& session, Protocol::C_FIELD_PAWN_SELECT& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	RoomRef room = player != nullptr ? player->room.load().lock() : nullptr;
	if (room == nullptr)
	{
		Protocol::S_FIELD_PAWN_SELECT result;
		result.set_success(false);
		result.set_reason("player is not in the field");
		result.set_object_id(player != nullptr ? player->objectInfo->object_id() : 0);
		result.set_pawn_class(pkt.pawn_class());
		SEND_PACKET(result);
		return true;
	}

	room->DoAsync(&Room::HandleFieldPawnSelect, gameSession, pkt);
	return true;
}

bool Handle_C_RESET_PLAYER_DATA(PacketSessionRef& session, Protocol::C_RESET_PLAYER_DATA& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	GRoom->DoAsync(&Room::HandleResetPlayerData, gameSession, pkt);
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

bool Handle_C_VILLAGE_SHOP_OPEN(PacketSessionRef& session, Protocol::C_VILLAGE_SHOP_OPEN& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;
	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;
	room->DoAsync(&Room::HandleVillageShopOpen, gameSession, pkt);
	return true;
}

bool Handle_C_VILLAGE_SHOP_BUY(PacketSessionRef& session, Protocol::C_VILLAGE_SHOP_BUY& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;
	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;
	room->DoAsync(&Room::HandleVillageShopBuy, gameSession, pkt);
	return true;
}

bool Handle_C_VILLAGE_SHOP_SELL(PacketSessionRef& session, Protocol::C_VILLAGE_SHOP_SELL& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr)
		return false;
	RoomRef room = player->room.load().lock();
	if (room == nullptr)
		return false;
	room->DoAsync(&Room::HandleVillageShopSell, gameSession, pkt);
	return true;
}

bool Handle_C_VILLAGE_QUEST_BOARD_OPEN(PacketSessionRef& session, Protocol::C_VILLAGE_QUEST_BOARD_OPEN& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr) return false;
	RoomRef room = player->room.load().lock();
	if (room == nullptr) return false;
	room->DoAsync(&Room::HandleVillageQuestBoardOpen, gameSession, pkt);
	return true;
}

bool Handle_C_QUEST_TRACKER_OPEN(PacketSessionRef& session, Protocol::C_QUEST_TRACKER_OPEN& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr) return false;
	RoomRef room = player->room.load().lock();
	if (room == nullptr) return false;
	room->DoAsync(&Room::HandleQuestTrackerOpen, gameSession, pkt);
	return true;
}

bool Handle_C_QUEST_ABANDON(PacketSessionRef& session, Protocol::C_QUEST_ABANDON& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr) return false;
	RoomRef room = player->room.load().lock();
	if (room == nullptr) return false;
	room->DoAsync(&Room::HandleQuestAbandon, gameSession, pkt);
	return true;
}

bool Handle_C_QUEST_ACCEPT(PacketSessionRef& session, Protocol::C_QUEST_ACCEPT& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr) return false;
	RoomRef room = player->room.load().lock();
	if (room == nullptr) return false;
	room->DoAsync(&Room::HandleQuestAccept, gameSession, pkt);
	return true;
}

bool Handle_C_QUEST_CLAIM_REWARD(PacketSessionRef& session, Protocol::C_QUEST_CLAIM_REWARD& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();
	if (player == nullptr) return false;
	RoomRef room = player->room.load().lock();
	if (room == nullptr) return false;
	room->DoAsync(&Room::HandleQuestClaimReward, gameSession, pkt);
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
