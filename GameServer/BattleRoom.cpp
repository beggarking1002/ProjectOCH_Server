#include "pch.h"
#include "BattleRoom.h"
#include "GameSession.h"
#include "Player.h"

BattleRoomRef GBattleRoom = make_shared<BattleRoom>();

BattleRoom::BattleRoom()
{
}

BattleRoom::~BattleRoom()
{
}

void BattleRoom::HandleEnterBattle(GameSessionRef session)
{
	PlayerRef player = session ? session->player.load() : nullptr;

	Protocol::S_ENTER_BATTLE enterBattlePkt;
	if (player == nullptr)
	{
		enterBattlePkt.set_success(false);
		enterBattlePkt.set_reason("player is not in game");
		cout << "BATTLE_ROOM_ENTER_FAIL reason=\"player is not in game\"" << endl;
		SendEnterBattle(session, enterBattlePkt);
		return;
	}

	const uint64 ownerId = player->objectInfo->object_id();
	const bool created = _battleByOwnerId.find(ownerId) == _battleByOwnerId.end();
	BattleState& battle = GetOrCreateBattle(ownerId);

	enterBattlePkt.set_success(true);
	FillEnterBattlePacket(battle, enterBattlePkt);

	cout << "BATTLE_ROOM_ENTER"
		<< " owner_id=" << ownerId
		<< " battle_id=" << battle.battleId
		<< " map_id=\"" << battle.mapId << "\""
		<< " state=" << (created ? "created" : "reused")
		<< " allied_count=" << battle.alliedPawns.size()
		<< " enemy_count=" << battle.enemyPawns.size()
		<< " current_turn_pawn_id=" << battle.currentTurnPawnId
		<< endl;

	SendEnterBattle(session, enterBattlePkt);
}

void BattleRoom::HandleLeaveBattle(uint64 ownerId, string reason)
{
	auto battleIdIt = _battleByOwnerId.find(ownerId);
	if (battleIdIt == _battleByOwnerId.end())
	{
		cout << "BATTLE_ROOM_LEAVE"
			<< " owner_id=" << ownerId
			<< " active=0"
			<< " reason=\"" << reason << "\""
			<< endl;
		return;
	}

	const uint64 battleId = battleIdIt->second;
	_battleByOwnerId.erase(battleIdIt);
	_battles.erase(battleId);

	cout << "BATTLE_ROOM_LEAVE"
		<< " owner_id=" << ownerId
		<< " battle_id=" << battleId
		<< " active=1"
		<< " reason=\"" << reason << "\""
		<< endl;
}

void BattleRoom::HandleBattleMove(GameSessionRef session, Protocol::C_BATTLE_MOVE pkt)
{
	PlayerRef player = session ? session->player.load() : nullptr;

	cout << "C_BATTLE_MOVE"
		<< " battle_id=" << pkt.battle_id()
		<< " pawn_id=" << pkt.pawn_id();
	if (pkt.has_target())
		cout << " target=(" << pkt.target().q() << ", " << pkt.target().r() << ")";
	else
		cout << " target=<missing>";
	cout << endl;

	if (player == nullptr)
	{
		SendBattleMoveResult(session, false, pkt.battle_id(), pkt.pawn_id(), Protocol::AxialCoord::default_instance(),
			Protocol::AxialCoord::default_instance(), 0, Protocol::BATTLE_MOVE_RESULT_INVALID_BATTLE, "player is not in game");
		return;
	}

	if (pkt.has_target() == false)
	{
		SendBattleMoveResult(session, false, pkt.battle_id(), pkt.pawn_id(), Protocol::AxialCoord::default_instance(),
			Protocol::AxialCoord::default_instance(), 0, Protocol::BATTLE_MOVE_RESULT_NOT_WALKABLE, "target is missing");
		return;
	}

	auto battleIt = _battles.find(pkt.battle_id());
	if (battleIt == _battles.end())
	{
		SendBattleMoveResult(session, false, pkt.battle_id(), pkt.pawn_id(), Protocol::AxialCoord::default_instance(),
			pkt.target(), 0, Protocol::BATTLE_MOVE_RESULT_INVALID_BATTLE, "invalid battle");
		return;
	}

	BattleState& battle = battleIt->second;
	BattlePawnState* pawn = FindPawn(battle, pkt.pawn_id());
	if (pawn == nullptr)
	{
		SendBattleMoveResult(session, false, battle.battleId, pkt.pawn_id(), Protocol::AxialCoord::default_instance(),
			pkt.target(), battle.currentTurnPawnId, Protocol::BATTLE_MOVE_RESULT_INVALID_PAWN, "invalid pawn");
		return;
	}

	const uint64 ownerId = player->objectInfo->object_id();
	const Protocol::AxialCoord start = pawn->axial;

	if (pawn->ownerId != ownerId)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_NOT_OWNER, "not owner");
		return;
	}

	if (battle.currentTurnPawnId != pawn->pawnId)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_NOT_YOUR_TURN, "not your turn");
		return;
	}

	if (IsBattleWalkable(pkt.target()) == false)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_NOT_WALKABLE, "not walkable");
		return;
	}

	if (AxialDistance(start, pkt.target()) > pawn->moveRange)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_OUT_OF_RANGE, "out of range");
		return;
	}

	if (IsOccupied(battle, pkt.target(), pawn->pawnId))
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_OCCUPIED, "occupied");
		return;
	}

	pawn->axial.CopyFrom(pkt.target());
	battle.currentTurnPawnId = GetNextAlliedTurnPawnId(battle, pawn->pawnId);

	SendBattleMoveResult(session, true, battle.battleId, pawn->pawnId, start, pawn->axial, battle.currentTurnPawnId,
		Protocol::BATTLE_MOVE_RESULT_OK, "");
}

BattleRoom::BattleState& BattleRoom::GetOrCreateBattle(uint64 ownerId)
{
	auto battleIdIt = _battleByOwnerId.find(ownerId);
	if (battleIdIt != _battleByOwnerId.end())
		return _battles[battleIdIt->second];

	BattleState battle = CreateBattle(ownerId);
	const uint64 battleId = battle.battleId;
	_battleByOwnerId[ownerId] = battleId;
	auto insertResult = _battles.emplace(battleId, move(battle));
	return insertResult.first->second;
}

BattleRoom::BattleState BattleRoom::CreateBattle(uint64 ownerId)
{
	BattleState battle;
	battle.battleId = _battleIdGenerator++;
	battle.ownerId = ownerId;
	battle.mapId = "Battle_Test_001";

	battle.alliedPawns.push_back(MakeBattlePawn(ownerId, Protocol::PAWN_CLASS_SUEN_AXE_SWORD, -2, 0, 100, 3));
	battle.alliedPawns.push_back(MakeBattlePawn(ownerId, Protocol::PAWN_CLASS_BEIGE_FIRE, -2, 1, 80, 3));
	battle.enemyPawns.push_back(MakeBattlePawn(0, Protocol::PAWN_CLASS_ZILLIAN_LONGBOW, 2, -1, 70, 3));
	battle.enemyPawns.push_back(MakeBattlePawn(0, Protocol::PAWN_CLASS_ALEN_SPEAR, 2, 0, 90, 3));

	battle.currentTurnPawnId = battle.alliedPawns.front().pawnId;
	return battle;
}

BattleRoom::BattlePawnState BattleRoom::MakeBattlePawn(uint64 ownerId, Protocol::PawnClass pawnClass, int32 q, int32 r, int32 hp, int32 moveRange)
{
	BattlePawnState pawn;
	pawn.pawnId = _battlePawnIdGenerator++;
	pawn.ownerId = ownerId;
	pawn.pawnClass = pawnClass;
	pawn.axial = MakeAxial(q, r);
	pawn.hp = hp;
	pawn.maxHp = hp;
	pawn.moveRange = moveRange;
	return pawn;
}

Protocol::AxialCoord BattleRoom::MakeAxial(int32 q, int32 r)
{
	Protocol::AxialCoord coord;
	coord.set_q(q);
	coord.set_r(r);
	return coord;
}

void BattleRoom::FillEnterBattlePacket(const BattleState& battle, Protocol::S_ENTER_BATTLE& pkt)
{
	pkt.set_battle_id(battle.battleId);
	pkt.set_map_id(battle.mapId);
	pkt.set_current_turn_pawn_id(battle.currentTurnPawnId);

	for (const BattlePawnState& pawn : battle.alliedPawns)
		CopyBattlePawn(pawn, pkt.add_allied_pawns());

	for (const BattlePawnState& pawn : battle.enemyPawns)
		CopyBattlePawn(pawn, pkt.add_enemy_pawns());
}

void BattleRoom::CopyBattlePawn(const BattlePawnState& src, Protocol::BattlePawnInfo* dst)
{
	dst->set_pawn_id(src.pawnId);
	dst->set_owner_id(src.ownerId);
	dst->set_pawn_class(src.pawnClass);
	dst->mutable_axial()->CopyFrom(src.axial);
	dst->set_hp(src.hp);
	dst->set_max_hp(src.maxHp);
	dst->set_move_range(src.moveRange);
}

BattleRoom::BattlePawnState* BattleRoom::FindPawn(BattleState& battle, uint64 pawnId)
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

bool BattleRoom::IsOccupied(const BattleState& battle, const Protocol::AxialCoord& coord, uint64 exceptPawnId)
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

bool BattleRoom::IsBattleWalkable(const Protocol::AxialCoord& coord)
{
	constexpr int32 kBattleMapRadius = 6;
	const int32 q = coord.q();
	const int32 r = coord.r();
	const int32 s = -q - r;
	return abs(q) <= kBattleMapRadius && abs(r) <= kBattleMapRadius && abs(s) <= kBattleMapRadius;
}

int32 BattleRoom::AxialDistance(const Protocol::AxialCoord& lhs, const Protocol::AxialCoord& rhs)
{
	const int32 dq = lhs.q() - rhs.q();
	const int32 dr = lhs.r() - rhs.r();
	const int32 ds = -dq - dr;
	return (abs(dq) + abs(dr) + abs(ds)) / 2;
}

uint64 BattleRoom::GetNextAlliedTurnPawnId(const BattleState& battle, uint64 currentPawnId)
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

void BattleRoom::SendEnterBattle(GameSessionRef session, Protocol::S_ENTER_BATTLE& pkt)
{
	if (session == nullptr)
		return;

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(pkt);
	session->Send(sendBuffer);
}

void BattleRoom::SendBattleMoveResult(GameSessionRef session, bool success, uint64 battleId, uint64 pawnId,
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

	if (session == nullptr)
		return;

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
