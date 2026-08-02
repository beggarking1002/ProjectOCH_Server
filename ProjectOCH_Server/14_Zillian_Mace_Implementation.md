# Zillian Mace Implementation

`ZILLIAN_MACE` is registered through `ZillianMace` and uses the seven data rows in `Data/BattleSkill.csv`.

| Slot | Skill | Server behavior |
| --- | --- | --- |
| 1 | 내가 성녀라니 | Battle-start Saintly Burden applies 1.5x MORALE loss. Each successful hostile direct or area hit immediately performs the common Dizzy Stun check against the attacker, using Zillian Mace's BaseFocus and the attacker's BaseWill. |
| 2 | 기초 빠따질 | Adjacent STR-scaled physical attack. |
| 3 | 오지마! | Weak adjacent STR-scaled physical attack followed by the common Dizzy Stun check on a successful hit. |
| 4 | 눈부신 눈부심 | `RADIUS_1` enemy area; each hit target receives a 2-owner-turn, cleanseable 30 percent hit-rate reduction. |
| 5 | 원하지 않은 권능 | Self-targeted action that restores every adjacent allied pawn except the caster. |
| 6 | 아무도 죽지마! | Spends 50 percent of current HP, then grants each ally a 2-owner-turn barrier. |
| 7 | 나 돌아갈래 | Adjacent allied target restore plus cleanse. The caster and target swap only if the request's `request_optional_position_swap` is true. |

## Generic Extensions

- `ADJACENT_ALLIES`: a dedicated effect-target scope that executes the effect once for each living allied pawn at axial distance 1, excluding the caster.
- `RESTORE_ADJACENT_ALLIES`: reuses the regular HP restoration calculation within that scope.
- `OPTIONAL_SWAP_POSITION`: reuses the validated position-swap primitive only when the incoming skill request opts in.
- `C_BATTLE_SKILL.request_optional_position_swap` is the client intent flag for this opt-in. Its protobuf default is false, so older clients keep the heal/cleanse behavior without swapping.
