# Battle Invite Flow

Updated on 2026-07-06.

## Goal

Players can start a PvP battle from the field:

1. Player 1 clicks Player 2 in the field.
2. Client shows a local Yes/No confirmation.
3. Yes sends `C_BATTLE_INVITE`.
4. Server sends waiting/received packets.
5. Player 2 accepts or declines with `C_BATTLE_INVITE_RESPONSE`.
6. Decline notifies Player 1.
7. Accept removes both field pawns and sends both players into one `BattleRoom`.

## Packets

New messages were appended after the existing battle packets, so existing packet IDs are preserved.

- `C_BATTLE_INVITE = 1020`
  - `target_player_id`
- `S_BATTLE_INVITE_REQUEST = 1021`
  - `success`
  - `requester_player_id`
  - `target_player_id`
  - `reason`
- `S_BATTLE_INVITE_RECEIVED = 1022`
  - `requester_player_id`
- `C_BATTLE_INVITE_RESPONSE = 1023`
  - `requester_player_id`
  - `accept`
- `S_BATTLE_INVITE_RESULT = 1024`
  - `accepted`
  - `requester_player_id`
  - `target_player_id`
  - `reason`

## Server Flow

`ServerPacketHandler.cpp` routes:

- `C_BATTLE_INVITE` to `Room::HandleBattleInvite`
- `C_BATTLE_INVITE_RESPONSE` to `Room::HandleBattleInviteResponse`

`Room` owns pending battle invites because it has the active field players and their sessions.

`Room::HandleBattleInvite` validates:

- requester is in the field
- target id is valid
- requester is not already waiting
- requester does not already have an incoming invite
- target exists in the field
- target has a live session
- target does not already have a pending invite

On success:

- requester receives `S_BATTLE_INVITE_REQUEST(success=true)`
- target receives `S_BATTLE_INVITE_RECEIVED`

On decline:

- requester receives `S_BATTLE_INVITE_RESULT(accepted=false, reason="declined")`
- target also receives the same result so the UI can close deterministically

On accept:

- both players receive `S_BATTLE_INVITE_RESULT(accepted=true)`
- both field pawns are removed from `Room`
- `S_DESPAWN` with both object ids is sent to both players and remaining field players
- `BattleRoom::HandleEnterPvpBattle` creates a shared PvP battle
- both players receive `S_ENTER_BATTLE`

## BattleRoom PvP

PvP battle uses one shared `battle_id`.

The requester pawns are stored in `alliedPawns`; the target pawns are stored in `enemyPawns`. `S_ENTER_BATTLE` is personalized:

- requester sees requester pawns as `allied_pawns`
- target sees target pawns as `allied_pawns`

Current temporary PvP pawn setup is based on each player's owned `Player::battlePawns`.

- odd player ids receive:
  - `PAWN_CLASS_SUEN_AXE_SWORD`
  - `PAWN_CLASS_BEIGE_FIRE`
- even player ids receive:
  - `PAWN_CLASS_ZILLIAN_LONGBOW`
  - `PAWN_CLASS_ALEN_SPEAR`

When PvP starts, requester-owned pawns are converted into `alliedPawns`, and target-owned pawns are converted into `enemyPawns`. These are battle-runtime snapshots; AP, HP, armor, movement flags, Ultimate usage, death, and facing are managed in `BattlePawnState`.

## Turn Queue

`BattleRoom` now has a random turn queue.

- Solo test battles queue only allied pawns.
- PvP battles queue both players' pawns.
- At battle creation, alive pawn ids are collected and shuffled.
- `C_BATTLE_END_TURN` advances through the queue.
- When a cycle ends, the queue is rebuilt and shuffled again.

## Client Notes

Client-side UI should map these server packets to states:

- local click confirmation: no packet until Yes
- waiting: after `S_BATTLE_INVITE_REQUEST(success=true)`
- received invite: after `S_BATTLE_INVITE_RECEIVED`
- declined: after `S_BATTLE_INVITE_RESULT(accepted=false)`
- accepted/transition: after `S_BATTLE_INVITE_RESULT(accepted=true)` followed by `S_DESPAWN` and `S_ENTER_BATTLE`

The generated Unity C# packet files were copied by the GameServer pre-build.

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
