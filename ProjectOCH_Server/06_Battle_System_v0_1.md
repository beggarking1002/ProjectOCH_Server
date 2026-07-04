# Battle System v0.1

Updated on 2026-07-04.

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
- `isShieldUnit`
- `isMelee`

`BattleState` owns `currentTurnPawnId`.

## Turn Start

`StartTurn()` applies the v0.1 turn-start rules:

- AP becomes 2.
- Movement availability is reset.
- SubAction usage for the turn is reset.
- Shield units recover `floor((max_armor - armor) / 2)` armor.

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

## Skills

Temporary v0.1 skill specs:

- slot 1: AP 1, damage 25, range 1
- slot 2: AP 2, damage 35, range 3
- slot 3: AP 2, damage 45, range 2
- slot 4: AP 2, damage 30, range 4
- slot 5: Ultimate, AP 0, damage 80, range 3, once per battle per pawn

Skill validation checks current turn, ownership, valid slot, Ultimate one-time use, enough AP, valid target, target axial match, target alive, and range.

Skill success does not advance the turn. AP 2 skills make movement unavailable afterwards. AP 1 skills leave movement availability intact if the pawn has not already moved.

Damage is applied to armor first, then HP.

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

## Still TODO

- `C_BATTLE_SUB_ACTION` / `S_BATTLE_SUB_ACTION`
- critical hit judgment
- evade, guard, perfect guard
- melee-only counter chains
- max counter-chain depth
- real skill-data table instead of temporary hardcoded specs

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
