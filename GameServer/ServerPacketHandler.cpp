#include "pch.h"
#include "ServerPacketHandler.h"
#include "BufferReader.h"
#include "BufferWriter.h"
#include "Protocol.pb.h"
#include "Room.h"
#include "ObjectUtils.h"
#include "Player.h"
#include "GameSession.h"

#include <mutex>

PacketHandlerFunc GPacketHandler[UINT16_MAX];

namespace
{
	struct BattlePawnState
	{
		uint64 pawnId = 0;
		uint64 ownerId = 0;
		Protocol::PawnClass pawnClass = Protocol::PAWN_CLASS_NONE;
		Protocol::AxialCoord axial;
		int32 hp = 0;
		int32 maxHp = 0;
		int32 moveRange = 0;
	};

	struct BattleState
	{
		uint64 battleId = 0;
		uint64 ownerId = 0;
		string mapId;
		vector<BattlePawnState> alliedPawns;
		vector<BattlePawnState> enemyPawns;
		uint64 currentTurnPawnId = 0;
	};

	mutex GBattleLock;
	atomic<uint64> GBattleIdGenerator = 1;
	atomic<uint64> GBattlePawnIdGenerator = 1;
	unordered_map<uint64, BattleState> GBattles;
	unordered_map<uint64, uint64> GBattleByOwnerId;

	Protocol::AxialCoord MakeAxial(int32 q, int32 r)
	{
		Protocol::AxialCoord coord;
		coord.set_q(q);
		coord.set_r(r);
		return coord;
	}

	BattlePawnState MakeBattlePawn(uint64 ownerId, Protocol::PawnClass pawnClass, int32 q, int32 r, int32 hp, int32 moveRange)
	{
		BattlePawnState pawn;
		pawn.pawnId = GBattlePawnIdGenerator.fetch_add(1);
		pawn.ownerId = ownerId;
		pawn.pawnClass = pawnClass;
		pawn.axial = MakeAxial(q, r);
		pawn.hp = hp;
		pawn.maxHp = hp;
		pawn.moveRange = moveRange;
		return pawn;
	}

	void CopyBattlePawn(const BattlePawnState& src, Protocol::BattlePawnInfo* dst)
	{
		dst->set_pawn_id(src.pawnId);
		dst->set_owner_id(src.ownerId);
		dst->set_pawn_class(src.pawnClass);
		dst->mutable_axial()->CopyFrom(src.axial);
		dst->set_hp(src.hp);
		dst->set_max_hp(src.maxHp);
		dst->set_move_range(src.moveRange);
	}

	BattleState CreateBattle(uint64 ownerId)
	{
		BattleState battle;
		battle.battleId = GBattleIdGenerator.fetch_add(1);
		battle.ownerId = ownerId;
		battle.mapId = "Battle_Test_001";

		battle.alliedPawns.push_back(MakeBattlePawn(ownerId, Protocol::PAWN_CLASS_SUEN_AXE_SWORD, -2, 0, 100, 3));
		battle.alliedPawns.push_back(MakeBattlePawn(ownerId, Protocol::PAWN_CLASS_BEIGE_FIRE, -2, 1, 80, 3));
		battle.enemyPawns.push_back(MakeBattlePawn(0, Protocol::PAWN_CLASS_ZILLIAN_LONGBOW, 2, -1, 70, 3));
		battle.enemyPawns.push_back(MakeBattlePawn(0, Protocol::PAWN_CLASS_ALEN_SPEAR, 2, 0, 90, 3));

		battle.currentTurnPawnId = battle.alliedPawns.front().pawnId;
		return battle;
	}

	int32 AxialDistance(const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs)
	{
		const int32 dq = lhs.q() - rhs.q();
		const int32 dr = lhs.r() - rhs.r();
		const int32 ds = -dq - dr;
		return (abs(dq) + abs(dr) + abs(ds)) / 2;
	}

	bool IsBattleWalkable(const Protocol::AxialCoord& coord)
	{
		constexpr int32 kBattleMapRadius = 6;
		const int32 q = coord.q();
		const int32 r = coord.r();
		const int32 s = -q - r;
		return abs(q) <= kBattleMapRadius && abs(r) <= kBattleMapRadius && abs(s) <= kBattleMapRadius;
	}

	BattlePawnState* FindPawn(BattleState& battle, uint64 pawnId)
	{
		for (BattlePawnState& pawn : battle.alliedPawns)
		{
			if (pawn.pawnId == pawnId)
				return &pawn;
		}

		for (BattlePawnState& pawn : battle.enemyPawns)
		{
			if (pawn.pawnId == pawnId)
				return &pawn;
		}

		return nullptr;
	}

	bool IsOccupied(const BattleState& battle, const Protocol::AxialCoord& coord, uint64 exceptPawnId)
	{
		auto isSameCell = [&coord, exceptPawnId](const BattlePawnState& pawn)
			{
				return pawn.pawnId != exceptPawnId && pawn.axial.q() == coord.q() && pawn.axial.r() == coord.r();
			};

		for (const BattlePawnState& pawn : battle.alliedPawns)
		{
			if (isSameCell(pawn))
				return true;
		}

		for (const BattlePawnState& pawn : battle.enemyPawns)
		{
			if (isSameCell(pawn))
				return true;
		}

		return false;
	}

	uint64 GetNextAlliedTurnPawnId(const BattleState& battle, uint64 currentPawnId)
	{
		if (battle.alliedPawns.empty())
			return 0;

		for (size_t i = 0; i < battle.alliedPawns.size(); i++)
		{
			if (battle.alliedPawns[i].pawnId == currentPawnId)
				return battle.alliedPawns[(i + 1) % battle.alliedPawns.size()].pawnId;
		}

		return battle.alliedPawns.front().pawnId;
	}

	void SendBattleMoveResult(PacketSessionRef& session, bool success, uint64 battleId, uint64 pawnId,
		const Protocol::AxialCoord& start, const Protocol::AxialCoord& target, uint64 nextTurnPawnId,
		Protocol::BattleMoveResult result, const string& reason)
	{
		cout << "S_BATTLE_MOVE"
			<< " success=" << success
			<< " battle_id=" << battleId
			<< " pawn_id=" << pawnId
			<< " start=(" << start.q() << ", " << start.r() << ")"
			<< " target=(" << target.q() << ", " << target.r() << ")"
			<< " next_turn_pawn_id=" << nextTurnPawnId
			<< " result=" << Protocol::BattleMoveResult_Name(result)
			<< " reason=\"" << reason << "\""
			<< endl;

		Protocol::S_BATTLE_MOVE movePkt;
		movePkt.set_success(success);
		movePkt.set_battle_id(battleId);
		movePkt.set_pawn_id(pawnId);
		movePkt.mutable_start()->CopyFrom(start);
		movePkt.mutable_target()->CopyFrom(target);
		movePkt.set_next_turn_pawn_id(nextTurnPawnId);
		movePkt.set_result(result);
		movePkt.set_reason(reason);

		SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
		session->Send(sendBuffer);
	}
}

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

bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt)
{
	return true;
}

bool Handle_C_ENTER_BATTLE(PacketSessionRef& session, Protocol::C_ENTER_BATTLE& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();

	Protocol::S_ENTER_BATTLE enterBattlePkt;
	if (player == nullptr)
	{
		enterBattlePkt.set_success(false);
		enterBattlePkt.set_reason("player is not in game");
		SEND_PACKET(enterBattlePkt);
		return false;
	}

	const uint64 ownerId = player->objectInfo->object_id();

	{
		lock_guard<mutex> lock(GBattleLock);

		uint64 battleId = 0;
		auto battleIdIt = GBattleByOwnerId.find(ownerId);
		if (battleIdIt == GBattleByOwnerId.end())
		{
			BattleState battle = CreateBattle(ownerId);
			battleId = battle.battleId;
			GBattleByOwnerId[ownerId] = battleId;
			GBattles.emplace(battleId, move(battle));
		}
		else
		{
			battleId = battleIdIt->second;
		}

		BattleState& battle = GBattles[battleId];
		enterBattlePkt.set_success(true);
		enterBattlePkt.set_battle_id(battle.battleId);
		enterBattlePkt.set_map_id(battle.mapId);
		enterBattlePkt.set_current_turn_pawn_id(battle.currentTurnPawnId);

		for (const BattlePawnState& pawn : battle.alliedPawns)
			CopyBattlePawn(pawn, enterBattlePkt.add_allied_pawns());

		for (const BattlePawnState& pawn : battle.enemyPawns)
			CopyBattlePawn(pawn, enterBattlePkt.add_enemy_pawns());
	}

	SEND_PACKET(enterBattlePkt);
	return true;
}

bool Handle_C_BATTLE_MOVE(PacketSessionRef& session, Protocol::C_BATTLE_MOVE& pkt)
{
	auto gameSession = static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->player.load();

	cout << "C_BATTLE_MOVE"
		<< " battle_id=" << pkt.battle_id()
		<< " pawn_id=" << pkt.pawn_id();
	if (pkt.has_target())
	{
		cout << " target=(" << pkt.target().q() << ", " << pkt.target().r() << ")";
	}
	else
	{
		cout << " target=<missing>";
	}
	cout << endl;

	if (player == nullptr)
	{
		SendBattleMoveResult(session, false, pkt.battle_id(), pkt.pawn_id(), Protocol::AxialCoord::default_instance(),
			Protocol::AxialCoord::default_instance(), 0, Protocol::BATTLE_MOVE_RESULT_INVALID_BATTLE, "player is not in game");
		return false;
	}

	if (pkt.has_target() == false)
	{
		SendBattleMoveResult(session, false, pkt.battle_id(), pkt.pawn_id(), Protocol::AxialCoord::default_instance(),
			Protocol::AxialCoord::default_instance(), 0, Protocol::BATTLE_MOVE_RESULT_NOT_WALKABLE, "target is missing");
		return false;
	}

	const uint64 ownerId = player->objectInfo->object_id();

	{
		lock_guard<mutex> lock(GBattleLock);

		auto battleIt = GBattles.find(pkt.battle_id());
		if (battleIt == GBattles.end())
		{
			SendBattleMoveResult(session, false, pkt.battle_id(), pkt.pawn_id(), Protocol::AxialCoord::default_instance(),
				pkt.target(), 0, Protocol::BATTLE_MOVE_RESULT_INVALID_BATTLE, "invalid battle");
			return false;
		}

		BattleState& battle = battleIt->second;
		BattlePawnState* pawn = FindPawn(battle, pkt.pawn_id());
		if (pawn == nullptr)
		{
			SendBattleMoveResult(session, false, battle.battleId, pkt.pawn_id(), Protocol::AxialCoord::default_instance(),
				pkt.target(), battle.currentTurnPawnId, Protocol::BATTLE_MOVE_RESULT_INVALID_PAWN, "invalid pawn");
			return false;
		}

		const Protocol::AxialCoord start = pawn->axial;

		if (pawn->ownerId != ownerId)
		{
			SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
				Protocol::BATTLE_MOVE_RESULT_NOT_OWNER, "not owner");
			return false;
		}

		if (battle.currentTurnPawnId != pawn->pawnId)
		{
			SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
				Protocol::BATTLE_MOVE_RESULT_NOT_YOUR_TURN, "not your turn");
			return false;
		}

		if (IsBattleWalkable(pkt.target()) == false)
		{
			SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
				Protocol::BATTLE_MOVE_RESULT_NOT_WALKABLE, "not walkable");
			return false;
		}

		if (AxialDistance(start, pkt.target()) > pawn->moveRange)
		{
			SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
				Protocol::BATTLE_MOVE_RESULT_OUT_OF_RANGE, "out of range");
			return false;
		}

		if (IsOccupied(battle, pkt.target(), pawn->pawnId))
		{
			SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
				Protocol::BATTLE_MOVE_RESULT_OCCUPIED, "occupied");
			return false;
		}

		pawn->axial.CopyFrom(pkt.target());
		battle.currentTurnPawnId = GetNextAlliedTurnPawnId(battle, pawn->pawnId);

		SendBattleMoveResult(session, true, battle.battleId, pawn->pawnId, start, pawn->axial, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_OK, "");
	}

	return true;
}
