#include "pch.h"
#include "BattleRoom.h"
#include "BattleEffectExecutor.h"
#include "BattleTemplateManager.h"
#include "GameSession.h"
#include "Player.h"
#include "Room.h"
#include <random>

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
	BattleState& battle = GetOrCreateBattle(player);

	enterBattlePkt.set_success(true);
	FillEnterBattlePacket(battle, ownerId, enterBattlePkt);

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
	auto battleIt = _battles.find(battleId);
	if (battleIt != _battles.end())
	{
		_battleByOwnerId.erase(battleIt->second.ownerId);
		if (battleIt->second.opponentOwnerId != 0)
			_battleByOwnerId.erase(battleIt->second.opponentOwnerId);
	}
	else
	{
		_battleByOwnerId.erase(battleIdIt);
	}
	_battles.erase(battleId);

	cout << "BATTLE_ROOM_LEAVE"
		<< " owner_id=" << ownerId
		<< " battle_id=" << battleId
		<< " active=1"
		<< " reason=\"" << reason << "\""
		<< endl;
}

void BattleRoom::HandleEnterPvpBattle(GameSessionRef requesterSession, GameSessionRef targetSession)
{
	PlayerRef requester = requesterSession ? requesterSession->player.load() : nullptr;
	PlayerRef target = targetSession ? targetSession->player.load() : nullptr;

	if (requester == nullptr || target == nullptr)
	{
		cout << "BATTLE_ROOM_PVP_ENTER_FAIL reason=\"player is missing\"" << endl;
		return;
	}

	const uint64 requesterId = requester->objectInfo->object_id();
	const uint64 targetId = target->objectInfo->object_id();

	if (_battleByOwnerId.find(requesterId) != _battleByOwnerId.end() || _battleByOwnerId.find(targetId) != _battleByOwnerId.end())
	{
		Protocol::S_ENTER_BATTLE failPkt;
		failPkt.set_success(false);
		failPkt.set_reason("player already has battle");
		SendEnterBattle(requesterSession, failPkt);
		SendEnterBattle(targetSession, failPkt);
		cout << "BATTLE_ROOM_PVP_ENTER_FAIL"
			<< " requester_id=" << requesterId
			<< " target_id=" << targetId
			<< " reason=\"player already has battle\""
			<< endl;
		return;
	}

	BattleState battle = CreatePvpBattle(requester, target);
	const uint64 battleId = battle.battleId;
	auto insertResult = _battles.emplace(battleId, move(battle));
	BattleState& storedBattle = insertResult.first->second;
	storedBattle.ownerSession = requesterSession;
	storedBattle.opponentSession = targetSession;
	_battleByOwnerId[requesterId] = battleId;
	_battleByOwnerId[targetId] = battleId;

	Protocol::S_ENTER_BATTLE requesterPkt;
	requesterPkt.set_success(true);
	FillEnterBattlePacket(storedBattle, requesterId, requesterPkt);
	SendEnterBattle(requesterSession, requesterPkt);

	Protocol::S_ENTER_BATTLE targetPkt;
	targetPkt.set_success(true);
	FillEnterBattlePacket(storedBattle, targetId, targetPkt);
	SendEnterBattle(targetSession, targetPkt);

	cout << "BATTLE_ROOM_PVP_ENTER"
		<< " battle_id=" << storedBattle.battleId
		<< " requester_id=" << requesterId
		<< " target_id=" << targetId
		<< " current_turn_pawn_id=" << storedBattle.currentTurnPawnId
		<< " turn_queue_size=" << storedBattle.turnQueue.size()
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
	if (battle.isFinished)
	{
		SendBattleMoveResult(session, false, battle.battleId, pkt.pawn_id(), Protocol::AxialCoord::default_instance(),
			pkt.target(), battle.currentTurnPawnId, Protocol::BATTLE_MOVE_RESULT_NOT_YOUR_TURN, "battle already finished");
		return;
	}

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

	if (IsAlive(*pawn) == false)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_CANNOT_MOVE, "pawn is dead", pawn);
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
	UpdateFacingByMove(*pawn, start, pawn->axial);
	pawn->hasMovedThisTurn = true;
	battle.stateVersion++;

	SendBattleMoveResult(session, true, battle.battleId, pawn->pawnId, start, pawn->axial, battle.currentTurnPawnId,
		Protocol::BATTLE_MOVE_RESULT_OK, "", pawn);
	if (battle.isPvp)
	{
		GameSessionRef ownerSession = battle.ownerSession.lock();
		if (ownerSession != nullptr && ownerSession != session)
		{
			SendBattleMoveResult(ownerSession, true, battle.battleId, pawn->pawnId, start, pawn->axial, battle.currentTurnPawnId,
				Protocol::BATTLE_MOVE_RESULT_OK, "", pawn);
		}

		GameSessionRef opponentSession = battle.opponentSession.lock();
		if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
		{
			SendBattleMoveResult(opponentSession, true, battle.battleId, pawn->pawnId, start, pawn->axial, battle.currentTurnPawnId,
				Protocol::BATTLE_MOVE_RESULT_OK, "", pawn);
		}
	}
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
	if (battle.isFinished)
	{
		SendBattleSkillResult(session, false, battle.battleId, pkt.caster_pawn_id(), pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "battle already finished");
		return;
	}

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

	if (IsAlive(*caster) == false)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "caster is dead", caster);
		return;
	}

	SkillSpec skillSpec;
	string skillError;
	if (TryGetSkillSpec(caster->pawnClass, pkt.skill_slot(), skillSpec, skillError) == false)
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

	if (skillSpec.isSubAction && caster->usedSubActionThisTurn)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "sub action already used this turn", caster);
		return;
	}

	if (caster->currentAp < skillSpec.apCost)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "not enough ap", caster);
		return;
	}

	const bool selfTarget = skillSpec.targetType == "SELF" || skillSpec.targetType == "SELF_TOGGLE";
	BattlePawnState* target = selfTarget ? caster : FindPawn(battle, pkt.target_pawn_id());
	if (target == nullptr)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "invalid target pawn", caster);
		return;
	}

	const bool enemyTarget = skillSpec.targetType == "ENEMY_SINGLE" || skillSpec.targetType == "TILE_OR_ENEMY";
	const bool allyTarget = skillSpec.targetType == "ALLY_SINGLE";
	if (enemyTarget && target->ownerId == caster->ownerId)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			target->axial, 0, target->hp, target->armor, battle.currentTurnPawnId, "cannot target ally", caster, target);
		return;
	}

	if (allyTarget && target->ownerId != caster->ownerId)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			target->axial, 0, target->hp, target->armor, battle.currentTurnPawnId, "target is not ally", caster, target);
		return;
	}

	const Protocol::AxialCoord targetAxial = selfTarget ? target->axial : (pkt.has_target_axial() ? pkt.target_axial() : target->axial);
	if (targetAxial.q() != target->axial.q() || targetAxial.r() != target->axial.r())
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			targetAxial, 0, target->hp, target->armor, battle.currentTurnPawnId, "target axial mismatch", caster, target);
		return;
	}

	if (IsAlive(*target) == false)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			target->axial, 0, target->hp, target->armor, battle.currentTurnPawnId, "target is dead", caster, target);
		return;
	}

	const int32 targetDistance = AxialDistance(caster->axial, target->axial);
	if (targetDistance < skillSpec.rangeMin || targetDistance > skillSpec.rangeMax)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			target->axial, 0, target->hp, target->armor, battle.currentTurnPawnId, "target out of range", caster, target);
		return;
	}

	if (skillSpec.isUltimate)
		caster->usedUltimate = true;
	else if (skillSpec.isSubAction)
		caster->usedSubActionThisTurn = true;
	else
		caster->currentAp = max(0, caster->currentAp - skillSpec.apCost);

	if (skillSpec.apCost >= 2)
		caster->hasMovedThisTurn = true;

	const bool isBackAttack = IsBackAttack(*caster, *target);
	const bool targetWasAlive = IsAlive(*target);

	vector<Protocol::BattleActionLog> logs;
	int32 appliedDamage = skillSpec.damage;
	if (skillSpec.skillTemplate != nullptr && skillSpec.casterTemplate != nullptr)
	{
		BattleEffectExecutionRequest effectRequest;
		effectRequest.skill = skillSpec.skillTemplate;
		effectRequest.casterTemplate = skillSpec.casterTemplate;
		effectRequest.skillSlot = pkt.skill_slot();
		effectRequest.actionType = skillSpec.isUltimate ? "ultimate" : "skill";
		effectRequest.isBackAttack = isBackAttack;
		effectRequest.logs = &logs;
		effectRequest.caster.pawnId = caster->pawnId;
		effectRequest.caster.ownerId = caster->ownerId;
		effectRequest.caster.pawnClass = caster->pawnClass;
		effectRequest.caster.axial = &caster->axial;
		effectRequest.caster.hp = &caster->hp;
		effectRequest.caster.armor = &caster->armor;
		effectRequest.caster.resources = &caster->resources;
		effectRequest.caster.maxResources = &caster->maxResources;
		effectRequest.caster.barriers = &caster->barriers;
		effectRequest.caster.statuses = &caster->statuses;
		effectRequest.target.pawnId = target->pawnId;
		effectRequest.target.ownerId = target->ownerId;
		effectRequest.target.pawnClass = target->pawnClass;
		effectRequest.target.axial = &target->axial;
		effectRequest.target.hp = &target->hp;
		effectRequest.target.armor = &target->armor;
		effectRequest.target.resources = &target->resources;
		effectRequest.target.maxResources = &target->maxResources;
		effectRequest.target.barriers = &target->barriers;
		effectRequest.target.statuses = &target->statuses;
		effectRequest.barrierIdGenerator = &_barrierIdGenerator;

		BattleEffectExecutor executor;
		const BattleEffectExecutionResult effectResult = executor.ExecuteOnCast(effectRequest);
		appliedDamage = effectResult.totalDamage;
	}
	else
	{
		ApplyDamage(*target, appliedDamage);
		Protocol::BattleActionLog actionLog;
		actionLog.set_attacker_pawn_id(caster->pawnId);
		actionLog.set_defender_pawn_id(target->pawnId);
		actionLog.set_skill_slot(pkt.skill_slot());
		actionLog.set_action_type(skillSpec.isUltimate ? "ultimate" : "skill");
		actionLog.set_damage(appliedDamage);
		actionLog.set_is_critical(false);
		actionLog.set_is_evaded(false);
		actionLog.set_is_guarded(false);
		actionLog.set_is_perfect_guarded(false);
		actionLog.set_is_counter(false);
		actionLog.set_is_back_attack(isBackAttack);
		actionLog.set_hp_after(target->hp);
		actionLog.set_armor_after(target->armor);
		logs.push_back(actionLog);
	}

	if (targetWasAlive && target->hp <= 0)
	{
		target->hp = 0;
		target->currentAp = 0;
		target->hasMovedThisTurn = true;
		target->isDead = true;
		battle.turnQueue.erase(remove(battle.turnQueue.begin(), battle.turnQueue.end(), target->pawnId), battle.turnQueue.end());
		if (battle.turnQueueIndex > battle.turnQueue.size())
			battle.turnQueueIndex = battle.turnQueue.size();
	}
	battle.stateVersion++;

	SendBattleSkillResult(session, true, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
		target->axial, appliedDamage, target->hp, target->armor, battle.currentTurnPawnId, "", caster, target, logs);
	if (battle.isPvp)
	{
		GameSessionRef ownerSession = battle.ownerSession.lock();
		if (ownerSession != nullptr && ownerSession != session)
		{
			SendBattleSkillResult(ownerSession, true, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
				target->axial, appliedDamage, target->hp, target->armor, battle.currentTurnPawnId, "", caster, target, logs);
		}

		GameSessionRef opponentSession = battle.opponentSession.lock();
		if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
		{
			SendBattleSkillResult(opponentSession, true, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
				target->axial, appliedDamage, target->hp, target->armor, battle.currentTurnPawnId, "", caster, target, logs);
		}
	}

	if (target->isDead)
	{
		SendBattlePawnDead(session, battle.battleId, target->pawnId, caster->pawnId);
		if (battle.isPvp)
		{
			GameSessionRef ownerSession = battle.ownerSession.lock();
			if (ownerSession != nullptr && ownerSession != session)
				SendBattlePawnDead(ownerSession, battle.battleId, target->pawnId, caster->pawnId);

			GameSessionRef opponentSession = battle.opponentSession.lock();
			if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
				SendBattlePawnDead(opponentSession, battle.battleId, target->pawnId, caster->pawnId);
		}
	}

	TryFinishBattle(battle, caster->ownerId);
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
	if (battle.isFinished)
	{
		SendBattleEndTurnResult(session, false, battle.battleId, pkt.pawn_id(), battle.currentTurnPawnId, "battle already finished");
		return;
	}

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

	ExecutePassiveTrigger(*pawn, "ON_OWNER_TURN_END");
	AdvanceOwnerTurnEffects(*pawn);
	AdvanceTurn(battle);
	battle.stateVersion++;
	BattlePawnState* nextPawn = FindPawn(battle, battle.currentTurnPawnId);

	SendBattleEndTurnResult(session, true, battle.battleId, pawn->pawnId, battle.currentTurnPawnId, "", pawn, nextPawn);
	if (battle.isPvp)
	{
		GameSessionRef ownerSession = battle.ownerSession.lock();
		if (ownerSession != nullptr && ownerSession != session)
			SendBattleEndTurnResult(ownerSession, true, battle.battleId, pawn->pawnId, battle.currentTurnPawnId, "", pawn, nextPawn);

		GameSessionRef opponentSession = battle.opponentSession.lock();
		if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
			SendBattleEndTurnResult(opponentSession, true, battle.battleId, pawn->pawnId, battle.currentTurnPawnId, "", pawn, nextPawn);
	}
}

void BattleRoom::HandleBattleResultAck(GameSessionRef session, Protocol::C_BATTLE_RESULT_ACK pkt)
{
	PlayerRef player = session ? session->player.load() : nullptr;

	cout << "C_BATTLE_RESULT_ACK"
		<< " battle_id=" << pkt.battle_id();
	if (player != nullptr)
		cout << " player_id=" << player->objectInfo->object_id();
	else
		cout << " player_id=0";
	cout << endl;

	if (player == nullptr)
	{
		SendBattleResultAck(session, false, pkt.battle_id(), "player is not in game");
		return;
	}

	auto battleIt = _battles.find(pkt.battle_id());
	if (battleIt == _battles.end())
	{
		SendBattleResultAck(session, false, pkt.battle_id(), "invalid battle");
		return;
	}

	BattleState& battle = battleIt->second;
	const uint64 playerId = player->objectInfo->object_id();
	if (battle.isFinished == false)
	{
		SendBattleResultAck(session, false, battle.battleId, "battle is not finished");
		return;
	}

	if (playerId == battle.ownerId)
	{
		if (battle.ownerResultAcked)
		{
			SendBattleResultAck(session, true, battle.battleId, "already acked");
			return;
		}
		battle.ownerResultAcked = true;
		_battleByOwnerId.erase(playerId);
	}
	else if (playerId == battle.opponentOwnerId)
	{
		if (battle.opponentResultAcked)
		{
			SendBattleResultAck(session, true, battle.battleId, "already acked");
			return;
		}
		battle.opponentResultAcked = true;
		_battleByOwnerId.erase(playerId);
	}
	else
	{
		SendBattleResultAck(session, false, battle.battleId, "not battle participant");
		return;
	}

	cout << "BATTLE_RESULT_ACK_RETURN_FIELD"
		<< " battle_id=" << battle.battleId
		<< " player_id=" << playerId
		<< endl;

	GRoom->DoAsync(&Room::HandleEnterPlayerFromBattle, player, battle.battleId);

	if ((battle.isPvp == false && battle.ownerResultAcked) ||
		(battle.isPvp && battle.ownerResultAcked && battle.opponentResultAcked))
	{
		const uint64 battleId = battle.battleId;
		_battles.erase(battleIt);
		cout << "BATTLE_RESULT_CLEANUP"
			<< " battle_id=" << battleId
			<< endl;
	}
}

BattleRoom::BattleState& BattleRoom::GetOrCreateBattle(PlayerRef ownerPlayer)
{
	const uint64 ownerId = ownerPlayer != nullptr && ownerPlayer->objectInfo != nullptr ? ownerPlayer->objectInfo->object_id() : 0;
	auto battleIdIt = _battleByOwnerId.find(ownerId);
	if (battleIdIt != _battleByOwnerId.end())
		return _battles[battleIdIt->second];

	BattleState battle = CreateBattle(ownerPlayer);
	const uint64 battleId = battle.battleId;
	_battleByOwnerId[ownerId] = battleId;
	auto insertResult = _battles.emplace(battleId, move(battle));
	return insertResult.first->second;
}

BattleRoom::BattleState BattleRoom::CreateBattle(PlayerRef ownerPlayer)
{
	const uint64 ownerId = ownerPlayer != nullptr && ownerPlayer->objectInfo != nullptr ? ownerPlayer->objectInfo->object_id() : 0;
	BattleState battle;
	battle.battleId = _battleIdGenerator++;
	battle.ownerId = ownerId;
	battle.isPvp = false;
	battle.mapId = "Battle_Test_001";

	AddOwnedBattlePawns(battle.alliedPawns, ownerPlayer, -2, 0);
	PawnTemplate enemyTemplate;
	if (TryGetPawnTemplate(Protocol::PAWN_CLASS_BEIGE_ICE, enemyTemplate))
	{
		battle.enemyPawns.push_back(MakeBattlePawn(0, Protocol::PAWN_CLASS_BEIGE_ICE, 2, -1, enemyTemplate.hp,
			enemyTemplate.moveRange, enemyTemplate.maxArmor, enemyTemplate.role));
	}
	if (TryGetPawnTemplate(Protocol::PAWN_CLASS_BEIGE_ICE, enemyTemplate))
	{
		battle.enemyPawns.push_back(MakeBattlePawn(0, Protocol::PAWN_CLASS_BEIGE_ICE, 2, 0, enemyTemplate.hp,
			enemyTemplate.moveRange, enemyTemplate.maxArmor, enemyTemplate.role));
	}

	ExecuteBattleStartEffects(battle);
	BuildTurnQueue(battle);
	if (BattlePawnState* currentPawn = FindPawn(battle, battle.currentTurnPawnId))
		StartTurn(*currentPawn);
	return battle;
}

BattleRoom::BattleState BattleRoom::CreatePvpBattle(PlayerRef ownerPlayer, PlayerRef opponentPlayer)
{
	const uint64 ownerId = ownerPlayer != nullptr && ownerPlayer->objectInfo != nullptr ? ownerPlayer->objectInfo->object_id() : 0;
	const uint64 opponentOwnerId = opponentPlayer != nullptr && opponentPlayer->objectInfo != nullptr ? opponentPlayer->objectInfo->object_id() : 0;
	BattleState battle;
	battle.battleId = _battleIdGenerator++;
	battle.ownerId = ownerId;
	battle.opponentOwnerId = opponentOwnerId;
	battle.isPvp = true;
	battle.mapId = "Battle_PVP_001";

	AddOwnedBattlePawns(battle.alliedPawns, ownerPlayer, -2, 0);
	AddOwnedBattlePawns(battle.enemyPawns, opponentPlayer, 2, -1);

	ExecuteBattleStartEffects(battle);
	BuildTurnQueue(battle);
	if (BattlePawnState* currentPawn = FindPawn(battle, battle.currentTurnPawnId))
		StartTurn(*currentPawn);

	return battle;
}

BattleRoom::BattlePawnState BattleRoom::MakeBattlePawn(uint64 ownerId, Protocol::PawnClass pawnClass, int32 q, int32 r, int32 hp, int32 moveRange,
	int32 maxArmor, Protocol::BattlePawnRole role)
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
	pawn.isDead = false;
	pawn.facingDirection = q <= 0 ? Protocol::BATTLE_FACING_DIRECTION_RIGHT : Protocol::BATTLE_FACING_DIRECTION_LEFT;
	pawn.role = role;
	return pawn;
}

BattleRoom::BattlePawnState BattleRoom::MakeBattlePawnFromOwnedPawn(PawnRef sourcePawn, int32 q, int32 r)
{
	if (sourcePawn == nullptr)
		return BattlePawnState();

	PawnTemplate pawnTemplate;
	if (TryGetPawnTemplate(sourcePawn->pawnClass, pawnTemplate) == false)
	{
		cout << "BATTLE_PAWN_CREATE_FAIL"
			<< " owner_id=" << sourcePawn->ownerId
			<< " pawn_id=" << sourcePawn->pawnId
			<< " pawn_class=" << Protocol::PawnClass_Name(sourcePawn->pawnClass)
			<< " reason=\"missing pawn template\""
			<< endl;
		return BattlePawnState();
	}

	return MakeBattlePawn(sourcePawn->ownerId, sourcePawn->pawnClass, q, r, pawnTemplate.hp, pawnTemplate.moveRange,
		pawnTemplate.maxArmor, pawnTemplate.role);
}

void BattleRoom::AddOwnedBattlePawns(vector<BattlePawnState>& dst, PlayerRef ownerPlayer, int32 q, int32 firstR)
{
	if (ownerPlayer == nullptr)
		return;

	for (size_t i = 0; i < ownerPlayer->battlePawns.size(); i++)
	{
		PawnRef sourcePawn = ownerPlayer->battlePawns[i];
		if (sourcePawn == nullptr)
			continue;

		BattlePawnState battlePawn = MakeBattlePawnFromOwnedPawn(sourcePawn, q, firstR + static_cast<int32>(i));
		if (battlePawn.pawnId != 0)
			dst.push_back(move(battlePawn));
	}
}

bool BattleRoom::TryGetPawnTemplate(Protocol::PawnClass pawnClass, PawnTemplate& pawnTemplate)
{
	const BattlePawnClassTemplate* data = GBattleTemplates.GetPawnClassTemplate(pawnClass);
	if (data != nullptr)
	{
		constexpr int32 kBaseHp = 50;
		constexpr int32 kHpPerCon = 5;
		constexpr int32 kFixedMoveRange = 3;

		pawnTemplate.hp = kBaseHp + data->baseCon * kHpPerCon;
		pawnTemplate.moveRange = kFixedMoveRange;
		pawnTemplate.maxArmor = data->role == Protocol::BATTLE_PAWN_ROLE_TANKER ? data->baseDefense : 0;
		pawnTemplate.role = data->role;
		return true;
	}

	switch (pawnClass)
	{
	case Protocol::PAWN_CLASS_SUEN_AXE_SWORD:
		pawnTemplate.hp = 100;
		pawnTemplate.moveRange = 3;
		pawnTemplate.maxArmor = 10;
		pawnTemplate.role = Protocol::BATTLE_PAWN_ROLE_TANKER;
		return true;
	case Protocol::PAWN_CLASS_BEIGE_FIRE:
		pawnTemplate.hp = 80;
		pawnTemplate.moveRange = 3;
		pawnTemplate.maxArmor = 4;
		pawnTemplate.role = Protocol::BATTLE_PAWN_ROLE_RANGED;
		return true;
	case Protocol::PAWN_CLASS_ZILLIAN_LONGBOW:
		pawnTemplate.hp = 70;
		pawnTemplate.moveRange = 3;
		pawnTemplate.maxArmor = 2;
		pawnTemplate.role = Protocol::BATTLE_PAWN_ROLE_RANGED;
		return true;
	case Protocol::PAWN_CLASS_ALEN_SPEAR:
		pawnTemplate.hp = 90;
		pawnTemplate.moveRange = 3;
		pawnTemplate.maxArmor = 8;
		pawnTemplate.role = Protocol::BATTLE_PAWN_ROLE_MELEE;
		return true;
	default:
		pawnTemplate = PawnTemplate();
		return false;
	}
}

Protocol::AxialCoord BattleRoom::MakeAxial(int32 q, int32 r)
{
	Protocol::AxialCoord coord;
	coord.set_q(q);
	coord.set_r(r);
	return coord;
}

void BattleRoom::FillEnterBattlePacket(const BattleState& battle, uint64 viewerOwnerId, Protocol::S_ENTER_BATTLE& pkt)
{
	pkt.set_battle_id(battle.battleId);
	pkt.set_map_id(battle.mapId);
	pkt.set_current_turn_pawn_id(battle.currentTurnPawnId);
	pkt.set_battle_state_version(battle.stateVersion);

	const vector<BattlePawnState>* alliedPawns = &battle.alliedPawns;
	const vector<BattlePawnState>* enemyPawns = &battle.enemyPawns;
	if (battle.isPvp && viewerOwnerId == battle.opponentOwnerId)
	{
		alliedPawns = &battle.enemyPawns;
		enemyPawns = &battle.alliedPawns;
	}

	for (const BattlePawnState& pawn : *alliedPawns)
		CopyBattlePawn(pawn, pkt.add_allied_pawns());

	for (const BattlePawnState& pawn : *enemyPawns)
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
	dst->set_is_dead(src.isDead);
	dst->set_facing_direction(src.facingDirection);
	dst->set_role(src.role);
	for (const auto& resource : src.resources)
	{
		Protocol::BattleResourceState* resourceState = dst->add_resources();
		resourceState->set_resource_type(resource.first);
		resourceState->set_value(resource.second);
		auto maxIt = src.maxResources.find(resource.first);
		resourceState->set_max_value(maxIt != src.maxResources.end() ? maxIt->second : 0);
	}

	for (const BattleBarrierState& barrier : src.barriers)
	{
		Protocol::BattleBarrierState* barrierState = dst->add_barriers();
		barrierState->set_barrier_id(barrier.barrierId);
		barrierState->set_source_skill_key(barrier.sourceSkillKey);
		barrierState->set_value(barrier.value);
		barrierState->set_remaining_owner_turns(barrier.remainingOwnerTurns);
	}

	for (const auto& item : src.statuses)
	{
		Protocol::BattleStatusState* statusState = dst->add_statuses();
		statusState->set_status_key(item.first);
		statusState->set_stacks(item.second.stacks);
		statusState->set_remaining_owner_turns(item.second.remainingOwnerTurns);
	}
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
	dst->set_is_dead(src.isDead);
	dst->set_facing_direction(src.facingDirection);
	for (const auto& resource : src.resources)
	{
		Protocol::BattleResourceState* resourceState = dst->add_resources();
		resourceState->set_resource_type(resource.first);
		resourceState->set_value(resource.second);
		auto maxIt = src.maxResources.find(resource.first);
		resourceState->set_max_value(maxIt != src.maxResources.end() ? maxIt->second : 0);
	}

	for (const BattleBarrierState& barrier : src.barriers)
	{
		Protocol::BattleBarrierState* barrierState = dst->add_barriers();
		barrierState->set_barrier_id(barrier.barrierId);
		barrierState->set_source_skill_key(barrier.sourceSkillKey);
		barrierState->set_value(barrier.value);
		barrierState->set_remaining_owner_turns(barrier.remainingOwnerTurns);
	}

	for (const auto& item : src.statuses)
	{
		Protocol::BattleStatusState* statusState = dst->add_statuses();
		statusState->set_status_key(item.first);
		statusState->set_stacks(item.second.stacks);
		statusState->set_remaining_owner_turns(item.second.remainingOwnerTurns);
	}
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
	auto isSameCell = [this, &coord, exceptPawnId](const BattlePawnState& pawn)
		{
			return IsAlive(pawn) && pawn.pawnId != exceptPawnId && pawn.axial.q() == coord.q() && pawn.axial.r() == coord.r();
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

void BattleRoom::BuildTurnQueue(BattleState& battle)
{
	battle.turnQueue.clear();
	battle.turnQueueIndex = 0;

	for (const BattlePawnState& pawn : battle.alliedPawns)
	{
		if (IsAlive(pawn))
			battle.turnQueue.push_back(pawn.pawnId);
	}

	if (battle.isPvp)
	{
		for (const BattlePawnState& pawn : battle.enemyPawns)
		{
			if (IsAlive(pawn))
				battle.turnQueue.push_back(pawn.pawnId);
		}
	}

	if (battle.turnQueue.empty())
	{
		battle.currentTurnPawnId = 0;
		return;
	}

	static random_device rd;
	static mt19937 rng(rd());
	shuffle(battle.turnQueue.begin(), battle.turnQueue.end(), rng);
	battle.currentTurnPawnId = battle.turnQueue.front();
}

uint64 BattleRoom::AdvanceTurn(BattleState& battle)
{
	if (battle.turnQueue.empty())
		BuildTurnQueue(battle);

	if (battle.turnQueue.empty())
		return 0;

	auto currentIt = find(battle.turnQueue.begin(), battle.turnQueue.end(), battle.currentTurnPawnId);
	if (currentIt == battle.turnQueue.end())
	{
		BuildTurnQueue(battle);
	}
	else
	{
		battle.turnQueueIndex = static_cast<size_t>(distance(battle.turnQueue.begin(), currentIt)) + 1;
		if (battle.turnQueueIndex >= battle.turnQueue.size())
			BuildTurnQueue(battle);
		else
			battle.currentTurnPawnId = battle.turnQueue[battle.turnQueueIndex];
	}

	BattlePawnState* queuedPawn = FindPawn(battle, battle.currentTurnPawnId);
	while (queuedPawn != nullptr && IsAlive(*queuedPawn) == false)
	{
		battle.turnQueueIndex++;
		if (battle.turnQueueIndex >= battle.turnQueue.size())
			BuildTurnQueue(battle);
		else
			battle.currentTurnPawnId = battle.turnQueue[battle.turnQueueIndex];

		queuedPawn = FindPawn(battle, battle.currentTurnPawnId);
	}

	if (BattlePawnState* nextPawn = FindPawn(battle, battle.currentTurnPawnId))
		StartTurn(*nextPawn);

	return battle.currentTurnPawnId;
}

bool BattleRoom::TryGetSkillSpec(Protocol::PawnClass pawnClass, int32 skillSlot, SkillSpec& spec, string& reason)
{
	const BattleSkillTemplate* skill = GBattleTemplates.GetSkillByActionSlot(pawnClass, skillSlot);
	const BattlePawnClassTemplate* pawnTemplate = GBattleTemplates.GetPawnClassTemplate(pawnClass);
	if (skill != nullptr && pawnTemplate != nullptr)
	{
		if (skill->skillCategory == "PASSIVE" || skill->skillCategory == "REACTION")
		{
			spec = SkillSpec();
			reason = "skill category cannot be cast";
			return false;
		}

		spec = SkillSpec();
	spec.skillKey = skill->skillKey;
	spec.targetType = skill->targetType;
	spec.skillTemplate = skill;
	spec.casterTemplate = pawnTemplate;
	spec.apCost = skill->apCost;
	spec.damage = 0;
		spec.rangeMin = skill->rangeMin;
		spec.rangeMax = skill->rangeMax;
		spec.isUltimate = skill->actionSlot == 6;
		spec.isSubAction = skill->actionSlot == 7;
		return true;
	}

	// Legacy classes remain playable until their BattleSkill.csv rows are authored.
	auto setSpec = [&spec](int32 apCost, int32 damage, int32 range, bool isUltimate = false)
		{
			spec = SkillSpec();
			spec.apCost = apCost;
			spec.damage = damage;
			spec.rangeMin = 0;
			spec.rangeMax = range;
			spec.targetType = "ENEMY_SINGLE";
			spec.isUltimate = isUltimate;
			return true;
		};

	switch (pawnClass)
	{
	case Protocol::PAWN_CLASS_SUEN_AXE_SWORD:
		switch (skillSlot)
		{
		case 2: return setSpec(1, 30, 1);
		case 3: return setSpec(2, 45, 1);
		case 4: return setSpec(2, 35, 1);
		case 5: return setSpec(2, 55, 1);
		case 6: return setSpec(0, 90, 1, true);
		default: break;
		}
		break;
	case Protocol::PAWN_CLASS_BEIGE_FIRE:
		switch (skillSlot)
		{
		case 2: return setSpec(1, 20, 3);
		case 3: return setSpec(2, 40, 3);
		case 4: return setSpec(2, 30, 4);
		case 5: return setSpec(2, 50, 3);
		case 6: return setSpec(0, 85, 4, true);
		default: break;
		}
		break;
	case Protocol::PAWN_CLASS_ZILLIAN_LONGBOW:
		switch (skillSlot)
		{
		case 2: return setSpec(1, 20, 4);
		case 3: return setSpec(2, 35, 5);
		case 4: return setSpec(2, 45, 4);
		case 5: return setSpec(2, 30, 6);
		case 6: return setSpec(0, 80, 6, true);
		default: break;
		}
		break;
	case Protocol::PAWN_CLASS_ALEN_SPEAR:
		switch (skillSlot)
		{
		case 2: return setSpec(1, 25, 2);
		case 3: return setSpec(2, 35, 2);
		case 4: return setSpec(2, 45, 2);
		case 5: return setSpec(2, 30, 3);
		case 6: return setSpec(0, 80, 2, true);
		default: break;
		}
		break;
	default:
		break;
	}

	switch (skillSlot)
	{
	case 2:
		return setSpec(1, 25, 1);
	case 3:
		return setSpec(2, 35, 3);
	case 4:
		return setSpec(2, 45, 2);
	case 5:
		return setSpec(2, 30, 4);
	case 6:
		return setSpec(0, 80, 3, true);
	default:
		spec = SkillSpec();
		reason = "invalid skill slot";
		return false;
	}
}

bool BattleRoom::CanMove(const BattlePawnState& pawn)
{
	return IsAlive(pawn) && pawn.hasMovedThisTurn == false && pawn.currentAp > 0;
}

bool BattleRoom::CanRecoverArmor(const BattlePawnState& pawn)
{
	return pawn.role == Protocol::BATTLE_PAWN_ROLE_TANKER;
}

bool BattleRoom::IsAlive(const BattlePawnState& pawn)
{
	return pawn.isDead == false && pawn.hp > 0;
}

bool BattleRoom::HasAlivePawn(const vector<BattlePawnState>& pawns)
{
	for (const BattlePawnState& pawn : pawns)
	{
		if (IsAlive(pawn))
			return true;
	}

	return false;
}

bool BattleRoom::TryFinishBattle(BattleState& battle, uint64 fallbackWinnerOwnerId)
{
	if (battle.isFinished || battle.isPvp == false)
		return false;

	const bool ownerAlive = HasAlivePawn(battle.alliedPawns);
	const bool opponentAlive = HasAlivePawn(battle.enemyPawns);
	if (ownerAlive && opponentAlive)
		return false;

	battle.isFinished = true;
	battle.currentTurnPawnId = 0;
	battle.turnQueue.clear();
	battle.turnQueueIndex = 0;

	if (ownerAlive == false && opponentAlive == false)
		battle.winnerOwnerId = fallbackWinnerOwnerId;
	else if (ownerAlive)
		battle.winnerOwnerId = battle.ownerId;
	else
		battle.winnerOwnerId = battle.opponentOwnerId;

	battle.loserOwnerId = (battle.winnerOwnerId == battle.ownerId) ? battle.opponentOwnerId : battle.ownerId;

	cout << "BATTLE_RESULT"
		<< " battle_id=" << battle.battleId
		<< " winner_player_id=" << battle.winnerOwnerId
		<< " loser_player_id=" << battle.loserOwnerId
		<< endl;

	if (GameSessionRef ownerSession = battle.ownerSession.lock())
		SendBattleResult(ownerSession, battle, battle.ownerId);
	if (GameSessionRef opponentSession = battle.opponentSession.lock())
		SendBattleResult(opponentSession, battle, battle.opponentOwnerId);

	return true;
}

void BattleRoom::StartTurn(BattlePawnState& pawn)
{
	pawn.currentAp = 2;
	pawn.hasMovedThisTurn = false;
	pawn.usedSubActionThisTurn = false;

	if (CanRecoverArmor(pawn) && pawn.armor < pawn.maxArmor)
	{
		const int32 lostArmor = pawn.maxArmor - pawn.armor;
		const int32 recoverArmor = lostArmor / 2;
		pawn.armor = min(pawn.maxArmor, pawn.armor + recoverArmor);
	}

	ExecutePassiveTrigger(pawn, "ON_OWNER_TURN_START");
}

void BattleRoom::ExecuteBattleStartEffects(BattleState& battle)
{
	for (BattlePawnState& pawn : battle.alliedPawns)
		ExecutePassiveTrigger(pawn, "ON_BATTLE_START");

	for (BattlePawnState& pawn : battle.enemyPawns)
		ExecutePassiveTrigger(pawn, "ON_BATTLE_START");
}

void BattleRoom::ExecutePassiveTrigger(BattlePawnState& pawn, const string& trigger)
{
	const BattleSkillTemplate* passiveSkill = GBattleTemplates.GetSkillByActionSlot(pawn.pawnClass, 1);
	const BattlePawnClassTemplate* pawnTemplate = GBattleTemplates.GetPawnClassTemplate(pawn.pawnClass);
	if (passiveSkill == nullptr || pawnTemplate == nullptr || passiveSkill->skillCategory != "PASSIVE")
		return;

	BattleEffectExecutionRequest request;
	request.skill = passiveSkill;
	request.casterTemplate = pawnTemplate;
	request.skillSlot = passiveSkill->actionSlot;
	request.actionType = "passive";
	request.caster.pawnId = pawn.pawnId;
	request.caster.ownerId = pawn.ownerId;
	request.caster.pawnClass = pawn.pawnClass;
	request.caster.axial = &pawn.axial;
	request.caster.hp = &pawn.hp;
	request.caster.armor = &pawn.armor;
	request.caster.resources = &pawn.resources;
	request.caster.maxResources = &pawn.maxResources;
	request.caster.barriers = &pawn.barriers;
	request.caster.statuses = &pawn.statuses;
	request.target = request.caster;
	request.barrierIdGenerator = &_barrierIdGenerator;

	BattleEffectExecutor executor;
	executor.ExecuteTrigger(request, trigger);
}

void BattleRoom::AdvanceOwnerTurnEffects(BattlePawnState& pawn)
{
	BattleEffectPawnContext context;
	context.pawnId = pawn.pawnId;
	context.ownerId = pawn.ownerId;
	context.pawnClass = pawn.pawnClass;
	context.axial = &pawn.axial;
	context.hp = &pawn.hp;
	context.armor = &pawn.armor;
	context.resources = &pawn.resources;
	context.maxResources = &pawn.maxResources;
	context.barriers = &pawn.barriers;
	context.statuses = &pawn.statuses;

	BattleEffectExecutor executor;
	executor.AdvanceOwnerTurn(context);
}

void BattleRoom::ApplyDamage(BattlePawnState& target, int32 damage)
{
	int32 barrierDamage = 0;
	for (auto it = target.barriers.rbegin(); it != target.barriers.rend() && damage > 0; ++it)
	{
		const int32 absorbed = min(it->value, damage);
		it->value -= absorbed;
		damage -= absorbed;
		barrierDamage += absorbed;
	}

	if (barrierDamage > 0)
	{
		cout << "BATTLE_BARRIER_ABSORB"
			<< " pawn_id=" << target.pawnId
			<< " amount=" << barrierDamage
			<< endl;
	}

	auto eraseBegin = remove_if(target.barriers.begin(), target.barriers.end(), [](const BattleBarrierState& barrier)
		{
			return barrier.value <= 0;
		});
	target.barriers.erase(eraseBegin, target.barriers.end());

	const int32 armorDamage = min(target.armor, damage);
	target.armor -= armorDamage;

	const int32 hpDamage = damage - armorDamage;
	if (hpDamage > 0)
		target.hp = max(0, target.hp - hpDamage);
}

void BattleRoom::UpdateFacingByMove(BattlePawnState& pawn, const Protocol::AxialCoord& start, const Protocol::AxialCoord& target)
{
	if (target.q() > start.q())
		pawn.facingDirection = Protocol::BATTLE_FACING_DIRECTION_RIGHT;
	else if (target.q() < start.q())
		pawn.facingDirection = Protocol::BATTLE_FACING_DIRECTION_LEFT;
}

bool BattleRoom::IsBackAttack(const BattlePawnState& attacker, const BattlePawnState& defender)
{
	if (attacker.axial.q() == defender.axial.q())
		return false;

	if (defender.facingDirection == Protocol::BATTLE_FACING_DIRECTION_RIGHT)
		return attacker.axial.q() < defender.axial.q();

	if (defender.facingDirection == Protocol::BATTLE_FACING_DIRECTION_LEFT)
		return attacker.axial.q() > defender.axial.q();

	return false;
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
	log->set_is_back_attack(false);
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
	const auto battleIt = _battles.find(battleId);
	const uint64 stateVersion = battleIt != _battles.end() ? battleIt->second.stateVersion : 0;

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
		<< " battle_state_version=" << stateVersion
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
	movePkt.set_battle_state_version(stateVersion);
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
	const auto battleIt = _battles.find(battleId);
	const uint64 stateVersion = battleIt != _battles.end() ? battleIt->second.stateVersion : 0;

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
		<< " battle_state_version=" << stateVersion
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
	skillPkt.set_battle_state_version(stateVersion);
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
	const auto battleIt = _battles.find(battleId);
	const uint64 stateVersion = battleIt != _battles.end() ? battleIt->second.stateVersion : 0;

	cout << "S_BATTLE_END_TURN"
		<< " success=" << success
		<< " battle_id=" << battleId
		<< " pawn_id=" << pawnId
		<< " next_turn_pawn_id=" << nextTurnPawnId
		<< " remaining_ap=" << (responsePawn != nullptr ? responsePawn->currentAp : 0)
		<< " can_move=" << (responsePawn != nullptr ? CanMove(*responsePawn) : false)
		<< " battle_state_version=" << stateVersion
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
	endTurnPkt.set_battle_state_version(stateVersion);
	if (pawn != nullptr)
		CopyBattlePawnDelta(*pawn, endTurnPkt.add_pawn_deltas());
	if (nextPawn != nullptr && nextPawn != pawn)
		CopyBattlePawnDelta(*nextPawn, endTurnPkt.add_pawn_deltas());

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(endTurnPkt);
	session->Send(sendBuffer);
}

void BattleRoom::SendBattlePawnDead(GameSessionRef session, uint64 battleId, uint64 pawnId, uint64 killerPawnId)
{
	cout << "S_BATTLE_PAWN_DEAD"
		<< " battle_id=" << battleId
		<< " pawn_id=" << pawnId
		<< " killer_pawn_id=" << killerPawnId
		<< endl;

	if (session == nullptr)
		return;

	Protocol::S_BATTLE_PAWN_DEAD deadPkt;
	deadPkt.set_battle_id(battleId);
	deadPkt.set_pawn_id(pawnId);
	deadPkt.set_killer_pawn_id(killerPawnId);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(deadPkt);
	session->Send(sendBuffer);
}

void BattleRoom::SendBattleResult(GameSessionRef session, const BattleState& battle, uint64 viewerOwnerId)
{
	const bool victory = viewerOwnerId == battle.winnerOwnerId;

	cout << "S_BATTLE_RESULT"
		<< " battle_id=" << battle.battleId
		<< " viewer_player_id=" << viewerOwnerId
		<< " victory=" << victory
		<< " winner_player_id=" << battle.winnerOwnerId
		<< " loser_player_id=" << battle.loserOwnerId
		<< endl;

	if (session == nullptr)
		return;

	Protocol::S_BATTLE_RESULT resultPkt;
	resultPkt.set_battle_id(battle.battleId);
	resultPkt.set_victory(victory);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(resultPkt);
	session->Send(sendBuffer);
}

void BattleRoom::SendBattleResultAck(GameSessionRef session, bool success, uint64 battleId, const string& reason)
{
	cout << "S_BATTLE_RESULT_ACK"
		<< " success=" << success
		<< " battle_id=" << battleId
		<< " reason=\"" << reason << "\""
		<< endl;

	if (session == nullptr)
		return;

	Protocol::S_BATTLE_RESULT_ACK ackPkt;
	ackPkt.set_success(success);
	ackPkt.set_battle_id(battleId);
	ackPkt.set_reason(reason);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(ackPkt);
	session->Send(sendBuffer);
}
