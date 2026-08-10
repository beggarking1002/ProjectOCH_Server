#include "pch.h"
#include "BattleRoom.h"
#include "BattleRules.h"
#include "BattleEffectExecutor.h"
#include "BattleTemplateManager.h"
#include "BattleCoordinate.h"
#include "BattleMapData.h"
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

	BattlePawn* pawn = FindPawn(battle, pkt.pawn_id());
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
			Protocol::BATTLE_MOVE_RESULT_CANNOT_MOVE, "pawn has already moved this turn", pawn);
		return;
	}

	if (IsBattleWalkable(battle, pkt.target()) == false)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_NOT_WALKABLE, "not walkable", pawn);
		return;
	}

	vector<Protocol::AxialCoord> movePath;
	const bool isReachable = _movementService.TryFindPath(start, pkt.target(), _movementService.GetMoveRange(*pawn),
		[this, &battle, pawn](const Protocol::AxialCoord& axial)
		{
			return IsBattleWalkable(battle, axial) && IsOccupied(battle, axial, pawn->pawnId) == false;
		}, movePath);
	if (isReachable == false)
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_OUT_OF_RANGE, "target is unreachable", pawn);
		return;
	}

	if (IsOccupied(battle, pkt.target(), pawn->pawnId))
	{
		SendBattleMoveResult(session, false, battle.battleId, pawn->pawnId, start, start, battle.currentTurnPawnId,
			Protocol::BATTLE_MOVE_RESULT_OCCUPIED, "occupied", pawn);
		return;
	}

	vector<Protocol::BattleActionLog> zocLogs;
	vector<Protocol::BattleTileInfo> zocTileDeltas;
	vector<const BattlePawn*> zocChangedPawns;
	vector<BattlePawn*> zocDeathCandidates;
	vector<Protocol::AxialCoord> traversedPath;
	bool endedOnFire = false;
	for (const Protocol::AxialCoord& step : movePath)
	{
		const Protocol::BattleFacingDirection facing = _spatialService.GetFacingToward(pawn->axial, step, pawn->facingDirection);
		pawn->ApplyResolvedMove(step, facing);
		traversedPath.push_back(step);
		endedOnFire = GetTileOverlayType(battle, pawn->axial) == Protocol::BATTLE_TILE_OVERLAY_TYPE_FIRE;
		ApplyFireTileLandingDamage(battle, *pawn, zocLogs, zocChangedPawns, zocDeathCandidates);
		if (IsAlive(*pawn) == false)
			break;
	}
	auto tryTriggerZocFrom = [this, &battle, pawn, &start, &zocLogs, &zocTileDeltas, &zocChangedPawns, &zocDeathCandidates]
		(const vector<BattlePawnRef>& candidates)
		{
			for (const BattlePawnRef& candidate : candidates)
			{
				if (candidate == nullptr || candidate->ownerId == pawn->ownerId || IsAlive(*candidate) == false ||
					_zocService.ShouldTriggerOnEnemyMoveInZone(*candidate, start) == false)
				{
					continue;
				}

				TryExecuteZocAttack(battle, *candidate, *pawn, zocLogs, zocTileDeltas, zocChangedPawns, zocDeathCandidates);
				if (IsAlive(*pawn) == false)
					break;
			}
		};
	tryTriggerZocFrom(battle.alliedPawns);
	if (IsAlive(*pawn))
		tryTriggerZocFrom(battle.enemyPawns);
	if (IsAlive(*pawn) && traversedPath.empty() == false && endedOnFire == false)
		ApplyFireTileLandingDamage(battle, *pawn, zocLogs, zocChangedPawns, zocDeathCandidates);

	vector<pair<BattlePawn*, uint64>> zocDeaths;
	unordered_set<uint64> checkedZocDeathPawnIds;
	for (BattlePawn* candidate : zocDeathCandidates)
	{
		if (candidate == nullptr || checkedZocDeathPawnIds.insert(candidate->pawnId).second == false || candidate->isDead || candidate->hp > 0)
			continue;

		uint64 killerPawnId = 0;
		for (auto actionIt = zocLogs.rbegin(); actionIt != zocLogs.rend(); ++actionIt)
		{
			if (actionIt->defender_pawn_id() == candidate->pawnId && actionIt->is_evaded() == false && actionIt->damage() > 0)
			{
				killerPawnId = actionIt->attacker_pawn_id();
				break;
			}
		}
		candidate->MarkDefeated();
		battle.turnQueue.erase(remove(battle.turnQueue.begin(), battle.turnQueue.end(), candidate->pawnId), battle.turnQueue.end());
		zocDeaths.emplace_back(candidate, killerPawnId);
	}
	for (size_t actionIndex = 0; actionIndex < zocLogs.size(); ++actionIndex)
	{
		const Protocol::BattleActionLog& actionLog = zocLogs[actionIndex];
		cout << "BATTLE_ZOC_ACTION_LOG"
			<< " sequence=" << actionIndex
			<< " attacker_pawn_id=" << actionLog.attacker_pawn_id()
			<< " defender_pawn_id=" << actionLog.defender_pawn_id()
			<< " action_type=" << actionLog.action_type()
			<< " skill_slot=" << actionLog.skill_slot()
			<< " damage=" << actionLog.damage()
			<< " critical=" << actionLog.is_critical()
			<< " evaded=" << actionLog.is_evaded()
			<< " counter=" << actionLog.is_counter()
			<< " defender_hp_after=" << actionLog.hp_after()
			<< " defender_armor_after=" << actionLog.armor_after()
			<< endl;
	}
	battle.stateVersion++;

	SendBattleMoveResult(session, true, battle.battleId, pawn->pawnId, start, pawn->axial, battle.currentTurnPawnId,
		Protocol::BATTLE_MOVE_RESULT_OK, "", pawn, zocLogs, zocChangedPawns, zocDeaths.empty() == false, traversedPath);
	if (battle.isPvp)
	{
		GameSessionRef ownerSession = battle.ownerSession.lock();
		if (ownerSession != nullptr && ownerSession != session)
		{
			SendBattleMoveResult(ownerSession, true, battle.battleId, pawn->pawnId, start, pawn->axial, battle.currentTurnPawnId,
				Protocol::BATTLE_MOVE_RESULT_OK, "", pawn, zocLogs, zocChangedPawns, zocDeaths.empty() == false, traversedPath);
		}

		GameSessionRef opponentSession = battle.opponentSession.lock();
		if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
		{
			SendBattleMoveResult(opponentSession, true, battle.battleId, pawn->pawnId, start, pawn->axial, battle.currentTurnPawnId,
				Protocol::BATTLE_MOVE_RESULT_OK, "", pawn, zocLogs, zocChangedPawns, zocDeaths.empty() == false, traversedPath);
		}
	}

	for (const auto& death : zocDeaths)
	{
		SendBattlePawnDead(session, battle.battleId, death.first->pawnId, death.second);
		if (battle.isPvp)
		{
			GameSessionRef ownerSession = battle.ownerSession.lock();
			if (ownerSession != nullptr && ownerSession != session)
				SendBattlePawnDead(ownerSession, battle.battleId, death.first->pawnId, death.second);

			GameSessionRef opponentSession = battle.opponentSession.lock();
			if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
				SendBattlePawnDead(opponentSession, battle.battleId, death.first->pawnId, death.second);
		}
	}

	TryFinishBattle(battle, pawn->ownerId);
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

	BattlePawn* caster = FindPawn(battle, pkt.caster_pawn_id());
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
	if (TryGetSkillSpec(*caster, pkt.skill_slot(), skillSpec, skillError) == false)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, skillError, caster);
		return;
	}
	if (caster->CanActivateSkill(pkt.skill_slot(), skillError) == false)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, skillError, caster);
		return;
	}

	if (caster->CanUseSkillSlot(pkt.skill_slot(), skillError) == false)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), pkt.target_pawn_id(),
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, skillError, caster);
		return;
	}

	if (pkt.has_target_axial() == false)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), 0,
			requestedTargetAxial, 0, 0, 0, battle.currentTurnPawnId, "target axial is missing", caster);
		return;
	}

	const Protocol::AxialCoord targetAxial = pkt.target_axial();
	if (_spatialService.IsInBounds(targetAxial) == false)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), 0,
			targetAxial, 0, 0, 0, battle.currentTurnPawnId, "target tile is out of bounds", caster);
		return;
	}

	const bool selfTarget = skillSpec.targetType == "SELF" || skillSpec.targetType == "SELF_TOGGLE";
	const bool emptyTileTarget = skillSpec.targetType == "EMPTY_TILE";
	const bool pickupTileTarget = skillSpec.targetType == "PICKUP_TILE";
	const bool allyOrSelfTarget = skillSpec.targetType == "ALLY_OR_SELF";
	BattlePawn* target = FindAlivePawnAt(battle, targetAxial);
	int32 targetHpBeforeAction = target != nullptr ? target->hp : 0;
	if (selfTarget)
	{
		if (target != caster)
		{
			SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), 0,
				targetAxial, 0, 0, 0, battle.currentTurnPawnId, "self skill requires caster tile", caster);
			return;
		}
	}

	const bool enemyTarget = skillSpec.targetType == "ENEMY_SINGLE" || skillSpec.targetType == "TILE_OR_ENEMY";
	const bool allyTarget = skillSpec.targetType == "ALLY_SINGLE" || allyOrSelfTarget;
	const bool requiresPawn = selfTarget || (enemyTarget && skillSpec.targetType != "TILE_OR_ENEMY") || allyTarget;
	if (requiresPawn && target == nullptr)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), 0,
			targetAxial, 0, 0, 0, battle.currentTurnPawnId, "target pawn is not on selected tile", caster);
		return;
	}

	if (emptyTileTarget && (target != nullptr || GetTileEquipmentKey(battle, targetAxial).empty() == false))
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target != nullptr ? target->pawnId : 0,
			targetAxial, 0, target != nullptr ? target->hp : 0, target != nullptr ? target->armor : 0, battle.currentTurnPawnId,
			"target tile is occupied", caster, target);
		return;
	}
	if (pickupTileTarget && caster->CanPickupEquipment(GetTileEquipmentKey(battle, targetAxial),
		GetTileEquipmentOwnerPawnId(battle, targetAxial)) == false)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), 0,
			targetAxial, 0, 0, 0, battle.currentTurnPawnId, "selected tile does not contain this pawn's equipment", caster);
		return;
	}

	if (skillSpec.skillTemplate != nullptr && skillSpec.skillTemplate->requiredOverlayType != Protocol::BATTLE_TILE_OVERLAY_TYPE_NONE &&
		GetTileOverlayType(battle, targetAxial) != skillSpec.skillTemplate->requiredOverlayType)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), 0,
			targetAxial, 0, 0, 0, battle.currentTurnPawnId, "target tile does not have the required overlay", caster);
		return;
	}

	if (enemyTarget && target != nullptr && target->ownerId == caster->ownerId)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			targetAxial, 0, target->hp, target->armor, battle.currentTurnPawnId, "cannot target ally", caster, target);
		return;
	}
	if (enemyTarget && target != nullptr && IsTauntTargetRequired(battle, *caster, *target))
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			targetAxial, 0, target->hp, target->armor, battle.currentTurnPawnId, "taunt requires attacking Alen", caster, target);
		return;
	}

	if (allyTarget && target != nullptr && target->ownerId != caster->ownerId)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target->pawnId,
			targetAxial, 0, target->hp, target->armor, battle.currentTurnPawnId, "target is not ally", caster, target);
		return;
	}

	const int32 targetDistance = _spatialService.AxialDistance(caster->axial, targetAxial);
	if (targetDistance < skillSpec.rangeMin || targetDistance > skillSpec.rangeMax)
	{
		SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), target != nullptr ? target->pawnId : 0,
			targetAxial, 0, target != nullptr ? target->hp : 0, target != nullptr ? target->armor : 0,
			battle.currentTurnPawnId, "target out of range", caster, target);
		return;
	}

	bool wasIntercepted = false;
	const bool isSingleTargetAttack = skillSpec.skillTemplate != nullptr && skillSpec.skillTemplate->targetType == "ENEMY_SINGLE" &&
		skillSpec.skillTemplate->targetShape.empty();
	if (isSingleTargetAttack && target != nullptr)
	{
		if (BattlePawn* interceptor = FindSingleTargetInterceptor(battle, *target))
		{
			target = interceptor;
			targetHpBeforeAction = target->hp;
			wasIntercepted = true;
		}
	}

	const Protocol::AxialCoord* areaDirectionAxial = nullptr;
	if (skillSpec.skillTemplate != nullptr && skillSpec.skillTemplate->targetShape == "LINE_3")
	{
		if (pkt.has_line_direction_axial() == false)
		{
			SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), 0,
				targetAxial, 0, 0, 0, battle.currentTurnPawnId, "line direction tile is missing", caster);
			return;
		}

		const Protocol::AxialCoord& lineDirectionAxial = pkt.line_direction_axial();
		if (_spatialService.IsInBounds(lineDirectionAxial) == false)
		{
			SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), 0,
				targetAxial, 0, 0, 0, battle.currentTurnPawnId, "line direction tile is out of bounds", caster);
			return;
		}

		if (_spatialService.AxialDistance(targetAxial, lineDirectionAxial) != 1)
		{
			SendBattleSkillResult(session, false, battle.battleId, caster->pawnId, pkt.skill_slot(), 0,
				targetAxial, 0, 0, 0, battle.currentTurnPawnId, "line direction tile must be adjacent to the start tile", caster);
			return;
		}

		areaDirectionAxial = &lineDirectionAxial;
	}

	if (enemyTarget && target != nullptr && target->ownerId != caster->ownerId)
	{
		caster->facingDirection = _spatialService.GetFacingToward(caster->axial, target->axial, caster->facingDirection);
	}

	caster->MarkSkillSlotUsed(pkt.skill_slot());

	const Protocol::AxialCoord* executionTargetAxial = wasIntercepted ? &target->axial : &targetAxial;
	const bool isBackAttack = target != nullptr && _spatialService.IsBackAttack(*caster, *target);

	vector<Protocol::BattleActionLog> logs;
	vector<Protocol::BattleTileInfo> tileDeltas;
	vector<const BattlePawn*> extraChangedPawns;
	vector<BattlePawn*> affectedPawns;
	vector<BattlePawn*> deathCandidates;
	unordered_map<uint64, Protocol::AxialCoord> axialBeforeAction;
	auto capturePawnAxial = [&axialBeforeAction](const vector<BattlePawnRef>& pawns)
		{
			for (const BattlePawnRef& candidate : pawns)
			{
				if (candidate != nullptr)
					axialBeforeAction[candidate->pawnId] = candidate->axial;
			}
		};
	capturePawnAxial(battle.alliedPawns);
	capturePawnAxial(battle.enemyPawns);

	int32 appliedDamage = skillSpec.damage;
	if (skillSpec.skillTemplate != nullptr && skillSpec.casterTemplate != nullptr)
	{
		BattleSkillActionRequest actionRequest;
		actionRequest.skill = skillSpec.skillTemplate;
		actionRequest.casterTemplate = skillSpec.casterTemplate;
		actionRequest.caster = caster;
		actionRequest.target = target;
		actionRequest.skillSlot = pkt.skill_slot();
		actionRequest.isUltimate = skillSpec.isUltimate;
		actionRequest.isBackAttack = isBackAttack;
		actionRequest.isGuarded = wasIntercepted;
		actionRequest.requestOptionalPositionSwap = pkt.request_optional_position_swap();
		actionRequest.targetAxial = executionTargetAxial;
		actionRequest.shouldEvadeTarget = [this](const BattlePawn& attacker, BattlePawn& defender, const BattleSkillTemplate& skill)
			{
				return RollEvade(attacker, defender, skill);
			};
		actionRequest.shouldCriticalTarget = [this](const BattlePawn& attacker, const BattleSkillTemplate& skill)
			{
				return RollCritical(attacker, skill);
			};
		actionRequest.areaDirectionAxial = areaDirectionAxial;
		actionRequest.findAlivePawnAt = [this, &battle](const Protocol::AxialCoord& axial)
			{
				return FindAlivePawnAt(battle, axial);
			};
		actionRequest.findAdjacentAliveAlly = [this, &battle](const BattlePawn& source, uint64 excludedPawnId)
			{
				return FindAdjacentAliveAlly(battle, source, excludedPawnId);
			};
		actionRequest.findAlliedPawns = [this, &battle](const BattlePawn& source)
			{
				return FindAlliedPawns(battle, source);
			};
		actionRequest.tryPushTarget = [this, &battle](BattlePawn& attacker, BattlePawn& target)
			{
				return ResolvePush(battle, attacker, target);
			};
		actionRequest.tryRetreatCaster = [this, &battle](BattlePawn& casterPawn, BattlePawn& targetPawn)
			{
				BattleDisplacementRequest request;
				request.attacker = &casterPawn;
				request.target = &targetPawn;
				request.isWalkable = [this, &battle](const Protocol::AxialCoord& axial)
					{
						return IsBattleWalkable(battle, axial);
					};
				request.findAlivePawnAt = [this, &battle](const Protocol::AxialCoord& axial)
					{
						return FindAlivePawnAt(battle, axial);
					};
				return _displacementService.TryRetreatFromTarget(request);
			};
		actionRequest.tryDashCaster = [this, &battle](BattlePawn& casterPawn, BattlePawn* targetPawn,
			const Protocol::AxialCoord& targetAxial, int32 maxDistance)
			{
				BattleDisplacementRequest request;
				request.attacker = &casterPawn;
				request.target = targetPawn;
				request.isWalkable = [this, &battle](const Protocol::AxialCoord& axial)
					{
						return IsBattleWalkable(battle, axial);
					};
				request.findAlivePawnAt = [this, &battle](const Protocol::AxialCoord& axial)
					{
						return FindAlivePawnAt(battle, axial);
					};
				return _displacementService.TryDashTowardTarget(request, targetAxial, maxDistance);
			};
		actionRequest.getBaseTileType = [this, &battle](const Protocol::AxialCoord& axial)
			{
				return GetBaseTileType(battle, axial);
			};
		actionRequest.getTileOverlayType = [this, &battle](const Protocol::AxialCoord& axial)
			{
				return GetTileOverlayType(battle, axial);
			};
		actionRequest.setTileOverlayType = [this, &battle](const Protocol::AxialCoord& axial, Protocol::BattleTileOverlayType overlayType)
			{
				SetTileOverlayType(battle, axial, overlayType);
			};
		actionRequest.getTileEquipmentKey = [this, &battle](const Protocol::AxialCoord& axial)
			{
				return GetTileEquipmentKey(battle, axial);
			};
		actionRequest.getTileEquipmentOwnerPawnId = [this, &battle](const Protocol::AxialCoord& axial)
			{
				return GetTileEquipmentOwnerPawnId(battle, axial);
			};
		actionRequest.setTileEquipment = [this, &battle](const Protocol::AxialCoord& axial, const string& equipmentKey, uint64 ownerPawnId)
			{
				SetTileEquipment(battle, axial, equipmentKey, ownerPawnId);
			};
		actionRequest.isTileValid = [this](const Protocol::AxialCoord& axial)
			{
				return _spatialService.IsInBounds(axial);
			};
		actionRequest.barrierIdGenerator = &_barrierIdGenerator;

		BattleSkillActionResult actionResult = _skillExecutionService.Execute(actionRequest);
		appliedDamage = actionResult.appliedDamage;
		logs = move(actionResult.logs);
		tileDeltas = move(actionResult.tileDeltas);
		extraChangedPawns = move(actionResult.extraChangedPawns);
		affectedPawns = move(actionResult.affectedTargets);
		deathCandidates = move(actionResult.deathCandidates);
	}
	else
	{
		if (target == nullptr)
		{
			appliedDamage = 0;
		}
		else
		{
			affectedPawns.push_back(target);
			deathCandidates.push_back(target);
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
	}

	if (caster->BlocksMoveAfterSkill(pkt.skill_slot()))
		caster->MarkMoved();

	const bool primarySkillIsMelee = skillSpec.skillTemplate != nullptr && skillSpec.skillTemplate->combatType == "MELEE";
	const bool wasEvaded = any_of(logs.begin(), logs.end(), [](const Protocol::BattleActionLog& log)
		{
			return log.is_evaded();
		});
	unordered_set<uint64> hitDefenderIds;
	for (const Protocol::BattleActionLog& log : logs)
	{
		if (log.attacker_pawn_id() != caster->pawnId || log.is_evaded() || log.defender_pawn_id() == caster->pawnId)
			continue;
		if (BattlePawn* defender = FindPawn(battle, log.defender_pawn_id()))
		{
			defender->OnSuccessfulHitReceived(*caster);
			if (hitDefenderIds.insert(defender->pawnId).second)
				extraChangedPawns.push_back(defender);
			extraChangedPawns.push_back(caster);
		}
	}
	if (primarySkillIsMelee && target != nullptr && target->ownerId != caster->ownerId)
	{
		const int32 counterDistance = _spatialService.AxialDistance(caster->axial, target->axial);
		const auto carbasGuardIt = target->statuses.find("ALEN_SHIELD_CARBAS_GUARD");
		const bool hasCarbasGuard = carbasGuardIt != target->statuses.end() && carbasGuardIt->second.remainingOwnerTurns != 0;
		const bool counterOnSuccessfulHit = target->CanCounterattackOnSuccessfulHit();
		const bool wasSuccessfulHit = hitDefenderIds.contains(target->pawnId);
		const bool standardCounterCondition = target->UsesConditionalCounterattack() == false &&
			(wasEvaded || target->hp == targetHpBeforeAction);

		cout << "BATTLE_COUNTER_CHECK"
			<< " attacker_pawn_id=" << caster->pawnId
			<< " attacker_class=" << Protocol::PawnClass_Name(caster->pawnClass)
			<< " attacker_axial=(" << caster->axial.q() << "," << caster->axial.r() << ")"
			<< " defender_pawn_id=" << target->pawnId
			<< " defender_class=" << Protocol::PawnClass_Name(target->pawnClass)
			<< " defender_axial=(" << target->axial.q() << "," << target->axial.r() << ")"
			<< " distance=" << counterDistance
			<< " caster_alive=" << IsAlive(*caster)
			<< " defender_alive=" << IsAlive(*target)
			<< " primary_melee=" << primarySkillIsMelee
			<< " was_evaded=" << wasEvaded
			<< " successful_hit=" << wasSuccessfulHit
			<< " defender_hp_before=" << targetHpBeforeAction
			<< " defender_hp_after=" << target->hp
			<< " conditional_counter=" << target->UsesConditionalCounterattack()
			<< " carbas_guard=" << hasCarbasGuard
			<< " counter_on_successful_hit=" << counterOnSuccessfulHit
			<< " standard_counter_condition=" << standardCounterCondition
			<< endl;
	}

	if (primarySkillIsMelee && target != nullptr && target->ownerId != caster->ownerId && IsAlive(*caster) && IsAlive(*target) &&
		target->CanCounterattackOnSuccessfulHit() && hitDefenderIds.contains(target->pawnId) &&
		_spatialService.AxialDistance(caster->axial, target->axial) == 1)
	{
		TryExecuteCounterattack(battle, *target, *caster, logs, tileDeltas, extraChangedPawns, deathCandidates);
	}
	if (primarySkillIsMelee && target != nullptr && target->ownerId != caster->ownerId && IsAlive(*caster) && IsAlive(*target) &&
		target->UsesConditionalCounterattack() == false && _spatialService.AxialDistance(caster->axial, target->axial) == 1 &&
		(wasEvaded || target->hp == targetHpBeforeAction))
	{
		TryExecuteCounterattack(battle, *target, *caster, logs, tileDeltas, extraChangedPawns, deathCandidates);
	}
	TryExecuteAllyAttackZocReactions(battle, *caster, affectedPawns, logs, tileDeltas, extraChangedPawns, deathCandidates);
	for (const BattlePawnRef& candidate : battle.alliedPawns)
	{
		if (candidate == nullptr)
			continue;

		auto beforeIt = axialBeforeAction.find(candidate->pawnId);
		if (beforeIt != axialBeforeAction.end() &&
			(beforeIt->second.q() != candidate->axial.q() || beforeIt->second.r() != candidate->axial.r()))
		{
			ApplyFireTileLandingDamage(battle, *candidate, logs, extraChangedPawns, deathCandidates);
		}
	}
	for (const BattlePawnRef& candidate : battle.enemyPawns)
	{
		if (candidate == nullptr)
			continue;

		auto beforeIt = axialBeforeAction.find(candidate->pawnId);
		if (beforeIt != axialBeforeAction.end() &&
			(beforeIt->second.q() != candidate->axial.q() || beforeIt->second.r() != candidate->axial.r()))
		{
			ApplyFireTileLandingDamage(battle, *candidate, logs, extraChangedPawns, deathCandidates);
		}
	}

	vector<pair<BattlePawn*, uint64>> deadPawns;
	unordered_set<uint64> checkedPawnIds;
	for (BattlePawn* candidate : deathCandidates)
	{
		if (candidate == nullptr || checkedPawnIds.insert(candidate->pawnId).second == false || candidate->isDead || candidate->hp > 0)
			continue;

		uint64 killerPawnId = caster->pawnId;
		for (auto actionIt = logs.rbegin(); actionIt != logs.rend(); ++actionIt)
		{
			if (actionIt->defender_pawn_id() == candidate->pawnId && actionIt->is_evaded() == false && actionIt->damage() > 0)
			{
				killerPawnId = actionIt->attacker_pawn_id();
				break;
			}
		}
		candidate->MarkDefeated();
		battle.turnQueue.erase(remove(battle.turnQueue.begin(), battle.turnQueue.end(), candidate->pawnId), battle.turnQueue.end());
		deadPawns.emplace_back(candidate, killerPawnId);
	}
	if (battle.turnQueueIndex > battle.turnQueue.size())
		battle.turnQueueIndex = battle.turnQueue.size();
	battle.stateVersion++;

	const uint64 resolvedTargetPawnId = target != nullptr ? target->pawnId : 0;
	const int32 resolvedTargetHp = target != nullptr ? target->hp : 0;
	const int32 resolvedTargetArmor = target != nullptr ? target->armor : 0;
	for (size_t actionIndex = 0; actionIndex < logs.size(); ++actionIndex)
	{
		const Protocol::BattleActionLog& actionLog = logs[actionIndex];
		cout << "BATTLE_ACTION_LOG"
			<< " battle_id=" << battle.battleId
			<< " sequence=" << actionIndex
			<< " attacker_pawn_id=" << actionLog.attacker_pawn_id()
			<< " defender_pawn_id=" << actionLog.defender_pawn_id()
			<< " skill_slot=" << actionLog.skill_slot()
			<< " action_type=" << actionLog.action_type()
			<< " damage=" << actionLog.damage()
			<< " critical=" << actionLog.is_critical()
			<< " evaded=" << actionLog.is_evaded()
			<< " guarded=" << actionLog.is_guarded()
			<< " perfect_guarded=" << actionLog.is_perfect_guarded()
			<< " counter=" << actionLog.is_counter()
			<< " back_attack=" << actionLog.is_back_attack()
			<< " defender_hp_after=" << actionLog.hp_after()
			<< " defender_armor_after=" << actionLog.armor_after()
			<< endl;
	}

	SendBattleSkillResult(session, true, battle.battleId, caster->pawnId, pkt.skill_slot(), resolvedTargetPawnId,
		targetAxial, appliedDamage, resolvedTargetHp, resolvedTargetArmor, battle.currentTurnPawnId, "", caster, target, logs, tileDeltas, extraChangedPawns, deadPawns.empty() == false);
	if (battle.isPvp)
	{
		GameSessionRef ownerSession = battle.ownerSession.lock();
		if (ownerSession != nullptr && ownerSession != session)
		{
			SendBattleSkillResult(ownerSession, true, battle.battleId, caster->pawnId, pkt.skill_slot(), resolvedTargetPawnId,
				targetAxial, appliedDamage, resolvedTargetHp, resolvedTargetArmor, battle.currentTurnPawnId, "", caster, target, logs, tileDeltas, extraChangedPawns, deadPawns.empty() == false);
		}

		GameSessionRef opponentSession = battle.opponentSession.lock();
		if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
		{
			SendBattleSkillResult(opponentSession, true, battle.battleId, caster->pawnId, pkt.skill_slot(), resolvedTargetPawnId,
				targetAxial, appliedDamage, resolvedTargetHp, resolvedTargetArmor, battle.currentTurnPawnId, "", caster, target, logs, tileDeltas, extraChangedPawns, deadPawns.empty() == false);
		}
	}

	for (const auto& death : deadPawns)
	{
		SendBattlePawnDead(session, battle.battleId, death.first->pawnId, death.second);
		if (battle.isPvp)
		{
			GameSessionRef ownerSession = battle.ownerSession.lock();
			if (ownerSession != nullptr && ownerSession != session)
				SendBattlePawnDead(ownerSession, battle.battleId, death.first->pawnId, death.second);

			GameSessionRef opponentSession = battle.opponentSession.lock();
			if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
				SendBattlePawnDead(opponentSession, battle.battleId, death.first->pawnId, death.second);
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

	BattlePawn* pawn = FindPawn(battle, pkt.pawn_id());
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
	const vector<uint64> turnQueueBeforeAdvance = battle.turnQueue;
	AdvanceTurn(battle);
	const bool turnQueueResynced = turnQueueBeforeAdvance != battle.turnQueue || battle.turnStartDeaths.empty() == false;
	battle.stateVersion++;
	BattlePawn* nextPawn = FindPawn(battle, battle.currentTurnPawnId);
	vector<const BattlePawn*> extraPawns;
	unordered_set<uint64> extraPawnIds;
	for (uint64 changedPawnId : battle.turnStartChangedPawnIds)
	{
		BattlePawn* changedPawn = FindPawn(battle, changedPawnId);
		if (changedPawn != nullptr && extraPawnIds.insert(changedPawnId).second)
			extraPawns.push_back(changedPawn);
	}

	SendBattleEndTurnResult(session, true, battle.battleId, pawn->pawnId, battle.currentTurnPawnId, "", pawn, nextPawn,
		{}, extraPawns, battle.turnStartLogs, turnQueueResynced);
	if (battle.isPvp)
	{
		GameSessionRef ownerSession = battle.ownerSession.lock();
		if (ownerSession != nullptr && ownerSession != session)
			SendBattleEndTurnResult(ownerSession, true, battle.battleId, pawn->pawnId, battle.currentTurnPawnId, "", pawn, nextPawn,
				{}, extraPawns, battle.turnStartLogs, turnQueueResynced);

		GameSessionRef opponentSession = battle.opponentSession.lock();
		if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
			SendBattleEndTurnResult(opponentSession, true, battle.battleId, pawn->pawnId, battle.currentTurnPawnId, "", pawn, nextPawn,
				{}, extraPawns, battle.turnStartLogs, turnQueueResynced);

		for (const auto& death : battle.turnStartDeaths)
		{
			if (ownerSession != nullptr && ownerSession != session)
				SendBattlePawnDead(ownerSession, battle.battleId, death.first, death.second);
			if (opponentSession != nullptr && opponentSession != session && opponentSession != ownerSession)
				SendBattlePawnDead(opponentSession, battle.battleId, death.first, death.second);
		}
	}

	for (const auto& death : battle.turnStartDeaths)
		SendBattlePawnDead(session, battle.battleId, death.first, death.second);

	TryFinishBattle(battle, pawn->ownerId);
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
	battle.mapId = GBattleMapData.MapId();
	InitializeBattleTiles(battle);

	AddOwnedBattlePawns(battle.alliedPawns, ownerPlayer, -2, 0);
	constexpr int32 kTestDummyArmor = 100;
	constexpr Protocol::PawnClass kTestDummyClass = Protocol::PAWN_CLASS_SUEN_AXE_SWORD;
	BattlePawnInitialStats enemyTemplate;
	if (TryGetPawnTemplate(kTestDummyClass, enemyTemplate))
	{
		battle.enemyPawns.push_back(MakeBattlePawn(0, kTestDummyClass, 2, -1, enemyTemplate.hp,
			enemyTemplate.moveRange, kTestDummyArmor, enemyTemplate.role));
	}
	if (TryGetPawnTemplate(kTestDummyClass, enemyTemplate))
	{
		battle.enemyPawns.push_back(MakeBattlePawn(0, kTestDummyClass, 2, 0, enemyTemplate.hp,
			enemyTemplate.moveRange, kTestDummyArmor, enemyTemplate.role));
	}

	ExecuteBattleStartEffects(battle);
	BuildTurnQueue(battle);
	if (BattlePawn* currentPawn = FindPawn(battle, battle.currentTurnPawnId))
		StartTurn(battle, *currentPawn);
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
	battle.mapId = GBattleMapData.MapId();
	InitializeBattleTiles(battle);

	AddOwnedBattlePawns(battle.alliedPawns, ownerPlayer, -2, 0);
	AddOwnedBattlePawns(battle.enemyPawns, opponentPlayer, 2, -1);

	ExecuteBattleStartEffects(battle);
	BuildTurnQueue(battle);
	if (BattlePawn* currentPawn = FindPawn(battle, battle.currentTurnPawnId))
		StartTurn(battle, *currentPawn);

	return battle;
}

BattlePawnRef BattleRoom::MakeBattlePawn(uint64 ownerId, Protocol::PawnClass pawnClass, int32 cellX, int32 cellY, int32 hp, int32 moveRange,
	int32 maxArmor, Protocol::BattlePawnRole role)
{
	BattlePawnRef pawn = CreateBattlePawn(pawnClass);

	pawn->pawnId = _battlePawnIdGenerator++;
	pawn->ownerId = ownerId;
	pawn->pawnClass = pawnClass;
	pawn->axial = BattleCoordinate::CellToAxial(cellX, cellY);
	pawn->hp = hp;
	pawn->maxHp = hp;
	pawn->moveRange = moveRange;
	pawn->maxArmor = role == Protocol::BATTLE_PAWN_ROLE_TANKER ? pawn->maxHp : min(max(0, maxArmor), pawn->maxHp);
	pawn->armor = pawn->maxArmor;
	pawn->InitializeBattleActionUsage();
	pawn->isDead = false;
	pawn->facingDirection = cellX <= 0 ? Protocol::BATTLE_FACING_DIRECTION_Q_POS : Protocol::BATTLE_FACING_DIRECTION_Q_NEG;
	pawn->role = role;
	if (const BattlePawnClassTemplate* pawnTemplate = GBattleTemplates.GetPawnClassTemplate(pawnClass))
	{
		const int32 maxMorale = max(0, pawnTemplate->baseWill * BattleRules::MoraleMaxFromWillMultiplier);
		pawn->maxResources[Protocol::BATTLE_RESOURCE_TYPE_MORALE] = maxMorale;
		pawn->resources[Protocol::BATTLE_RESOURCE_TYPE_MORALE] = maxMorale;
	}
	return pawn;
}

BattlePawnRef BattleRoom::MakeBattlePawnFromOwnedPawn(PawnRef sourcePawn, int32 cellX, int32 cellY)
{
	if (sourcePawn == nullptr)
		return nullptr;

	BattlePawnInitialStats pawnTemplate;
	if (TryGetPawnTemplate(sourcePawn->pawnClass, pawnTemplate) == false)
	{
		cout << "BATTLE_PAWN_CREATE_FAIL"
			<< " owner_id=" << sourcePawn->ownerId
			<< " pawn_id=" << sourcePawn->pawnId
			<< " pawn_class=" << Protocol::PawnClass_Name(sourcePawn->pawnClass)
			<< " reason=\"missing pawn template\""
			<< endl;
		return nullptr;
	}

	return MakeBattlePawn(sourcePawn->ownerId, sourcePawn->pawnClass, cellX, cellY, pawnTemplate.hp, pawnTemplate.moveRange,
		pawnTemplate.maxArmor, pawnTemplate.role);
}

void BattleRoom::AddOwnedBattlePawns(vector<BattlePawnRef>& dst, PlayerRef ownerPlayer, int32 cellX, int32 firstCellY)
{
	if (ownerPlayer == nullptr)
		return;

	for (size_t i = 0; i < ownerPlayer->battlePawns.size(); i++)
	{
		PawnRef sourcePawn = ownerPlayer->battlePawns[i];
		if (sourcePawn == nullptr)
			continue;

		BattlePawnRef battlePawn = MakeBattlePawnFromOwnedPawn(sourcePawn, cellX, firstCellY + static_cast<int32>(i));
		if (battlePawn != nullptr)
			dst.push_back(move(battlePawn));
	}
}

bool BattleRoom::TryGetPawnTemplate(Protocol::PawnClass pawnClass, BattlePawnInitialStats& pawnTemplate)
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
		pawnTemplate = BattlePawnInitialStats();
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

uint64 BattleRoom::MakeTileKey(const Protocol::AxialCoord& axial) const
{
	return (static_cast<uint64>(static_cast<uint32>(axial.q())) << 32) |
		static_cast<uint32>(axial.r());
}

void BattleRoom::InitializeBattleTiles(BattleState& battle)
{
	battle.tileStates.clear();
	ASSERT_CRASH(GBattleMapData.IsLoaded());
	ASSERT_CRASH(battle.mapId == GBattleMapData.MapId());
	for (const Protocol::AxialCoord& axial : GBattleMapData.Tiles())
	{
		battle.tileStates.emplace(MakeTileKey(axial), BattleTileState());
	}
}

Protocol::BattleTileType BattleRoom::GetBaseTileType(const BattleState& battle, const Protocol::AxialCoord& axial) const
{
	auto it = battle.tileStates.find(MakeTileKey(axial));
	return it != battle.tileStates.end() ? it->second.baseTileType : Protocol::BATTLE_TILE_TYPE_NORMAL;
}

Protocol::BattleTileOverlayType BattleRoom::GetTileOverlayType(const BattleState& battle, const Protocol::AxialCoord& axial) const
{
	auto it = battle.tileStates.find(MakeTileKey(axial));
	return it != battle.tileStates.end() ? it->second.overlayType : Protocol::BATTLE_TILE_OVERLAY_TYPE_NONE;
}

string BattleRoom::GetTileEquipmentKey(const BattleState& battle, const Protocol::AxialCoord& axial) const
{
	auto it = battle.tileStates.find(MakeTileKey(axial));
	return it != battle.tileStates.end() ? it->second.equipmentKey : "";
}

uint64 BattleRoom::GetTileEquipmentOwnerPawnId(const BattleState& battle, const Protocol::AxialCoord& axial) const
{
	auto it = battle.tileStates.find(MakeTileKey(axial));
	return it != battle.tileStates.end() ? it->second.equipmentOwnerPawnId : 0;
}

void BattleRoom::SetTileEquipment(BattleState& battle, const Protocol::AxialCoord& axial, const string& equipmentKey, uint64 ownerPawnId)
{
	BattleTileState& state = battle.tileStates[MakeTileKey(axial)];
	state.equipmentKey = equipmentKey;
	state.equipmentOwnerPawnId = ownerPawnId;
}

void BattleRoom::SetTileOverlayType(BattleState& battle, const Protocol::AxialCoord& axial, Protocol::BattleTileOverlayType overlayType)
{
	BattleTileState& state = battle.tileStates[MakeTileKey(axial)];
	state.overlayType = overlayType;
}

void BattleRoom::AppendBattleTileStates(const BattleState& battle, google::protobuf::RepeatedPtrField<Protocol::BattleTileInfo>* dst) const
{
	vector<pair<uint64, BattleTileState>> tiles(battle.tileStates.begin(), battle.tileStates.end());
	sort(tiles.begin(), tiles.end(), [](const auto& lhs, const auto& rhs)
		{
			return lhs.first < rhs.first;
		});

	for (const auto& tile : tiles)
	{
		Protocol::BattleTileInfo* tileInfo = dst->Add();
		tileInfo->mutable_axial()->set_q(static_cast<int32>(tile.first >> 32));
		tileInfo->mutable_axial()->set_r(static_cast<int32>(tile.first & 0xFFFFFFFF));
		tileInfo->set_tile_type(tile.second.baseTileType);
		tileInfo->set_overlay_type(tile.second.overlayType);
		tileInfo->set_equipment_key(tile.second.equipmentKey);
		tileInfo->set_equipment_owner_pawn_id(tile.second.equipmentOwnerPawnId);
	}
}

vector<uint64> BattleRoom::BuildUpcomingTurnPawnIds(const BattleState& battle, size_t count) const
{
	vector<uint64> upcoming;
	if (battle.currentTurnPawnId == 0 || battle.turnQueue.empty() || count == 0)
		return upcoming;

	auto currentIt = find(battle.turnQueue.begin(), battle.turnQueue.end(), battle.currentTurnPawnId);
	if (currentIt == battle.turnQueue.end())
		return upcoming;

	const size_t startIndex = static_cast<size_t>(distance(battle.turnQueue.begin(), currentIt));
	for (size_t offset = 0; upcoming.size() < count && offset < battle.turnQueue.size() * count; ++offset)
	{
		const uint64 pawnId = battle.turnQueue[(startIndex + offset) % battle.turnQueue.size()];
		for (const BattlePawnRef& pawn : battle.alliedPawns)
		{
			if (pawn != nullptr && pawn->pawnId == pawnId && pawn->isDead == false && pawn->hp > 0)
			{
				upcoming.push_back(pawnId);
				break;
			}
		}
		if (upcoming.size() > 0 && upcoming.back() == pawnId)
			continue;
		for (const BattlePawnRef& pawn : battle.enemyPawns)
		{
			if (pawn != nullptr && pawn->pawnId == pawnId && pawn->isDead == false && pawn->hp > 0)
			{
				upcoming.push_back(pawnId);
				break;
			}
		}
	}
	return upcoming;
}

void BattleRoom::AppendUpcomingTurnPawnIds(const BattleState& battle, google::protobuf::RepeatedField<uint64>* dst) const
{
	if (dst == nullptr)
		return;
	for (uint64 pawnId : BuildUpcomingTurnPawnIds(battle))
		dst->Add(pawnId);
}

void BattleRoom::FillEnterBattlePacket(const BattleState& battle, uint64 viewerOwnerId, Protocol::S_ENTER_BATTLE& pkt)
{
	pkt.set_battle_id(battle.battleId);
	pkt.set_map_id(battle.mapId);
	pkt.set_current_turn_pawn_id(battle.currentTurnPawnId);
	pkt.set_battle_state_version(battle.stateVersion);
	AppendUpcomingTurnPawnIds(battle, pkt.mutable_upcoming_turn_pawn_ids());
	AppendBattleTileStates(battle, pkt.mutable_tiles());

	const vector<BattlePawnRef>* alliedPawns = &battle.alliedPawns;
	const vector<BattlePawnRef>* enemyPawns = &battle.enemyPawns;
	if (battle.isPvp && viewerOwnerId == battle.opponentOwnerId)
	{
		alliedPawns = &battle.enemyPawns;
		enemyPawns = &battle.alliedPawns;
	}

	for (const BattlePawnRef& pawn : *alliedPawns)
	{
		if (pawn != nullptr)
			CopyBattlePawn(*pawn, pkt.add_allied_pawns());
	}

	for (const BattlePawnRef& pawn : *enemyPawns)
	{
		if (pawn != nullptr)
			CopyBattlePawn(*pawn, pkt.add_enemy_pawns());
	}
}

void BattleRoom::CopyBattlePawn(const BattlePawn& src, Protocol::BattlePawnInfo* dst)
{
	dst->set_pawn_id(src.pawnId);
	dst->set_owner_id(src.ownerId);
	dst->set_pawn_class(src.pawnClass);
	dst->mutable_axial()->CopyFrom(src.axial);
	dst->set_hp(src.hp);
	dst->set_max_hp(src.maxHp);
	dst->set_move_range(_movementService.GetMoveRange(src));
	dst->set_armor(src.armor);
	dst->set_max_armor(src.GetEffectiveMaxArmor());
	dst->set_can_move(CanMove(src));
	dst->set_used_normal_skill_this_turn(src.usedNormalSkillThisTurn);
	dst->set_is_action_blocked(src.isActionBlockedThisTurn);
	dst->set_zoc_reactions_used_this_turn(src.zocReactionsUsedThisTurn);
	dst->set_used_sub_action_this_turn(src.usedSubActionThisTurn);
	dst->set_used_ultimate(src.usedUltimate);
	dst->set_is_dead(src.isDead);
	dst->set_facing_direction(src.facingDirection);
	dst->set_role(src.role);
	dst->set_shield_current(src.GetShieldCurrent());
	dst->set_shield_max(src.GetShieldMax());
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
		barrierState->set_max_value(barrier.maxValue);
	}

	for (const auto& item : src.statuses)
	{
		Protocol::BattleStatusState* statusState = dst->add_statuses();
		statusState->set_status_key(item.first);
		statusState->set_stacks(item.second.stacks);
		statusState->set_remaining_owner_turns(item.second.remainingOwnerTurns);
	}

	for (const auto& item : src.auras)
	{
		Protocol::BattleAuraState* auraState = dst->add_auras();
		auraState->set_source_skill_key(item.second.sourceSkillKey);
		auraState->set_radius(item.second.radius);
	}
}

void BattleRoom::CopyBattlePawnDelta(const BattlePawn& src, Protocol::BattlePawnDelta* dst)
{
	dst->set_pawn_id(src.pawnId);
	dst->mutable_axial()->CopyFrom(src.axial);
	dst->set_hp(src.hp);
	dst->set_armor(src.armor);
	dst->set_move_range(_movementService.GetMoveRange(src));
	dst->set_can_move(CanMove(src));
	dst->set_used_normal_skill_this_turn(src.usedNormalSkillThisTurn);
	dst->set_is_action_blocked(src.isActionBlockedThisTurn);
	dst->set_zoc_reactions_used_this_turn(src.zocReactionsUsedThisTurn);
	dst->set_used_sub_action_this_turn(src.usedSubActionThisTurn);
	dst->set_used_ultimate(src.usedUltimate);
	dst->set_is_dead(src.isDead);
	dst->set_facing_direction(src.facingDirection);
	dst->set_shield_current(src.GetShieldCurrent());
	dst->set_shield_max(src.GetShieldMax());
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
		barrierState->set_max_value(barrier.maxValue);
	}

	for (const auto& item : src.statuses)
	{
		Protocol::BattleStatusState* statusState = dst->add_statuses();
		statusState->set_status_key(item.first);
		statusState->set_stacks(item.second.stacks);
		statusState->set_remaining_owner_turns(item.second.remainingOwnerTurns);
	}

	for (const auto& item : src.auras)
	{
		Protocol::BattleAuraState* auraState = dst->add_auras();
		auraState->set_source_skill_key(item.second.sourceSkillKey);
		auraState->set_radius(item.second.radius);
	}
}

BattlePawn* BattleRoom::FindPawn(BattleState& battle, uint64 pawnId)
{
	for (const BattlePawnRef& pawn : battle.alliedPawns)
	{
		if (pawn != nullptr && pawn->pawnId == pawnId)
			return pawn.get();
	}

	for (const BattlePawnRef& pawn : battle.enemyPawns)
	{
		if (pawn != nullptr && pawn->pawnId == pawnId)
			return pawn.get();
	}

	return nullptr;
}

BattlePawn* BattleRoom::FindAlivePawnAt(BattleState& battle, const Protocol::AxialCoord& axial)
{
	auto findAt = [this, &axial](const vector<BattlePawnRef>& pawns) -> BattlePawn*
		{
			for (const BattlePawnRef& pawn : pawns)
			{
				if (pawn != nullptr && IsAlive(*pawn) && pawn->axial.q() == axial.q() && pawn->axial.r() == axial.r())
					return pawn.get();
			}
			return nullptr;
		};

	if (BattlePawn* pawn = findAt(battle.alliedPawns))
		return pawn;
	return findAt(battle.enemyPawns);
}

BattlePawn* BattleRoom::FindSingleTargetInterceptor(BattleState& battle, const BattlePawn& protectedPawn)
{
	auto findIn = [this, &protectedPawn](vector<BattlePawnRef>& pawns) -> BattlePawn*
		{
			for (const BattlePawnRef& candidate : pawns)
			{
				if (candidate == nullptr || candidate->pawnId == protectedPawn.pawnId || IsAlive(*candidate) == false ||
					candidate->ownerId != protectedPawn.ownerId || _spatialService.AxialDistance(candidate->axial, protectedPawn.axial) != 1)
				{
					continue;
				}
				if (candidate->CanInterceptSingleTargetAttack())
					return candidate.get();
			}
			return nullptr;
		};
	if (BattlePawn* interceptor = findIn(battle.alliedPawns))
		return interceptor;
	return findIn(battle.enemyPawns);
}

bool BattleRoom::IsTauntTargetRequired(BattleState& battle, const BattlePawn& attacker, const BattlePawn& selectedTarget)
{
	static random_device randomDevice;
	static mt19937 randomGenerator(randomDevice());
	uniform_real_distribution<double> distribution(0.0, 1.0);

	for (const auto& item : attacker.statuses)
	{
		const BattleStatusState& status = item.second;
		if (status.remainingOwnerTurns == 0 || status.forcedTargetPawnId == 0 || status.forcedTargetPawnId == selectedTarget.pawnId)
			continue;

		BattlePawn* forcedTarget = FindPawn(battle, status.forcedTargetPawnId);
		if (forcedTarget == nullptr || IsAlive(*forcedTarget) == false || forcedTarget->ownerId == attacker.ownerId)
			continue;

		const double chance = clamp(status.forcedTargetChance, 0.0, 1.0);
		const bool required = distribution(randomGenerator) < chance;
		cout << "BATTLE_TAUNT_CHECK attacker_pawn_id=" << attacker.pawnId
			<< " forced_target_pawn_id=" << forcedTarget->pawnId << " chance=" << chance
			<< " required=" << required << endl;
		if (required)
			return true;
	}
	return false;
}

BattlePawn* BattleRoom::FindAdjacentAliveAlly(BattleState& battle, const BattlePawn& source, uint64 excludedPawnId)
{
	vector<BattlePawn*> candidates;
	auto collect = [this, &candidates, &source, excludedPawnId](const vector<BattlePawnRef>& pawns)
		{
			for (const BattlePawnRef& pawn : pawns)
			{
				if (pawn != nullptr && pawn->pawnId != excludedPawnId && pawn->ownerId == source.ownerId &&
					IsAlive(*pawn) && _spatialService.AxialDistance(source.axial, pawn->axial) == 1)
				{
					candidates.push_back(pawn.get());
				}
			}
		};

	collect(battle.alliedPawns);
	collect(battle.enemyPawns);
	if (candidates.empty())
		return nullptr;

	sort(candidates.begin(), candidates.end(), [](const BattlePawn* lhs, const BattlePawn* rhs)
		{
			return lhs->pawnId < rhs->pawnId;
		});
	return candidates.front();
}

vector<BattlePawn*> BattleRoom::FindAlliedPawns(BattleState& battle, const BattlePawn& source)
{
	vector<BattlePawn*> allies;
	auto collect = [this, &allies, &source](const vector<BattlePawnRef>& pawns)
		{
			for (const BattlePawnRef& pawn : pawns)
			{
				if (pawn != nullptr && pawn->ownerId == source.ownerId && IsAlive(*pawn))
					allies.push_back(pawn.get());
			}
		};

	collect(battle.alliedPawns);
	collect(battle.enemyPawns);
	sort(allies.begin(), allies.end(), [](const BattlePawn* lhs, const BattlePawn* rhs)
		{
			return lhs->pawnId < rhs->pawnId;
		});
	return allies;
}

BattlePushResult BattleRoom::ResolvePush(BattleState& battle, BattlePawn& attacker, BattlePawn& target)
{
	BattleDisplacementRequest request;
	request.attacker = &attacker;
	request.target = &target;
	request.isWalkable = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return IsBattleWalkable(battle, axial);
		};
	request.findAlivePawnAt = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return FindAlivePawnAt(battle, axial);
		};
	return _displacementService.TryPush(request);
}

bool BattleRoom::IsOccupied(const BattleState& battle, const Protocol::AxialCoord& coord, uint64 exceptPawnId)
{
	auto isSameCell = [this, &coord, exceptPawnId](const BattlePawnRef& pawn)
		{
			return pawn != nullptr && IsAlive(*pawn) && pawn->pawnId != exceptPawnId && pawn->axial.q() == coord.q() && pawn->axial.r() == coord.r();
		};

	for (const BattlePawnRef& pawn : battle.alliedPawns)
	{
		if (isSameCell(pawn))
			return true;
	}

	for (const BattlePawnRef& pawn : battle.enemyPawns)
	{
		if (isSameCell(pawn))
			return true;
	}

	return false;
}

bool BattleRoom::IsBattleWalkable(const BattleState& battle, const Protocol::AxialCoord& coord) const
{
	if (_spatialService.IsInBounds(coord) == false)
		return false;

	const Protocol::BattleTileType baseTileType = GetBaseTileType(battle, coord);
	const Protocol::BattleTileOverlayType overlayType = GetTileOverlayType(battle, coord);
	return (baseTileType != Protocol::BATTLE_TILE_TYPE_WATER || overlayType == Protocol::BATTLE_TILE_OVERLAY_TYPE_ICE) &&
		GetTileEquipmentKey(battle, coord) != "PARVIS";
}

uint64 BattleRoom::GetNextAlliedTurnPawnId(const BattleState& battle, uint64 currentPawnId)
{
	if (battle.alliedPawns.empty())
		return 0;

	for (size_t i = 0; i < battle.alliedPawns.size(); i++)
	{
		if (battle.alliedPawns[i] != nullptr && battle.alliedPawns[i]->pawnId == currentPawnId)
		{
			const BattlePawnRef& nextPawn = battle.alliedPawns[(i + 1) % battle.alliedPawns.size()];
			return nextPawn != nullptr ? nextPawn->pawnId : 0;
		}
	}

	return battle.alliedPawns.front() != nullptr ? battle.alliedPawns.front()->pawnId : 0;
}

void BattleRoom::BuildTurnQueue(BattleState& battle)
{
	battle.turnQueue = _turnService.BuildQueue(battle.alliedPawns, battle.enemyPawns, battle.isPvp);
	battle.turnQueueIndex = 0;
	battle.currentTurnPawnId = battle.turnQueue.empty() ? 0 : battle.turnQueue.front();
}

uint64 BattleRoom::AdvanceTurn(BattleState& battle)
{
	battle.currentTurnPawnId = _turnService.Advance(battle.turnQueue, battle.turnQueueIndex, battle.currentTurnPawnId,
		[this, &battle](uint64 pawnId)
		{
			return FindPawn(battle, pawnId);
		},
		[this, &battle]()
		{
			return _turnService.BuildQueue(battle.alliedPawns, battle.enemyPawns, battle.isPvp);
		});

	if (BattlePawn* nextPawn = FindPawn(battle, battle.currentTurnPawnId))
		StartTurn(battle, *nextPawn);

	return battle.currentTurnPawnId;
}

bool BattleRoom::TryGetSkillSpec(const BattlePawn& pawn, int32 skillSlot, SkillSpec& spec, string& reason)
{
	const string overrideKey = pawn.ResolveSkillKey(skillSlot);
	const BattleSkillTemplate* skill = overrideKey.empty() ? GBattleTemplates.GetSkillByActionSlot(pawn.pawnClass, skillSlot) : GBattleTemplates.GetSkillByKey(overrideKey);
	const BattlePawnClassTemplate* pawnTemplate = GBattleTemplates.GetPawnClassTemplate(pawn.pawnClass);
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
	spec.damage = 0;
		spec.rangeMin = skill->rangeMin;
		spec.rangeMax = skill->rangeMax;
		spec.isUltimate = BattlePawn::IsUltimateSkillSlot(skill->actionSlot);
		return true;
	}

	// Compatibility fallback for classes that do not yet have authored battle data.
	auto setSpec = [&spec](int32 damage, int32 range, bool isUltimate = false)
		{
			spec = SkillSpec();
			spec.damage = damage;
			spec.rangeMin = 0;
			spec.rangeMax = range;
			spec.targetType = "ENEMY_SINGLE";
			spec.isUltimate = isUltimate;
			return true;
		};

	switch (pawn.pawnClass)
	{
	case Protocol::PAWN_CLASS_BEIGE_FIRE:
		switch (skillSlot)
		{
		case 2: return setSpec(20, 3);
		case 3: return setSpec(40, 3);
		case 4: return setSpec(30, 4);
		case 5: return setSpec(50, 3);
		case 6: return setSpec(85, 4, true);
		default: break;
		}
		break;
	default:
		break;
	}

	switch (skillSlot)
	{
	case 2:
		return setSpec(25, 1);
	case 3:
		return setSpec(35, 3);
	case 4:
		return setSpec(45, 2);
	case 5:
		return setSpec(30, 4);
	case 6:
		return setSpec(80, 3, true);
	default:
		spec = SkillSpec();
		reason = "invalid skill slot";
		return false;
	}
}

bool BattleRoom::CanMove(const BattlePawn& pawn)
{
	return pawn.CanMove();
}

bool BattleRoom::CanRecoverArmor(const BattlePawn& pawn)
{
	return pawn.role == Protocol::BATTLE_PAWN_ROLE_TANKER;
}

bool BattleRoom::IsAlive(const BattlePawn& pawn)
{
	return pawn.isDead == false && pawn.hp > 0;
}

bool BattleRoom::HasAlivePawn(const vector<BattlePawnRef>& pawns)
{
	for (const BattlePawnRef& pawn : pawns)
	{
		if (pawn != nullptr && IsAlive(*pawn))
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

void BattleRoom::StartTurn(BattleState& battle, BattlePawn& pawn)
{
	battle.turnStartChangedPawnIds.clear();
	battle.turnStartLogs.clear();
	battle.turnStartDeaths.clear();

	const auto stunIt = pawn.statuses.find(BattleRules::StunStatusKey);
	const bool isStunnedThisTurn = stunIt != pawn.statuses.end() && stunIt->second.remainingOwnerTurns != 0;

	// Expire duration effects before turn actions and player input are made available.
	if (AdvanceOwnerTurnEffects(pawn))
		battle.turnStartChangedPawnIds.push_back(pawn.pawnId);
	_skillResolver.RefreshAuraRadii(pawn);

	pawn.ResetTurnActionUsage();
	pawn.isActionBlockedThisTurn = isStunnedThisTurn;
	if (isStunnedThisTurn)
	{
		cout << "BATTLE_STUN_TURN_BLOCK"
			<< " pawn_id=" << pawn.pawnId
			<< endl;
		battle.turnStartChangedPawnIds.push_back(pawn.pawnId);
	}

	const int32 currentBarrier = max(0, pawn.GetShieldCurrent() - pawn.armor);
	const int32 armorCapacity = min(pawn.GetEffectiveMaxArmor(), max(0, pawn.maxHp - currentBarrier));
	if (CanRecoverArmor(pawn) && pawn.armor < armorCapacity)
	{
		const int32 lostArmor = armorCapacity - pawn.armor;
		const int32 recoverArmor = lostArmor / 2;
		pawn.armor = min(armorCapacity, pawn.armor + recoverArmor);
	}

	ExecutePassiveTrigger(pawn, "ON_OWNER_TURN_START");
	ExecuteAuraTurnStartEffects(battle, pawn);
}

void BattleRoom::ExecuteBattleStartEffects(BattleState& battle)
{
	for (const BattlePawnRef& pawn : battle.alliedPawns)
	{
		if (pawn != nullptr)
			ExecutePassiveTrigger(*pawn, "ON_BATTLE_START");
	}

	for (const BattlePawnRef& pawn : battle.enemyPawns)
	{
		if (pawn != nullptr)
			ExecutePassiveTrigger(*pawn, "ON_BATTLE_START");
	}
}

void BattleRoom::ExecutePassiveTrigger(BattlePawn& pawn, const string& trigger, BattlePawn* effectTarget)
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
	request.caster.maxHp = &pawn.maxHp;
	request.caster.armor = &pawn.armor;
	request.caster.resources = &pawn.resources;
	request.caster.maxResources = &pawn.maxResources;
	request.caster.barriers = &pawn.barriers;
	request.caster.statuses = &pawn.statuses;
	request.caster.statBonuses = &pawn.statBonuses;
	request.caster.auras = &pawn.auras;
	request.caster.zocModifiers = &pawn.zocModifiers;
	request.target = request.caster;
	if (effectTarget != nullptr)
	{
		request.target.pawnId = effectTarget->pawnId;
		request.target.ownerId = effectTarget->ownerId;
		request.target.pawnClass = effectTarget->pawnClass;
		request.target.axial = &effectTarget->axial;
		request.target.hp = &effectTarget->hp;
		request.target.maxHp = &effectTarget->maxHp;
		request.target.armor = &effectTarget->armor;
		request.target.resources = &effectTarget->resources;
		request.target.maxResources = &effectTarget->maxResources;
		request.target.barriers = &effectTarget->barriers;
		request.target.statuses = &effectTarget->statuses;
		request.target.statBonuses = &effectTarget->statBonuses;
		request.target.auras = &effectTarget->auras;
		request.target.zocModifiers = &effectTarget->zocModifiers;
	}
	request.casterPawn = &pawn;
	request.targetPawn = effectTarget != nullptr ? effectTarget : &pawn;
	request.barrierIdGenerator = &_barrierIdGenerator;

	BattleEffectExecutor executor;
	executor.ExecuteTrigger(request, trigger);
}

void BattleRoom::ExecuteAuraTurnStartEffects(BattleState& battle, BattlePawn& pawn)
{
	if (pawn.auras.empty())
		return;

	auto makeContext = [](BattlePawn& source)
		{
			BattleEffectPawnContext context;
			context.pawnId = source.pawnId;
			context.ownerId = source.ownerId;
			context.pawnClass = source.pawnClass;
		context.axial = &source.axial;
		context.hp = &source.hp;
		context.maxHp = &source.maxHp;
		context.armor = &source.armor;
			context.resources = &source.resources;
			context.maxResources = &source.maxResources;
			context.barriers = &source.barriers;
			context.statuses = &source.statuses;
			context.statBonuses = &source.statBonuses;
			context.auras = &source.auras;
			context.zocModifiers = &source.zocModifiers;
			return context;
		};

	for (const auto& item : pawn.auras)
	{
		const BattleAuraState& aura = item.second;
		const BattleSkillTemplate* skill = GBattleTemplates.GetSkillByKey(aura.sourceSkillKey);
		const BattlePawnClassTemplate* casterTemplate = GBattleTemplates.GetPawnClassTemplate(pawn.pawnClass);
		if (skill == nullptr || casterTemplate == nullptr)
			continue;

		BattleEffectExecutionRequest request;
		request.skill = skill;
		request.casterTemplate = casterTemplate;
		request.skillSlot = skill->actionSlot;
		request.actionType = "aura";
		request.damageMultiplier = _skillResolver.GetStatModifierMultiplier(pawn, "DAMAGE_DEALT");
		request.caster = makeContext(pawn);
		request.target = request.caster;
		request.casterPawn = &pawn;
		request.targetPawn = &pawn;
		request.barrierIdGenerator = &_barrierIdGenerator;
		request.logs = &battle.turnStartLogs;

		BattleEffectExecutor executor;
		executor.ExecuteTrigger(request, "ON_OWNER_TURN_START", BattleEffectTargetScope::CasterOnly);
		battle.turnStartChangedPawnIds.push_back(pawn.pawnId);

		for (const BattlePawnRef& candidate : battle.alliedPawns)
		{
			if (candidate != nullptr && candidate->ownerId != pawn.ownerId && IsAlive(*candidate) && _spatialService.AxialDistance(pawn.axial, candidate->axial) <= aura.radius)
			{
				request.target = makeContext(*candidate);
				request.targetPawn = candidate.get();
				executor.ExecuteTrigger(request, "ON_OWNER_TURN_START", BattleEffectTargetScope::TargetOnly);
				battle.turnStartChangedPawnIds.push_back(candidate->pawnId);
			}
		}

		for (const BattlePawnRef& candidate : battle.enemyPawns)
		{
			if (candidate != nullptr && candidate->ownerId != pawn.ownerId && IsAlive(*candidate) && _spatialService.AxialDistance(pawn.axial, candidate->axial) <= aura.radius)
			{
				request.target = makeContext(*candidate);
				request.targetPawn = candidate.get();
				executor.ExecuteTrigger(request, "ON_OWNER_TURN_START", BattleEffectTargetScope::TargetOnly);
				battle.turnStartChangedPawnIds.push_back(candidate->pawnId);
			}
		}
	}

	for (uint64 pawnId : battle.turnStartChangedPawnIds)
	{
		BattlePawn* changedPawn = FindPawn(battle, pawnId);
		if (changedPawn == nullptr || changedPawn->isDead || changedPawn->hp > 0)
			continue;

		changedPawn->MarkDefeated();
		battle.turnQueue.erase(remove(battle.turnQueue.begin(), battle.turnQueue.end(), changedPawn->pawnId), battle.turnQueue.end());
		battle.turnStartDeaths.emplace_back(changedPawn->pawnId, pawn.pawnId);
	}
}

bool BattleRoom::AdvanceOwnerTurnEffects(BattlePawn& pawn)
{
	BattleEffectPawnContext context;
	context.pawnId = pawn.pawnId;
	context.ownerId = pawn.ownerId;
	context.pawnClass = pawn.pawnClass;
	context.axial = &pawn.axial;
	context.hp = &pawn.hp;
	context.maxHp = &pawn.maxHp;
	context.armor = &pawn.armor;
	context.resources = &pawn.resources;
	context.maxResources = &pawn.maxResources;
	context.barriers = &pawn.barriers;
	context.statuses = &pawn.statuses;
	context.statBonuses = &pawn.statBonuses;

	BattleEffectExecutor executor;
	return executor.AdvanceOwnerTurn(context);
}

void BattleRoom::ApplyDamage(BattlePawn& target, int32 damage)
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

void BattleRoom::ApplyFireTileLandingDamage(BattleState& battle, BattlePawn& pawn,
	vector<Protocol::BattleActionLog>& logs, vector<const BattlePawn*>& changedPawns,
	vector<BattlePawn*>& deathCandidates)
{
	if (IsAlive(pawn) == false || GetTileOverlayType(battle, pawn.axial) != Protocol::BATTLE_TILE_OVERLAY_TYPE_FIRE)
		return;

	ApplyDamage(pawn, BattleRules::FireTileLandingDamage);

	Protocol::BattleActionLog actionLog;
	actionLog.set_attacker_pawn_id(0);
	actionLog.set_defender_pawn_id(pawn.pawnId);
	actionLog.set_skill_slot(0);
	actionLog.set_action_type("fire_tile");
	actionLog.set_damage(BattleRules::FireTileLandingDamage);
	actionLog.set_is_critical(false);
	actionLog.set_is_evaded(false);
	actionLog.set_is_guarded(false);
	actionLog.set_is_perfect_guarded(false);
	actionLog.set_is_counter(false);
	actionLog.set_is_back_attack(false);
	actionLog.set_hp_after(pawn.hp);
	actionLog.set_armor_after(pawn.armor);
	logs.push_back(actionLog);
	changedPawns.push_back(&pawn);
	deathCandidates.push_back(&pawn);

	cout << "BATTLE_FIRE_TILE_DAMAGE"
		<< " pawn_id=" << pawn.pawnId
		<< " q=" << pawn.axial.q()
		<< " r=" << pawn.axial.r()
		<< " damage=" << BattleRules::FireTileLandingDamage
		<< " hp_after=" << pawn.hp
		<< " armor_after=" << pawn.armor
		<< endl;
}

bool BattleRoom::RollEvade(const BattlePawn& attacker, BattlePawn& defender, const BattleSkillTemplate& skill)
{
	const BattlePawnClassTemplate* defenderTemplate = GBattleTemplates.GetPawnClassTemplate(defender.pawnClass);
	if (defenderTemplate == nullptr)
		return false;
	if (attacker.IsGuaranteedHit(skill))
		return false;

	if (defender.TryConsumeGuaranteedEvade())
		return true;

	const auto dexBonusIt = defender.statBonuses.find("DEX");
	const int32 baseDex = defenderTemplate->baseDex + (dexBonusIt != defender.statBonuses.end() ? dexBonusIt->second : 0);
	const double evadeRatio = static_cast<double>(max(0, baseDex + static_cast<int32>(round(
		_skillResolver.GetStatModifierAdditiveRatio(defender, "EVADE_RATE") * 100.0)))) / 100.0;
	const BattlePawnClassTemplate* attackerTemplate = GBattleTemplates.GetPawnClassTemplate(attacker.pawnClass);
	const auto focusBonusIt = attacker.statBonuses.find("FOCUS");
	const int32 baseFocus = attackerTemplate != nullptr ? attackerTemplate->baseFocus +
		(focusBonusIt != attacker.statBonuses.end() ? focusBonusIt->second : 0) : 0;
	const int32 hitRating = static_cast<int32>(round(
		GBattleTemplates.GetConfigDouble("HIT_BASE_PERCENT", 100.0) +
		baseFocus * GBattleTemplates.GetConfigDouble("FOCUS_HIT_PERCENT_PER_POINT", 1.0) +
		_skillResolver.GetStatModifierAdditiveRatio(attacker, "HIT_RATE") * 100.0 +
		attacker.GetHitRateBonus(skill)));
	const int32 finalHitPercent = clamp(static_cast<int32>(round(
		clamp(hitRating,
			GBattleTemplates.GetConfigInt("HIT_RATING_MIN", 0),
			GBattleTemplates.GetConfigInt("HIT_RATING_MAX", 150)) * (1.0 - evadeRatio))), 0, 100);
	if (finalHitPercent >= 100)
		return false;

	static thread_local mt19937 rng{ random_device{}() };
	uniform_int_distribution<int32> roll(1, 100);
	const bool evaded = roll(rng) > finalHitPercent;
	if (evaded)
	{
		cout << "BATTLE_EVADE"
			<< " attacker_pawn_id=" << attacker.pawnId
			<< " pawn_id=" << defender.pawnId
			<< " final_hit_percent=" << finalHitPercent
			<< endl;
	}

	return evaded;
}

bool BattleRoom::RollCritical(const BattlePawn& attacker, const BattleSkillTemplate& skill)
{
	if (attacker.IsGuaranteedCritical(skill))
		return true;

	const BattlePawnClassTemplate* attackerTemplate = GBattleTemplates.GetPawnClassTemplate(attacker.pawnClass);
	if (attackerTemplate == nullptr)
		return false;

	const auto focusBonusIt = attacker.statBonuses.find("FOCUS");
	const int32 focus = attackerTemplate->baseFocus + (focusBonusIt != attacker.statBonuses.end() ? focusBonusIt->second : 0);
	const int32 criticalChance = clamp(static_cast<int32>(round(
		focus * GBattleTemplates.GetConfigDouble("FOCUS_CRIT_PERCENT_PER_POINT", 1.5) +
		_skillResolver.GetStatModifierAdditiveRatio(attacker, "CRIT_RATE") * 100.0)),
		GBattleTemplates.GetConfigInt("CRIT_CHANCE_MIN", 0),
		GBattleTemplates.GetConfigInt("CRIT_CHANCE_MAX", 100));
	if (criticalChance <= 0)
		return false;
	if (criticalChance >= 100)
		return true;

	static thread_local mt19937 rng{ random_device{}() };
	uniform_int_distribution<int32> roll(1, 100);
	return roll(rng) <= criticalChance;
}

bool BattleRoom::TryExecuteZocAttack(BattleState& battle, BattlePawn& zocOwner, BattlePawn& movingPawn,
	vector<Protocol::BattleActionLog>& logs, vector<Protocol::BattleTileInfo>& tileDeltas,
	vector<const BattlePawn*>& extraChangedPawns, vector<BattlePawn*>& deathCandidates)
{
	const BattleZocProfile zocProfile = _zocService.GetProfile(zocOwner);
	if (zocOwner.CanUseZocReaction(zocProfile.reactionLimitPerTurn) == false || zocProfile.enabled == false ||
		IsAlive(movingPawn) == false)
	{
		return false;
	}

	const BattlePawnClassTemplate* casterTemplate = GBattleTemplates.GetPawnClassTemplate(zocOwner.pawnClass);
	const BattleSkillTemplate* zocSkill = GBattleTemplates.GetSkillByActionSlot(zocOwner.pawnClass, zocProfile.reactionSkillSlot);
	if (casterTemplate == nullptr || zocSkill == nullptr)
		return false;

	cout << "BATTLE_ZOC_ATTACK"
		<< " attacker_pawn_id=" << zocOwner.pawnId
		<< " defender_pawn_id=" << movingPawn.pawnId
		<< " skill_slot=" << zocProfile.reactionSkillSlot
		<< endl;

	BattleSkillActionRequest request;
	request.skill = zocSkill;
	request.casterTemplate = casterTemplate;
	request.caster = &zocOwner;
	request.target = &movingPawn;
	request.skillSlot = zocProfile.reactionSkillSlot;
	request.actionType = "zoc";
	request.targetAxial = &movingPawn.axial;
	request.shouldEvadeTarget = [this](const BattlePawn& attacker, BattlePawn& defender, const BattleSkillTemplate& skill)
		{
			return RollEvade(attacker, defender, skill);
		};
	request.shouldCriticalTarget = [this](const BattlePawn& attacker, const BattleSkillTemplate& skill)
		{
			return RollCritical(attacker, skill);
		};
	request.findAlivePawnAt = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return FindAlivePawnAt(battle, axial);
		};
	request.findAdjacentAliveAlly = [this, &battle](const BattlePawn& source, uint64 excludedPawnId)
		{
			return FindAdjacentAliveAlly(battle, source, excludedPawnId);
		};
	request.findAlliedPawns = [this, &battle](const BattlePawn& source)
		{
			return FindAlliedPawns(battle, source);
		};
	request.tryPushTarget = [this, &battle](BattlePawn& attacker, BattlePawn& target)
		{
			return ResolvePush(battle, attacker, target);
		};
	request.getBaseTileType = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return GetBaseTileType(battle, axial);
		};
	request.getTileOverlayType = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return GetTileOverlayType(battle, axial);
		};
	request.setTileOverlayType = [this, &battle](const Protocol::AxialCoord& axial, Protocol::BattleTileOverlayType overlayType)
		{
			SetTileOverlayType(battle, axial, overlayType);
		};
	request.getTileEquipmentKey = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return GetTileEquipmentKey(battle, axial);
		};
	request.getTileEquipmentOwnerPawnId = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return GetTileEquipmentOwnerPawnId(battle, axial);
		};
	request.setTileEquipment = [this, &battle](const Protocol::AxialCoord& axial, const string& equipmentKey, uint64 ownerPawnId)
		{
			SetTileEquipment(battle, axial, equipmentKey, ownerPawnId);
		};
	request.isTileValid = [this](const Protocol::AxialCoord& axial)
		{
			return _spatialService.IsInBounds(axial);
		};
	request.barrierIdGenerator = &_barrierIdGenerator;

	zocOwner.MarkZocReactionUsed();
	zocOwner.facingDirection = _spatialService.GetFacingToward(zocOwner.axial, movingPawn.axial, zocOwner.facingDirection);
	BattleSkillActionResult result = _skillExecutionService.Execute(request);
	logs.insert(logs.end(), result.logs.begin(), result.logs.end());
	tileDeltas.insert(tileDeltas.end(), result.tileDeltas.begin(), result.tileDeltas.end());
	extraChangedPawns.push_back(&zocOwner);
	extraChangedPawns.push_back(&movingPawn);
	extraChangedPawns.insert(extraChangedPawns.end(), result.extraChangedPawns.begin(), result.extraChangedPawns.end());
	deathCandidates.insert(deathCandidates.end(), result.deathCandidates.begin(), result.deathCandidates.end());

	// ZOC is a one-way opportunity attack.  A class perk may opt into a counter
	// response later, but the shared ZOC rule must not start a counter exchange.

	return true;
}

void BattleRoom::TryExecuteAllyAttackZocReactions(BattleState& battle, BattlePawn& attacker,
	const vector<BattlePawn*>& attackedPawns, vector<Protocol::BattleActionLog>& logs,
	vector<Protocol::BattleTileInfo>& tileDeltas, vector<const BattlePawn*>& extraChangedPawns,
	vector<BattlePawn*>& deathCandidates)
{
	if (IsAlive(attacker) == false)
		return;

	auto tryCandidates = [this, &battle, &attacker, &logs, &tileDeltas, &extraChangedPawns, &deathCandidates]
		(const vector<BattlePawnRef>& candidates, const BattlePawn& attackedPawn)
		{
			for (const BattlePawnRef& candidate : candidates)
			{
				if (candidate == nullptr ||
					_zocService.ShouldTriggerOnAllyAttacked(*candidate, attacker, attackedPawn) == false)
				{
					continue;
				}

				cout << "BATTLE_ZOC_ALLY_ATTACK_TRIGGER"
					<< " zoc_owner_pawn_id=" << candidate->pawnId
					<< " attacker_pawn_id=" << attacker.pawnId
					<< " attacked_ally_pawn_id=" << attackedPawn.pawnId
					<< endl;
				TryExecuteZocAttack(battle, *candidate, attacker, logs, tileDeltas, extraChangedPawns, deathCandidates);
				if (IsAlive(attacker) == false)
					return;
			}
		};

	for (BattlePawn* attackedPawn : attackedPawns)
	{
		if (attackedPawn == nullptr || attackedPawn->ownerId == attacker.ownerId || IsAlive(attacker) == false)
			continue;

		tryCandidates(battle.alliedPawns, *attackedPawn);
		if (IsAlive(attacker))
			tryCandidates(battle.enemyPawns, *attackedPawn);
	}
}

bool BattleRoom::TryExecuteCounterattack(BattleState& battle, BattlePawn& defender, BattlePawn& attacker,
	vector<Protocol::BattleActionLog>& logs, vector<Protocol::BattleTileInfo>& tileDeltas,
	vector<const BattlePawn*>& extraChangedPawns, vector<BattlePawn*>& deathCandidates, int32 counterChainDepth)
{
	constexpr int32 kMaxCounterChainCount = 10;
	if (counterChainDepth >= kMaxCounterChainCount)
	{
		cout << "BATTLE_COUNTER_CHAIN_STOP"
			<< " reason=max_chain_count"
			<< " max_chain_count=" << kMaxCounterChainCount
			<< endl;
		return false;
	}

	const BattlePawnClassTemplate* defenderTemplate = GBattleTemplates.GetPawnClassTemplate(defender.pawnClass);
	if (defenderTemplate == nullptr || defenderTemplate->counterSkillSlot <= 0)
		return false;

	const BattleSkillTemplate* counterSkill = GBattleTemplates.GetSkillByActionSlot(defender.pawnClass, defenderTemplate->counterSkillSlot);
	if (counterSkill == nullptr || counterSkill->skillCategory != "CAST" || counterSkill->combatType != "MELEE" ||
		counterSkill->targetType != "ENEMY_SINGLE" || IsAlive(defender) == false || IsAlive(attacker) == false ||
		_spatialService.AxialDistance(defender.axial, attacker.axial) != 1)
	{
		return false;
	}

	cout << "BATTLE_COUNTER_BEGIN"
		<< " depth=" << counterChainDepth
		<< " counter_caster_pawn_id=" << defender.pawnId
		<< " counter_target_pawn_id=" << attacker.pawnId
		<< " skill_slot=" << defenderTemplate->counterSkillSlot
		<< endl;

	BattleSkillActionRequest request;
	request.skill = counterSkill;
	request.casterTemplate = defenderTemplate;
	request.caster = &defender;
	request.target = &attacker;
	request.skillSlot = defenderTemplate->counterSkillSlot;
	request.isBackAttack = _spatialService.IsBackAttack(defender, attacker);
	request.isCounter = true;
	request.actionType = "counter";
	request.targetAxial = &attacker.axial;
	request.shouldEvadeTarget = [this](const BattlePawn& attacker, BattlePawn& target, const BattleSkillTemplate& skill)
		{
			return RollEvade(attacker, target, skill);
		};
	request.shouldCriticalTarget = [this](const BattlePawn& attacker, const BattleSkillTemplate& skill)
		{
			return RollCritical(attacker, skill);
		};
	request.findAlivePawnAt = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return FindAlivePawnAt(battle, axial);
		};
	request.findAdjacentAliveAlly = [this, &battle](const BattlePawn& source, uint64 excludedPawnId)
		{
			return FindAdjacentAliveAlly(battle, source, excludedPawnId);
		};
	request.findAlliedPawns = [this, &battle](const BattlePawn& source)
		{
			return FindAlliedPawns(battle, source);
		};
	request.tryPushTarget = [this, &battle](BattlePawn& attacker, BattlePawn& target)
		{
			return ResolvePush(battle, attacker, target);
		};
	request.getBaseTileType = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return GetBaseTileType(battle, axial);
		};
	request.getTileOverlayType = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return GetTileOverlayType(battle, axial);
		};
	request.setTileOverlayType = [this, &battle](const Protocol::AxialCoord& axial, Protocol::BattleTileOverlayType overlayType)
		{
			SetTileOverlayType(battle, axial, overlayType);
		};
	request.getTileEquipmentKey = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return GetTileEquipmentKey(battle, axial);
		};
	request.getTileEquipmentOwnerPawnId = [this, &battle](const Protocol::AxialCoord& axial)
		{
			return GetTileEquipmentOwnerPawnId(battle, axial);
		};
	request.setTileEquipment = [this, &battle](const Protocol::AxialCoord& axial, const string& equipmentKey, uint64 ownerPawnId)
		{
			SetTileEquipment(battle, axial, equipmentKey, ownerPawnId);
		};
	request.isTileValid = [this](const Protocol::AxialCoord& axial)
		{
			return _spatialService.IsInBounds(axial);
		};
	request.barrierIdGenerator = &_barrierIdGenerator;

	const int32 attackerHpBeforeCounter = attacker.hp;
	defender.facingDirection = _spatialService.GetFacingToward(defender.axial, attacker.axial, defender.facingDirection);
	BattleSkillActionResult result = _skillExecutionService.Execute(request);
	for (const Protocol::BattleActionLog& counterLog : result.logs)
	{
		cout << "BATTLE_COUNTER_RESULT"
			<< " expected_attacker_pawn_id=" << defender.pawnId
			<< " expected_defender_pawn_id=" << attacker.pawnId
			<< " logged_attacker_pawn_id=" << counterLog.attacker_pawn_id()
			<< " logged_defender_pawn_id=" << counterLog.defender_pawn_id()
			<< endl;
	}
	const bool counterWasEvaded = any_of(result.logs.begin(), result.logs.end(), [](const Protocol::BattleActionLog& log)
		{
			return log.is_evaded();
		});
	logs.insert(logs.end(), result.logs.begin(), result.logs.end());
	tileDeltas.insert(tileDeltas.end(), result.tileDeltas.begin(), result.tileDeltas.end());
	extraChangedPawns.push_back(&defender);
	extraChangedPawns.push_back(&attacker);
	extraChangedPawns.insert(extraChangedPawns.end(), result.extraChangedPawns.begin(), result.extraChangedPawns.end());
	deathCandidates.insert(deathCandidates.end(), result.deathCandidates.begin(), result.deathCandidates.end());

	if (IsAlive(defender) && IsAlive(attacker) && _spatialService.AxialDistance(defender.axial, attacker.axial) == 1 &&
		(counterWasEvaded || attacker.hp == attackerHpBeforeCounter))
	{
		TryExecuteCounterattack(battle, attacker, defender, logs, tileDeltas, extraChangedPawns, deathCandidates,
			counterChainDepth + 1);
	}

	return true;
}

void BattleRoom::AddActionLog(google::protobuf::RepeatedPtrField<Protocol::BattleActionLog>* logs,
	uint64 attackerPawnId, uint64 defenderPawnId, int32 skillSlot, const string& actionType,
	int32 damage, const BattlePawn& defender, bool isCounter)
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
	Protocol::BattleMoveResult result, const string& reason, const BattlePawn* pawn,
	const vector<Protocol::BattleActionLog>& logs, const vector<const BattlePawn*>& extraPawns, bool turnQueueResynced, const vector<Protocol::AxialCoord>& path)
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
	for (const Protocol::AxialCoord& step : path)
		movePkt.add_path()->CopyFrom(step);
	movePkt.set_next_turn_pawn_id(nextTurnPawnId);
	movePkt.set_result(result);
	movePkt.set_reason(reason);
	movePkt.set_can_move(pawn != nullptr ? CanMove(*pawn) : false);
	movePkt.set_battle_state_version(stateVersion);
	if (turnQueueResynced && battleIt != _battles.end())
	{
		movePkt.set_turn_queue_resynced(true);
		AppendUpcomingTurnPawnIds(battleIt->second, movePkt.mutable_upcoming_turn_pawn_ids());
	}
	unordered_set<uint64> deltaPawnIds;
	auto addPawnDelta = [this, &movePkt, &deltaPawnIds](const BattlePawn* source)
		{
			if (source != nullptr && deltaPawnIds.insert(source->pawnId).second)
				CopyBattlePawnDelta(*source, movePkt.add_pawn_deltas());
		};
	addPawnDelta(pawn);
	for (const BattlePawn* extraPawn : extraPawns)
		addPawnDelta(extraPawn);
	for (const Protocol::BattleActionLog& actionLog : logs)
		movePkt.add_logs()->CopyFrom(actionLog);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(movePkt);
	session->Send(sendBuffer);
}

void BattleRoom::SendBattleSkillResult(GameSessionRef session, bool success, uint64 battleId, uint64 casterPawnId,
	int32 skillSlot, uint64 targetPawnId, const Protocol::AxialCoord& targetAxial,
	int32 damage, int32 targetHp, int32 targetArmor, uint64 nextTurnPawnId, const string& reason,
	const BattlePawn* caster, const BattlePawn* target, const vector<Protocol::BattleActionLog>& logs,
	const vector<Protocol::BattleTileInfo>& tileDeltas, const vector<const BattlePawn*>& extraPawns, bool turnQueueResynced)
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
	skillPkt.set_can_move(caster != nullptr ? CanMove(*caster) : false);
	skillPkt.set_used_sub_action_this_turn(caster != nullptr ? caster->usedSubActionThisTurn : false);
	skillPkt.set_used_ultimate(caster != nullptr ? caster->usedUltimate : false);
	skillPkt.set_target_armor(targetArmor);
	skillPkt.set_battle_state_version(stateVersion);
	if (turnQueueResynced && battleIt != _battles.end())
	{
		skillPkt.set_turn_queue_resynced(true);
		AppendUpcomingTurnPawnIds(battleIt->second, skillPkt.mutable_upcoming_turn_pawn_ids());
	}
	unordered_set<uint64> deltaPawnIds;
	auto addPawnDelta = [this, &skillPkt, &deltaPawnIds](const BattlePawn* source)
		{
			if (source != nullptr && deltaPawnIds.insert(source->pawnId).second)
				CopyBattlePawnDelta(*source, skillPkt.add_pawn_deltas());
		};
	addPawnDelta(caster);
	addPawnDelta(target);
	for (const BattlePawn* extraPawn : extraPawns)
		addPawnDelta(extraPawn);
	for (const Protocol::BattleActionLog& log : logs)
		skillPkt.add_logs()->CopyFrom(log);
	for (const Protocol::BattleTileInfo& tileDelta : tileDeltas)
		skillPkt.add_tile_deltas()->CopyFrom(tileDelta);

	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(skillPkt);
	session->Send(sendBuffer);
}

void BattleRoom::SendBattleEndTurnResult(GameSessionRef session, bool success, uint64 battleId, uint64 pawnId,
	uint64 nextTurnPawnId, const string& reason, const BattlePawn* pawn, const BattlePawn* nextPawn,
	const vector<Protocol::BattleTileInfo>& tileDeltas, const vector<const BattlePawn*>& extraPawns,
	const vector<Protocol::BattleActionLog>& logs, bool turnQueueResynced)
{
	const BattlePawn* responsePawn = nextPawn != nullptr ? nextPawn : pawn;
	const auto battleIt = _battles.find(battleId);
	const uint64 stateVersion = battleIt != _battles.end() ? battleIt->second.stateVersion : 0;

	cout << "S_BATTLE_END_TURN"
		<< " success=" << success
		<< " battle_id=" << battleId
		<< " pawn_id=" << pawnId
		<< " next_turn_pawn_id=" << nextTurnPawnId
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
	endTurnPkt.set_can_move(responsePawn != nullptr ? CanMove(*responsePawn) : false);
	endTurnPkt.set_used_sub_action_this_turn(responsePawn != nullptr ? responsePawn->usedSubActionThisTurn : false);
	endTurnPkt.set_used_ultimate(responsePawn != nullptr ? responsePawn->usedUltimate : false);
	endTurnPkt.set_battle_state_version(stateVersion);
	if (success && battleIt != _battles.end())
	{
		const vector<uint64> upcoming = BuildUpcomingTurnPawnIds(battleIt->second);
		if (upcoming.empty() == false)
			endTurnPkt.set_entering_turn_pawn_id(upcoming.back());
		if (turnQueueResynced)
		{
			endTurnPkt.set_turn_queue_resynced(true);
			for (uint64 upcomingPawnId : upcoming)
				endTurnPkt.add_upcoming_turn_pawn_ids(upcomingPawnId);
		}
	}
	unordered_set<uint64> deltaPawnIds;
	auto addPawnDelta = [this, &endTurnPkt, &deltaPawnIds](const BattlePawn* source)
		{
			if (source != nullptr && deltaPawnIds.insert(source->pawnId).second)
				CopyBattlePawnDelta(*source, endTurnPkt.add_pawn_deltas());
		};
	addPawnDelta(pawn);
	addPawnDelta(nextPawn);
	for (const BattlePawn* extraPawn : extraPawns)
		addPawnDelta(extraPawn);
	for (const Protocol::BattleActionLog& log : logs)
		endTurnPkt.add_logs()->CopyFrom(log);
	for (const Protocol::BattleTileInfo& tileDelta : tileDeltas)
		endTurnPkt.add_tile_deltas()->CopyFrom(tileDelta);

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
