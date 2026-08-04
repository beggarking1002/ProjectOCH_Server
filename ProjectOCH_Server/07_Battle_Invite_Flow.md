# PvP Battle Invite and Return Flow

Updated: 2026-07-20.

## Invite Flow

1. The initiating client shows its local Yes/No confirmation.
2. Yes sends `C_BATTLE_INVITE(target_player_id)`.
3. The field `Room` validates both players and sends `S_BATTLE_INVITE_REQUEST` to the requester and `S_BATTLE_INVITE_RECEIVED` to the target.
4. The target sends `C_BATTLE_INVITE_RESPONSE(requester_player_id, accept)`.
5. A rejection sends `S_BATTLE_INVITE_RESULT(accepted=false)` and leaves both players in the field.
6. An acceptance sends `S_BATTLE_INVITE_RESULT(accepted=true)` followed by `S_BATTLE_CLASS_SELECTION_START` to both players; they remain in the field while choosing classes.
7. Each client sends `C_BATTLE_CLASS_SELECTION` with exactly one class from Suen, Beige, Alen, and Zillian. The server validates the selection and reports progress through `S_BATTLE_CLASS_SELECTION_RESULT`.
8. When both valid selections arrive, the server creates four selected pawns for each player, despawns both field objects, and queues PvP entry on `BattleRoom`.
9. `BattleRoom::HandleEnterPvpBattle` creates one shared battle state and sends each player a personalized `S_ENTER_BATTLE`.

The field `Room` owns pending invites because it owns field-player membership and sessions. Battle-room entry is queued through `DoAsync`, so room ownership changes happen in the target room's job context.

## Battle Pawn Source

PvP battle pawns are snapshots of `Player::battlePawns`; they are not the field player object and do not persist battle-only HP, action-use, resource, or status state after the battle.

`ObjectUtils.cpp` no longer assigns a fixed roster by player ID. On each accepted PvP invite, the server replaces each participant's temporary `battlePawns` list with the four classes selected for that battle:

| Character | Choices |
| --- | --- |
| Suen | `SUEN_AXE_SWORD`, `SUEN_PARVIS` |
| Beige | `BEIGE_ICE`, `BEIGE_FIRE` |
| Alen | `ALEN_SPEAR`, `ALEN_SWORD_SHIELD` |
| Zillian | `ZILLIAN_LONGBOW`, `ZILLIAN_MACE` |

Solo test enemies are currently spawned as `BEIGE_ICE` in `BattleRoom`. The battle implementation itself is designed to consume each player's actual `battlePawns` list.

## Turn and Room Behavior

- Both participants share one `battle_id` and one server-authoritative turn queue.
- At the beginning of each full alive-pawn cycle, the queue is shuffled again.
- `S_ENTER_BATTLE` is personalized: each recipient receives its own pawns as allied and the other side as enemies.
- Each action response is sent to both battle participants, enabling both clients to update turn, pawn, and tile state.

## Battle End and Field Return

When one side has no live pawns, the server sends `S_BATTLE_RESULT` to both participants. Each client presents its own result UI. Pressing OK sends `C_BATTLE_RESULT_ACK`.

The acknowledgement is independent per player:

1. Server validates that player's result acknowledgement.
2. Server queues field-room entry for that player.
3. Field room restores the player and completes its enter work.
4. Server sends `S_BATTLE_RESULT_ACK` after the field-return operation is complete.

The player must wait for `S_BATTLE_RESULT_ACK` before the Unity client considers field return finalized. The battle room is released only after both participants have completed their acknowledgement/return path.

## Client States

- Local confirmation: before sending an invite.
- Waiting: successful `S_BATTLE_INVITE_REQUEST`.
- Received invite: `S_BATTLE_INVITE_RECEIVED`.
- Declined: `S_BATTLE_INVITE_RESULT(accepted=false)`.
- Class selection: accepted result, then `S_BATTLE_CLASS_SELECTION_START`; submit four choices with `C_BATTLE_CLASS_SELECTION` and display the waiting state from `S_BATTLE_CLASS_SELECTION_RESULT`.
- Transitioning: a successful final selection result, field `S_DESPAWN`, then `S_ENTER_BATTLE`.
- Result UI: `S_BATTLE_RESULT`.
- Field restored: `S_BATTLE_RESULT_ACK`.
