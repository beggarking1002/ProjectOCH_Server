# Implementation Status - 2026-07-20

## Completed Since the Initial Battle Prototype

- PvP battle invitation, accept/decline, shared battle room entry, field pawn despawn, result packets, per-player result acknowledgement, and field return.
- Randomized turn queue per full cycle.
- Server-authoritative action-use, movement availability, ownership, current-turn, target, range, occupancy, and ally-target validation.
- Battle pawn runtime model with armor, barriers, resources, statuses, auras, facing, role, and death state.
- Armor/barrier/HP damage ordering, tank armor recovery, death notification, and battle end resolution.
- Pure axial battle coordinates with Unity Point Top / Odd-R conversion helpers.
- Base terrain plus dynamic overlay state, ICE overlay synchronization, and water-on-ice walkability.
- Data loading and validation at startup through `BattleTemplateManager`.
- First full data-driven class: Beige Ice, including passive, normal skills, ultimate modifiers, and sub action.
- Generic `ADD_SKILL_MODIFIER` and timed `APPLY_STAT_MODIFIER` table paths.
- Migrated the active PvP development roster's legacy Suen Axe, Zillian Longbow, and Alen Spear damage skills into the CSV/effect pipeline while preserving their ranges and damage values.
- Extracted character-specific battle behavior from `BattleRoom`: `BattlePawn` subclasses now resolve class-only target areas, and `BattleSkillResolver` owns generic skill modifier/aura rules.
- Added the current character hierarchy: `BattlePawn -> Beige -> BeigeIce`.
- Extracted common skill execution from `BattleRoom` into `BattleSkillExecutionService`; `BattleRoom` now retains validation, turn/action-use handling, death resolution, and packet delivery.
- Implemented Beige Fire's HEAT passive, Fireball, Explosion, Fire Wall, FIRE-overlay teleport, ultimate damage/backlash modifiers, and Cooling Potion through the character behavior and data/effect pipeline.

## Current Important Files

- `GameServer/BattleRoom.*`: battle lifecycle, validation, turn flow, packet assembly, and target-resolution helpers.
- `GameServer/BattleEffectExecutor.*`: generic effect execution and timed status/barrier lifetime handling.
- `GameServer/BattleTemplateManager.*`: CSV parsing, cross-table validation, and Unity cell-to-axial map conversion.
- `GameServer/BattleCoordinate.h`: Point Top / Odd-R conversion boundary.
- `Data/*.csv`: current battle design data.

## Protocol Snapshot

Battle protocol now includes:

- `C_BATTLE_MOVE` / `S_BATTLE_MOVE`
- `C_BATTLE_SKILL` / `S_BATTLE_SKILL`
- `C_BATTLE_END_TURN` / `S_BATTLE_END_TURN`
- battle invitation packets 1020 through 1024
- `S_BATTLE_PAWN_DEAD` = 1025
- `S_BATTLE_RESULT` = 1026
- `C_BATTLE_RESULT_ACK` = 1027
- `S_BATTLE_RESULT_ACK` = 1028

`BattlePawnInfo` and `BattlePawnDelta` include resources, barriers, statuses, auras, shield totals, facing, action-use/movement state, ultimate/sub-action flags, and death state. `S_ENTER_BATTLE` sends all initial tile states; subsequent battle packets send deltas.

## Client Integration State

The server requires the Unity client to:

1. Convert Odd-R Tilemap cells to axial before battle movement and skill packets.
2. Convert received axial positions back to cells before pawn and CombatOverlay Tilemap placement.
3. Apply every `pawn_deltas` entry, all `tile_deltas`, `statuses`, `resources`, `barriers`, and `auras`.
4. Treat the server response as final for multi-target action results, death, and action availability.

The reported Storm Center range mismatch remains an integration verification item until the production Unity path confirms the same axial/cell conversion at both packet boundaries.

## Next Recommended Work

1. Verify Odd-R conversion with two Unity clients and a six-neighbor Storm Center test.
2. Export prop collision data and include it in server `IsBattleWalkable` validation.
3. Author data-driven skills for the remaining classes that still use compatibility fallback specs (currently Beige Fire and un-authored future roster classes).
4. Implement reusable target resolvers for more area shapes and explicit target-selection policy.
5. Implement critical, evade, guard, perfect guard, and bounded melee counter chains.
6. Add focused battle simulation tests for effects, durations, turn queue, and coordinate conversion.

## Verification

Latest server code build verification:

```text
GameServer Debug: warning 0, error 0
```
