# Battle System v0.1

Updated on 2026-07-06.

## Server Authoritative Direction

The server owns battle judgment. The Unity client should use input, preview, animation, and UI state only as presentation. Final movement, skill, AP, armor, HP, turn, and failure reasons come from `BattleRoom`.

## Implemented State

`BattleRoom::BattlePawnState` now tracks:

- `currentAp`
- `hasMovedThisTurn`
- `usedSubActionThisTurn`
- `usedUltimate`
- `hp`
- `armor`
- `maxArmor`
- `facingDirection`
- `role`

`BattleState` owns `currentTurnPawnId`.

Battle pawn role is represented by `BattlePawnRole`:

- `BATTLE_PAWN_ROLE_TANKER`
- `BATTLE_PAWN_ROLE_MELEE`
- `BATTLE_PAWN_ROLE_RANGED`

`BattlePawnInfo.is_shield_unit` and `BattlePawnInfo.is_melee` were removed. Field numbers 14 and 15 are reserved and must not be reused.

`BattlePawnDelta.role` was removed because role is treated as initial pawn identity data. Field number 10 is reserved and must not be reused.

## Owned Pawn Model

`Player` now owns persistent battle pawn data through `Player::battlePawns`.

`Pawn` is long-lived player-owned data:

- `ownerId`
- `pawnId`
- `pawnClass`
- `level`

`BattlePawnState` remains battle-runtime state. AP, current HP, current armor, turn movement flags, Ultimate usage, death, and facing direction are copied into and managed inside the battle room for a specific battle.

Temporary player creation grants two default owned pawns:

- odd player ids: `PAWN_CLASS_SUEN_AXE_SWORD`, `PAWN_CLASS_BEIGE_FIRE`
- even player ids: `PAWN_CLASS_ZILLIAN_LONGBOW`, `PAWN_CLASS_ALEN_SPEAR`

Battle creation now converts each player's owned `Pawn` list into `BattlePawnState` snapshots. Class base stats and role are still temporary server-side values in `BattleRoom::TryGetPawnTemplate()` and should move to a data table later.

## Turn Start

`StartTurn()` applies the v0.1 turn-start rules:

- AP becomes 2.
- Movement availability is reset.
- SubAction usage for the turn is reset.
- Tanker-role pawns recover `floor((max_armor - armor) / 2)` armor.

`C_BATTLE_END_TURN` is the only packet that advances the turn. On success it advances to the next allied pawn and calls `StartTurn()` for that pawn.

## Movement

`C_BATTLE_MOVE` validates:

- battle exists
- pawn exists
- requester owns the pawn
- pawn is the current turn pawn
- pawn can still move
- target is walkable
- target is in range
- target is not occupied

Movement does not consume AP. A successful move sets `hasMovedThisTurn = true` and does not advance the turn.

If AP 2 has already been spent, movement fails with `BATTLE_MOVE_RESULT_CANNOT_MOVE`.

On successful movement, the server updates `facingDirection` from the move start and target:

- target `q` greater than start `q`: `BATTLE_FACING_DIRECTION_RIGHT`
- target `q` less than start `q`: `BATTLE_FACING_DIRECTION_LEFT`
- same `q`: keep the previous facing

## Skills

Temporary v0.1 fallback skill specs:

- slot 1: AP 1, damage 25, range 1
- slot 2: AP 2, damage 35, range 3
- slot 3: AP 2, damage 45, range 2
- slot 4: AP 2, damage 30, range 4
- slot 5: Ultimate, AP 0, damage 80, range 3, once per battle per pawn

`TryGetSkillSpec()` now looks up skills by `PawnClass + skill_slot`. If a pawn class has no dedicated temporary table yet, it falls back to the generic values above.

Temporary class skill specs currently implemented:

| PawnClass | Slot 1 | Slot 2 | Slot 3 | Slot 4 | Ultimate |
| --- | --- | --- | --- | --- | --- |
| `PAWN_CLASS_SUEN_AXE_SWORD` | AP 1 / dmg 30 / range 1 | AP 2 / dmg 45 / range 1 | AP 2 / dmg 35 / range 1 | AP 2 / dmg 55 / range 1 | AP 0 / dmg 90 / range 1 |
| `PAWN_CLASS_BEIGE_FIRE` | AP 1 / dmg 20 / range 3 | AP 2 / dmg 40 / range 3 | AP 2 / dmg 30 / range 4 | AP 2 / dmg 50 / range 3 | AP 0 / dmg 85 / range 4 |
| `PAWN_CLASS_ZILLIAN_LONGBOW` | AP 1 / dmg 20 / range 4 | AP 2 / dmg 35 / range 5 | AP 2 / dmg 45 / range 4 | AP 2 / dmg 30 / range 6 | AP 0 / dmg 80 / range 6 |
| `PAWN_CLASS_ALEN_SPEAR` | AP 1 / dmg 25 / range 2 | AP 2 / dmg 35 / range 2 | AP 2 / dmg 45 / range 2 | AP 2 / dmg 30 / range 3 | AP 0 / dmg 80 / range 2 |

Skill validation checks current turn, ownership, valid slot, Ultimate one-time use, enough AP, valid target, target axial match, target alive, and range.

Skill success does not advance the turn. AP 2 skills make movement unavailable afterwards. AP 1 skills leave movement availability intact if the pawn has not already moved.

Damage is applied to armor first, then HP.

Back attack judgment is server authoritative. The server compares attacker position, defender position, and defender `facingDirection`; the result is sent as `BattleActionLog.is_back_attack`.

## Response Fields

`S_BATTLE_MOVE`, `S_BATTLE_SKILL`, and `S_BATTLE_END_TURN` now include battle-state feedback needed by the client:

- `remaining_ap`
- `can_move`
- `pawn_deltas`
- `logs`

`S_BATTLE_SKILL` also includes:

- `target_armor`
- `used_sub_action_this_turn`
- `used_ultimate`

`BattlePawnInfo` and `BattlePawnDelta` include `facing_direction`, so `S_ENTER_BATTLE`, `S_BATTLE_MOVE`, `S_BATTLE_SKILL`, and other delta-bearing battle responses can drive client sprite direction from server state.

## Still TODO

- `C_BATTLE_SUB_ACTION` / `S_BATTLE_SUB_ACTION`
- critical hit judgment
- evade, guard, perfect guard
- melee-only counter chains
- max counter-chain depth
- real skill-data table instead of temporary hardcoded specs

## Death Handling

Updated on 2026-07-05.

When a skill reduces a pawn to HP 0:

- the server sets the pawn inactive with `isDead = true`
- `current_ap` becomes 0
- movement is blocked
- skill usage is blocked
- the pawn is ignored by occupancy checks
- the pawn is removed/skipped from the turn queue
- battle clients receive `S_BATTLE_PAWN_DEAD`

The pawn remains in server battle state instead of being removed. This keeps logs, replay, target references, and client animation timing stable. The client should usually disable or death-state the pawn object after the death packet rather than immediately destroying it.

## Battle Result And Field Return

Updated on 2026-07-06.

When all pawns owned by one PvP player are dead:

- the server marks the battle finished
- the turn queue is cleared
- both clients receive `S_BATTLE_RESULT`
- `S_BATTLE_RESULT.victory` is set from the receiver's perspective
- `S_BATTLE_RESULT` only sends `battle_id` and `victory`
- winner/loser ids are kept in server state and logs

Client result UI should send `C_BATTLE_RESULT_ACK` after the player presses OK.

When the server receives `C_BATTLE_RESULT_ACK`:

- that player is removed from the battle owner lookup
- that player is inserted back into the field `Room`
- after field room state is updated, the server replies with `S_BATTLE_RESULT_ACK`
- `success=true` means the client can close battle UI and start field return flow
- normal field packets (`S_ENTER_GAME` / `S_SPAWN`) follow from the field `Room`
- when both PvP players ack, the battle state is removed

## Verification

Build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Server.sln /t:GameServer /p:Configuration=Debug /p:Platform=x64 /m:1
```

Result:

```text
warning 0
error 0
```
