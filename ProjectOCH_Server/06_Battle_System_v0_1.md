# Battle System v0.1

Updated: 2026-07-20.

## Authority and Runtime Model

`BattleRoom` is authoritative. The Unity client sends intent, previews possible targets, and plays presentation. The server validates every action and returns the resulting battle state through `S_BATTLE_*` packets.

`Player::battlePawns` stores player-owned `Pawn` records. `BattleRoom` creates `BattlePawn` runtime snapshots for a battle. Runtime state includes HP, armor, AP, turn flags, death, facing, resources, barriers, statuses, and auras.

`BattlePawn` now owns the character-behavior boundary. It resolves class-specific target areas, while `BattleSkillResolver` evaluates generic table-driven modifiers and aura radius changes. `BattleSkillExecutionService` builds effect requests, resolves chained/area targets, executes common effects, and collects deltas. The current hierarchy is `BattlePawn -> Beige -> BeigeIce`; `BeigeIce` implements the `TRIANGLE_3` Hail-area resolver. New character-only target rules belong in their own `BattlePawn` subclass, not in `BattleRoom`.

## Coordinate Contract

Battle packets and server battle state use pure axial `(q, r)` coordinates.

BattleField_001 is Unity Point Top / Odd-R offset. At the Unity boundary:

```text
CellToAxial(col, row)
q = col - ((row - (row & 1)) / 2)
r = row

AxialToCell(q, r)
col = q + ((r - (r & 1)) / 2)
row = r
```

`BattleField_001.walkmap.json` stores Unity cell ranges for valid battle tiles. `BattleMapData` loads the JSON at server startup and converts every walkable Unity cell to axial coordinates. `BattleSpatialService::IsInBounds()` then uses this server-owned axial tile set for movement, target boundaries, displacement, and tile initialization. Spawn inputs use Unity cells and are also converted at battle-pawn creation. The Unity client must convert Tilemap cells before sending `C_BATTLE_MOVE` or `C_BATTLE_SKILL`, and convert received axial positions before placing pawns or overlays.

All distance, range, neighbor, Hail, and aura calculations use axial coordinates. Facing and back-attack left/right judgment converts axial coordinates back to Odd-R cells for visual left/right semantics.

## Turn and Movement Rules

- Start of an owner turn: AP becomes 2, movement is reset, and sub-action usage resets.
- Tanker pawns recover `floor((maxArmor - armor) / 2)`.
- Movement consumes no AP, but is unavailable after an AP 2 action.
- AP 1 actions preserve movement when the pawn has not moved. Ultimate and sub-action AP costs are also defined by `BattleSkill.csv`; a cost of 0 leaves AP unchanged.
- `C_BATTLE_END_TURN` is the action that advances the queue. At the end of a queue cycle, alive pawns are shuffled for the next cycle.
- The server validates battle id, current turn, ownership, alive state, map bounds, walkability, range, and occupancy.

## Damage, Defense, and Death

Damage order is barrier, armor, then HP. `shield_current` and `shield_max` expose armor plus active barriers for the client shield bar.

At HP 0, a pawn remains in battle state with `is_dead=true`, is removed from the turn queue, and is sent through `S_BATTLE_PAWN_DEAD`. The client should disable or play a death state rather than immediately destroy the pawn object.

Critical, evade, guard, perfect guard, and counter chains are not implemented yet.

## Battle Tile State

Each battle tile has:

- `base_tile_type`: `NORMAL` or `WATER`
- `overlay_type`: `NONE`, `ICE`, or `FIRE`

`S_ENTER_BATTLE.tiles` sends the initial battle map. `S_BATTLE_SKILL.tile_deltas` and `S_BATTLE_END_TURN.tile_deltas` send changes. Water is walkable only while its overlay is `ICE`. Prop-tile collision export is still not integrated into server walkability.

## Beige Ice Implementation

All implemented Beige Ice rules are defined in [[08_Battle_Data_Tables]]. Summary:

- Passive: COLD max 20, turn-start decay, 70/90 percent turn-end backlash.
- Ice Bolt: single-target magic damage and COLD +1.
- Ice Shield: barrier for 2 owner turns and COLD +1.
- Hail: empty tile overlay handling, water-neighbor icing, or enemy damage plus Frostbite and COLD +3.
- Storm Center: self toggle aura, turn-start enemy damage/Frostbite, and COLD +2.
- Ultimate: 3-turn backlash immunity and generic skill modifiers.
- Sub action: halves COLD and applies a non-stacking 2-turn 10 percent outgoing-damage reduction.

## Beige Fire Implementation

- Passive: HEAT maximum 20, owner-turn-start decay, and 70/90 percent turn-end backlash.
- Fireball: single-target magic damage and HEAT +1.
- Explosion: stronger center damage and weaker `RADIUS_1` outer damage, HEAT +3.
- Fire Wall: applies the FIRE overlay to a `LINE_3`, HEAT +2.
- Teleport: moves only to an empty FIRE-overlay tile in range, HEAT +4.
- Ultimate: 3-turn HEAT-backlash immunity plus 30 percent damage modifiers for Fireball, Explosion, and Fire Wall.
- Sub action: halves HEAT and applies a non-stacking 2-turn 10 percent outgoing-damage reduction.

## Client State Contract

The client must apply every `pawn_deltas` entry, not only the caster and primary target. This is required for chained shields, area Hail, aura ticks, death, statuses, barriers, resources, and auras.

Use:

- `statuses` for timed ultimate and sub-action effects.
- `auras.radius` for Storm Center presentation.
- `logs` and per-pawn deltas for multi-target results. `S_BATTLE_SKILL.damage` can be aggregate damage for a multi-target action.
- `tile_deltas` for CombatOverlay Tilemap changes.

## Known Gaps

- Unity axial/cell conversion must be verified end-to-end for the actual battle client before trusting range preview or aura visuals.
- No server-authoritative prop collision data yet.
- The active PvP development roster (Beige Ice, Suen Axe, Zillian Longbow, and Alen Spear) uses battle skill data. Compatibility fallback skills remain only for classes whose design rows have not been authored.
- Barrier-break Frostbite and several generic trigger types are tabled but not fully executed.
- Critical, evade, guard, perfect guard, counter, and battle AI remain future work.
