# Battle Data Tables

Updated: 2026-07-20.

## Loaded Server Data

`BattleTemplateManager` loads these files at GameServer startup. A failed load stops startup through `ASSERT_CRASH`.

| File | Purpose |
| --- | --- |
| `ClassKey.csv` | Design class key to protobuf `PawnClass` mapping. |
| `PawnTemplate.csv` | Role and base stats. Runtime max HP and armor derive from these stats in `BattleRoom`. |
| `BattleSkill.csv` | Skill identity, action slot, AP cost, range, target type, and effect group. |
| `BattleSkillEffect.csv` | Ordered effect instances and triggers for an effect group. |
| `BattleSkillEffectParam.csv` | One parameter per effect instance. |
| `EnumDef.csv` | Human-readable enum/value catalog for data authoring. It is not loaded by the C++ runtime. |

The client should mirror the battle skill/effect data it needs for tooltip and range preview, but the server remains authoritative for all results.

## Battle Map JSON

`Data\Maps\BattleField_001.walkmap.json` is loaded directly by `BattleMapData`, separately from `BattleTemplateManager`. Its `walkable_ranges` define the valid Unity cells of the battle board; the server converts them to axial coordinates and uses the resulting tile set as the boundary for movement, targeting, displacement, and battle-tile initialization.

## Table Relationships

```text
ClassKey -> PawnTemplate
             |
             +-> BattleSkill (ClassKey, ActionSlot, EffectGroupKey)
                       |
                       +-> BattleSkillEffect (EffectGroupKey, EffectInstanceKey)
                                      |
                                      +-> BattleSkillEffectParam
```

## Skill Slots

| ActionSlot | Meaning |
| --- | --- |
| 1 | Passive |
| 2-5 | Skill1 through Skill4 |
| 6 | Ultimate |
| 7 | Sub action |
| 8 | System Move action; not a `BattleSkill.csv` row |

`BattleSkill.csv` uses `SkillCategory` values such as `PASSIVE`, `CAST`, and `TOGGLE`. It does not encode presentation assets.

`TargetShape` is an optional character-resolved area policy. `RequiredOverlayType` is an optional target-tile prerequisite, used by Beige Fire Teleport to require `FIRE`.

## Effect Data Contract

The table defines what happens. Code implements the generic primitive represented by `EffectKey`.

Implemented primitives include:

- `SET_RESOURCE_MAX`
- `MODIFY_RESOURCE`
- `DEAL_DAMAGE`
- `APPLY_BARRIER`
- `APPLY_STATUS`
- `APPLY_STAT_MODIFIER`
- `TOGGLE_AURA`
- `CHANGE_TILE_TYPE` (runtime meaning: change overlay)
- `ADD_SKILL_MODIFIER` (persistent modifier definition evaluated while its required status is active)
- `TOGGLE_STANCE` (mutually exclusive persistent stance statuses)
- `DASH` (bounded straight-line forced movement before a subsequent effect)
- `APPLY_TAUNT` (timed forced-target status for adjacent enemies)
- `CONVERT_STAT_RATIO` (timed source-stat to target-stat conversion)

`APPLY_STAT_MODIFIER` uses a timed status as its runtime marker. For `DAMAGE_DEALT`, `ADD_RATIO=-0.1` means 10 percent lower outgoing damage. `stack_policy=REFRESH` preserves one stack and refreshes duration on recast.

`ADD_SKILL_MODIFIER` uses these parameters:

| ModifierType | Parameters | Current behavior |
| --- | --- | --- |
| `DAMAGE_MULTIPLIER` | `multiplier` | Multiplies the matching skill's damage. |
| `EXTRA_TARGET_COUNT` | `extra_targets` | Adds adjacent allies for a matching barrier skill. |
| `TARGET_SHAPE_OVERRIDE` | `shape` | Uses a server-resolved area such as `TRIANGLE_3`. |
| `AURA_RADIUS_DELTA` | `radius_delta` | Changes an active or newly toggled aura radius. |

`required_status_key` gates a modifier. This lets the ultimate define modifiers without putting Beige-specific effect keys in the engine.

## Beige Ice Current Data

`BEIGE_ICE` is the first full data-driven class. Its current rows define:

- COLD maximum: 20.
- COLD decay: -1 at owner turn start.
- Backlash: 90 percent threshold = 20 damage; 70 percent threshold = 10 damage; high priority suppresses low when both match.
- Ice Bolt: `10 + SPELL * 1.0`, COLD +1.
- Ice Shield: `20 + SPELL * 0.8` barrier for 2 owner turns, COLD +1.
- Hail hit: `10 + SPELL * 0.8`, COLD +3; empty-tile overlay behavior is defined by tile filters.
- Storm Center: radius 1, `8 + SPELL * 0.6` at owner turn start, Frostbite +1, COLD +2.
- Ultimate: 3-turn immunity and empowerment status. It adds damage multiplier 2, one shield target, `TRIANGLE_3`, and aura radius +1 modifiers.
- Thawing Potion: halves COLD and applies `DAMAGE_DEALT ADD_RATIO -0.1` for 2 owner turns with `REFRESH` stack policy.

The current PvP development roster is also data-driven for its original single-target damage behavior:

- `SUEN_AXE`: five cast skills with the legacy AP costs, ranges, and 30/45/35/55/90 damage values.
- `ZILLIAN_LONGBOW`: five cast skills with the legacy AP costs, ranges, and 20/35/45/30/80 damage values.
- `ALEN_SPEAR`: five cast skills with the legacy AP costs, ranges, and 25/35/45/30/80 damage values.
- `ALEN_SHIELD`: Carbas Will and Morale Boost reuse Alen's shared effect groups. Its remaining rows define sword damage, stance toggle, intercept/swap/temporary armor, dash/push/taunt, and a marked-target Duel Master modifier.

`BEIGE_FIRE` uses HEAT as its resource and introduces two character-resolved target shapes: `RADIUS_1` for Explosion and `LINE_3` for Fire Wall. Its Fire Wall applies the `FIRE` overlay, and Teleport uses the reusable `TELEPORT_TO_OVERLAY` primitive with an `EMPTY_TILE` target and `RequiredOverlayType=FIRE`.

## Data Versus Code Boundary

Data must contain values, target selection intent, duration, conditions, and modifier type. Code must contain reusable rules that interpret those values: hex shape calculation, adjacent-pawn selection, damage application, barrier handling, status lifetime, and packet output.

Do not add per-character effect keys when an existing primitive plus parameters can express the rule. Add a new primitive only when it is reusable by more than one planned skill or removes meaningful engine complexity.
