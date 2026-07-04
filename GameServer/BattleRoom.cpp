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
			Protocol::BATTLE_MOVE_RESULT_NOT_OWNER, "not owner", pawn);
		return;
	}

	if (battle.currentTurnPawnId != pawn->pawnId)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_NOT_YOUR_TURN, "not your turn", pawn);
		return;
	}

	if (CanMove(*pawn) == false)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_CANNOT_MOVE, "cannot move after spending AP 2", pawn);
		return;
	}

	if (IsBattleWalkable(pkt.target()) == false)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_NOT_WALKABLE, "not walkable", pawn);
		return;
	}

	if (AxialDistance(start, pkt.target()) > pawn->moveRange)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_OUT_OF_RANGE, "out of range", pawn);
		return;
	}

	if (IsOccupied(battle, pkt.target(), pawn->pawnId))
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_OCCUPIED, "occupied", pawn);
		return;
	}

	pawn->axial.CopyFrom(pkt.target());
	pawn->hasMovedThisTurn = true;

	SendBattleMoveResult(session, true, battle.battleId, pawn->pawnId, start, pawn->axial, battle.currentTurnPawnId,
		Protocol::BATTLE_MOVE_RESULT_OK, "", pawn);
}

void BattleRoom::HandleBattleSkill(GameSessionRef session, Protocol::C_BATTLE_SKILL pkt)
{
	PlayerRef player = session ? session->player.load() : nullptr;
	const Protocol::AxialCoord requestedTargetAxial = pkt.has_target_axial()
		? pkt.target_axial()
		: Protocol::AxialCoord::default_instance();

	cout << "C_BATTLE_SKILL"
		<< " battle_id=" << pkt.battle_id()
		<< " caster_pawn_id=" << pkt.caster_pawn_id()
		<< " skill_slot=" << pkt.skill_slot()
		<< " target_pawn_id=" << pkt.target_pawn_id();
	if (pkt.has_target_axial())
		cout << " target_axial=(" << pkt.target_axial().q() << ", " << pkt.target_axial().r() << ")";
	else
		cout << " target_axial=<missing>";
	cout << endl;

	if (player == nullptr)
	{
		SendBattleSkillResult(session, false, pkt.battle_id(), pkt.caster_pawn_id(), pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, 0, "player is not in game");
		return;
	}

	auto battleIt = _battles.find(pkt.battle_id());
	if (battleIt == _battles.end())
	{
		SendBattleSkillResult(session, false, pkt.battle_id(), pkt.caster_pawn_id(), pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, 0, "invalid battle");
		return;
	}

	BattleState& battle = battleIt->second;
	BattlePawnState* caster = FindPawn(battle, pkt.caster_pawn_id());
	if (caster == nullptr)
	{
		SendBattleSkillResult(session, false, battle.battleId, pkt.caster_pawn_id(), pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "invalid caster pawn");
		return;
	}

	const uint64 ownerId = player->objectInfo->object_id();
	if (caster->ownerId != ownerId)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "not owner", caster);
		return;
	}

	if (battle.currentTurnPawnId != caster->pawnId)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "not your turn", caster);
		return;
	}

	SkillSpec skillSpec;
	string skillError;
	if (TryGetSkillSpec(pkt.skill_slot(), skillSpec, skillError) == false)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, skillError, caster);
		return;
	}

	if (skillSpec.isUltimate && caster->usedUltimate)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "ultimate already used", caster);
		return;
	}

	if (caster->currentAp < skillSpec.apCost)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "not enough ap", caster);
		return;
	}

	BattlePawnState* target = FindPawn(battle, pkt.target_pawn_id());
	if (target == nullptr)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "invalid target pawn", caster);
		return;
	}

	const Protocol::AxialCoord targetAxial = pkt.has_target_axial() ? pkt.target_axial() : target->axial;
	if (targetAxial.q() != target->axial.q() || targetAxial.r() != target->axial.r())
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			targetAxial, 0, target->hp, target->armor, battle.currentTurnPawnId, "target axial mismatch", caster, target);
		return;
	}

	if (target->hp <= 0)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			target->axial, 0, target->hp, target->armor, battle.currentTurnPawnId, "target is dead", caster, target);
		return;
	}

	if (AxialDistance(caster->axial, target->axial) > skillSpec.range)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			target->axial, 0, target->hp, target->armor, battle.currentTurnPawnId, "target out of range", caster, target);
		return;
	}

	if (skillSpec.isUltimate)
		caster->usedUltimate = true;
	else
		caster->currentAp = max(0, caster->currentAp - skillSpec.apCost);

	if (skillSpec.apCost >= 2)
		caster->hasMovedThisTurn = true;

	ApplyDamage(*target, skillSpec.damage);

	vector<Protocol::BattleActionLog> logs;
	Protocol::BattleActionLog actionLog;
	actionLog.set_attacker_pawn_id(caster->pawnId);
	actionLog.set_defender_pawn_id(target->pawnId);
	actionLog.set_skill_slot(pkt.skill_slot());
	actionLog.set_action_type(skillSpec.isUltimate ? "ultimate" : "skill");
	actionLog.set_damage(skillSpec.damage);
	actionLog.set_is_critical(false);
	actionLog.set_is_evaded(false);
	actionLog.set_is_guarded(false);
	actionLog.set_is_perfect_guarded(false);
	actionLog.set_is_counter(false);
	actionLog.set_hp_after(target->hp);
	actionLog.set_armor_after(target->armor);
	logs.push_back(actionLog);

	SendBattleSkillResult(session, true, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
		target->axial, skillSpec.damage, target->hp, target->armor, battle.currentTurnPawnId, "", caster, target, logs);
}

void BattleRoom::HandleBattleEndTurn(GameSessionRef session, Protocol::C_BATTLE_END_TURN pkt)
{
	PlayerRef player = session ? session->player.load() : nullptr;

	cout << "C_BATTLE_END_TURN"
		<< " battle_id=" << pkt.battle_id()
		<< " pawn_id=" << pkt.pawn_id()
		<< endl;

	if (player == nullptr)
	{
		SendBattleEndTurnResult(session, false, pkt.battle_id(), pkt.pawn_id(), 0, "player is not in game");
		return;
	}

	auto battleIt = _battles.find(pkt.battle_id());
	if (battleIt == _battles.end())
	{
		SendBattleEndTurnResult(session, false, pkt.battle_id(), pkt.pawn_id(), 0, "invalid battle");
		return;
	}

	BattleState& battle = battleIt->second;
	BattlePawnState* pawn = FindPawn(battle, pkt.pawn_id());
	if (pawn == nullptr)
	{
		SendBattleEndTurnResult(session, false, battle.battleId, pkt.pawn_id(), battle.currentTurnPawnId, "invalid pawn");
		return;
	}

	const uint64 ownerId = player->objectInfo->object_id();
	if (pawn->ownerId != ownerId)
	{
		SendBattleEndTurnResult(session, false, battle.battleId, pawn->pawnId, battle.currentTurnPawnId, "not owner", pawn);
		return;
	}

	if (battle.currentTurnPawnId != pawn->pawnId)
	{
		SendBattleEndTurnResult(session, false, battle.battleId, pawn->pawnId, battle.currentTurnPawnId, "not your turn", pawn);
		return;
	}

	battle.currentTurnPawnId = GetNextAlliedTurnPawnId(battle, pawn->pawnId);
	BattlePawnState* nextPawn = FindPawn(battle, battle.currentTurnPawnId);
	if (nextPawn != nullptr)
		StartTurn(*nextPawn);

	SendBattleEndTurnResult(session, true, battle.battleId, pawn->pawnId, battle.currentTurnPawnId, "", pawn, nextPawn);
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

	battle.alliedPawns.push_back(MakeBattlePawn(ownerId, Protocol::PAWN_CLASS_SUEN_AXE_SWORD, -2, 0, 100, 3, 10, true, true));
	battle.alliedPawns.push_back(MakeBattlePawn(ownerId, Protocol::PAWN_CLASS_BEIGE_FIRE, -2, 1, 80, 3, 4, false, false));
	battle.enemyPawns.push_back(MakeBattlePawn(0, Protocol::PAWN_CLASS_ZILLIAN_LONGBOW, 2, -1, 70, 3, 2, false, false));
	battle.enemyPawns.push_back(MakeBattlePawn(0, Protocol::PAWN_CLASS_ALEN_SPEAR, 2, 0, 90, 3, 8, true, true));

	battle.currentTurnPawnId = battle.alliedPawns.front().pawnId;
	StartTurn(battle.alliedPawns.front());
	return battle;
}

BattleRoom::BattlePawnState BattleRoom::MakeBattlePawn(uint64 ownerId, Protocol::PawnClass pawnClass, int32 q, int32 r, int32 hp, int32 moveRange,
	int32 maxArmor, bool isShieldUnit, bool isMelee)
{
	BattlePawnState pawn;
	pawn.pawnId = _battlePawnIdGenerator++;
	pawn.ownerId = ownerId;
	pawn.pawnClass = pawnClass;
	pawn.axial = MakeAxial(q, r);
	pawn.hp = hp;
	pawn.maxHp = hp;
	pawn.moveRange = moveRange;
	pawn.armor = maxArmor;
	pawn.maxArmor = maxArmor;
	pawn.currentAp = 0;
	pawn.hasMovedThisTurn = false;
	pawn.usedSubActionThisTurn = false;
	pawn.usedUltimate = false;
	pawn.isShieldUnit = isShieldUnit;
	pawn.isMelee = isMelee;
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
	dst->set_armor(src.armor);
	dst->set_max_armor(src.maxArmor);
	dst->set_current_ap(src.currentAp);
	dst->set_can_move(CanMove(src));
	dst->set_used_sub_action_this_turn(src.usedSubActionThisTurn);
	dst->set_used_ultimate(src.usedUltimate);
	dst->set_is_shield_unit(src.isShieldUnit);
	dst->set_is_melee(src.isMelee);
}

void BattleRoom::CopyBattlePawnDelta(const BattlePawnState& src, Protocol::BattlePawnDelta* dst)
{
	dst->set_pawn_id(src.pawnId);
	dst->set_hp(src.hp);
	dst->set_armor(src.armor);
	dst->set_current_ap(src.currentAp);
	dst->set_can_move(CanMove(src));
	dst->set_used_sub_action_this_turn(src.usedSubActionThisTurn);
	dst->set_used_ultimate(src.usedUltimate);
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

bool BattleRoom::TryGetSkillSpec(int32 skillSlot, SkillSpec& spec, string& reason)
{
	switch (skillSlot)
	{
	case 1:
		spec.apCost = 1;
		spec.damage = 25;
		spec.range = 1;
		return true;
	case 2:
		spec.apCost = 2;
		spec.damage = 35;
		spec.range = 3;
		return true;
	case 3:
		spec.apCost = 2;
		spec.damage = 45;
		spec.range = 2;
		return true;
	case 4:
		spec.apCost = 2;
		spec.damage = 30;
		spec.range = 4;
		return true;
	case 5:
		spec.apCost = 0;
		spec.damage = 80;
		spec.range = 3;
		spec.isUltimate = true;
		return true;
	default:
		spec = SkillSpec();
		reason = "invalid skill slot";
		return false;
	}
}

bool BattleRoom::CanMove(const BattlePawnState& pawn)
{
	return pawn.hasMovedThisTurn == false && pawn.currentAp > 0;
}

void BattleRoom::StartTurn(BattlePawnState& pawn)
{
	pawn.currentAp = 2;
	pawn.hasMovedThisTurn = false;
	pawn.usedSubActionThisTurn = false;

	if (pawn.isShieldUnit && pawn.armor < pawn.maxArmor)
	{
		const int32 lostArmor = pawn.maxArmor - pawn.armor;
		const int32 recoverArmor = lostArmor / 2;
		pawn.armor = min(pawn.maxArmor, pawn.armor + recoverArmor);
	}
}

void BattleRoom::ApplyDamage(BattlePawnState& target, int32 damage)
{
	const int32 armorDamage = min(target.armor, damage);
	target.armor -= armorDamage;

	const int32 hpDamage = damage - armorDamage;
	if (hpDamage > 0)
		target.hp = max(0, target.hp - hpDamage);
}

void BattleRoom::AddActionLog(google::protobuf::RepeatedPtrField<Protocol::BattleActionLog>* logs,
	uint64 attackerPawnId, uint64 defenderPawnId, int32 skillSlot, const string& actionType,
	int32 damage, const BattlePawnState& defender, bool isCounter)
{
	Protocol::BattleActionLog* log = logs->Add();
	log->set_attacker_pawn_id(attackerPawnId);
	log->set_defender_pawn_id(defenderPawnId);
	log->set_skill_slot(skillSlot);
	log->set_action_type(actionType);
	log->set_damage(damage);
	log->set_is_critical(false);
	log->set_is_evaded(false);
	log->set_is_guarded(false);
	log->set_is_perfect_guarded(false);
	log->set_is_counter(isCounter);
	log->set_hp_after(defender.hp);
	log->set_armor_after(defender.armor);
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
	Protocol::BattleMoveResult result, const string& reason, const BattlePawnState* pawn)
{
	cout << "S_BATTLE_MOVE"
		<< " success=" << success
		<< " battle_id=" << battleId
		<< " pawn_id=" << pawnId
		<< " start=(" << start.q() << ", " << start.r() << ")"
		<< " target=(" << target.q() << ", " << target.r() << ")"
		<< " next_turn_pawn_id=" << nextTurnPawnId
		<< " result=" << Protocol::BattleMoveResult_Name(result)
		<< " remaining_ap=" << (pawn != nullptr ? pawn->currentAp : 0)
		<< " can_move=" << (pawn != nullptr ? CanMove(*pawn) : false)
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
	movePkt.set_remaining_ap(pawn != nullptr ? pawn->currentAp : 0);
	movePkt.set_can_move(pawn != nullptr ? CanMove(*pawn) : false);
	if (pawn != nullptr)
		CopyBattlePawnDelta(*pawn, movePkt.add_pawn_deltas());

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
	session->Send(sendBuffer);
}

void BattleRoom::SendBattleSkillResult(GameSessionRef session, bool success, uint64 battleId, uint64 casterPawnId,
	int32 skillSlot, uint64 targetPawnId, const Protocol::AxialCoord& targetAxial,
	int32 damage, int32 targetHp, int32 targetArmor, uint64 nextTurnPawnId, const string& reason,
	const BattlePawnState* caster, const BattlePawnState* target, const vector<Protocol::BattleActionLog>& logs)
{
	cout << "S_BATTLE_SKILL"
		<< " success=" << success
		<< " battle_id=" << battleId
		<< " caster_pawn_id=" << casterPawnId
		<< " skill_slot=" << skillSlot
		<< " target_pawn_id=" << targetPawnId
		<< " target_axial=(" << targetAxial.q() << ", " << targetAxial.r() << ")"
		<< " damage=" << damage
		<< " target_hp=" << targetHp
		<< " target_armor=" << targetArmor
		<< " next_turn_pawn_id=" << nextTurnPawnId
		<< " remaining_ap=" << (caster != nullptr ? caster->currentAp : 0)
		<< " can_move=" << (caster != nullptr ? CanMove(*caster) : false)
		<< " reason=\"" << reason << "\""
		<< endl;

	if (session == nullptr)
		return;

	Protocol::S_BATTLE_SKILL skillPkt;
	skillPkt.set_success(success);
	skillPkt.set_battle_id(battleId);
	skillPkt.set_caster_pawn_id(casterPawnId);
	skillPkt.set_skill_slot(skillSlot);
	skillPkt.set_target_pawn_id(targetPawnId);
	skillPkt.mutable_target_axial()->CopyFrom(targetAxial);
	skillPkt.set_damage(damage);
	skillPkt.set_target_hp(targetHp);
	skillPkt.set_next_turn_pawn_id(nextTurnPawnId);
	skillPkt.set_reason(reason);
	skillPkt.set_remaining_ap(caster != nullptr ? caster->currentAp : 0);
	skillPkt.set_can_move(caster != nullptr ? CanMove(*caster) : false);
	skillPkt.set_used_sub_action_this_turn(caster != nullptr ? caster->usedSubActionThisTurn : false);
	skillPkt.set_used_ultimate(caster != nullptr ? caster->usedUltimate : false);
	skillPkt.set_target_armor(targetArmor);
	if (caster != nullptr)
		CopyBattlePawnDelta(*caster, skillPkt.add_pawn_deltas());
	if (target != nullptr && target != caster)
		CopyBattlePawnDelta(*target, skillPkt.add_pawn_deltas());
	for (const Protocol::BattleActionLog& log : logs)
		skillPkt.add_logs()->CopyFrom(log);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(skillPkt);
	session->Send(sendBuffer);
}

void BattleRoom::SendBattleEndTurnResult(GameSessionRef session, bool success, uint64 battleId, uint64 pawnId,
	uint64 nextTurnPawnId, const string& reason, const BattlePawnState* pawn, const BattlePawnState* nextPawn)
{
	const BattlePawnState* responsePawn = nextPawn != nullptr ? nextPawn : pawn;

	cout << "S_BATTLE_END_TURN"
		<< " success=" << success
		<< " battle_id=" << battleId
		<< " pawn_id=" << pawnId
		<< " next_turn_pawn_id=" << nextTurnPawnId
		<< " remaining_ap=" << (responsePawn != nullptr ? responsePawn->currentAp : 0)
		<< " can_move=" << (responsePawn != nullptr ? CanMove(*responsePawn) : false)
		<< " reason=\"" << reason << "\""
		<< endl;

	if (session == nullptr)
		return;

	Protocol::S_BATTLE_END_TURN endTurnPkt;
	endTurnPkt.set_success(success);
	endTurnPkt.set_battle_id(battleId);
	endTurnPkt.set_pawn_id(pawnId);
	endTurnPkt.set_next_turn_pawn_id(nextTurnPawnId);
	endTurnPkt.set_reason(reason);
	endTurnPkt.set_remaining_ap(responsePawn != nullptr ? responsePawn->currentAp : 0);
	endTurnPkt.set_can_move(responsePawn != nullptr ? CanMove(*responsePawn) : false);
	endTurnPkt.set_used_sub_action_this_turn(responsePawn != nullptr ? responsePawn->usedSubActionThisTurn : false);
	endTurnPkt.set_used_ultimate(responsePawn != nullptr ? responsePawn->usedUltimate : false);
	if (pawn != nullptr)
		CopyBattlePawnDelta(*pawn, endTurnPkt.add_pawn_deltas());
	if (nextPawn != nullptr && nextPawn != pawn)
		CopyBattlePawnDelta(*nextPawn, endTurnPkt.add_pawn_deltas());

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(endTurnPkt);
	session->Send(sendBuffer);
}
