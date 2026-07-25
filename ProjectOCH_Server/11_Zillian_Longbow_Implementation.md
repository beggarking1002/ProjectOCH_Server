# Zillian Longbow Implementation

Updated: 2026-07-25.

## Hierarchy

```text
BattlePawn -> Zillian -> ZillianLongbow
```

`ZillianLongbow` is registered through `CreateBattlePawn`. Numerical skill tuning and effect composition remain in the battle CSV tables. Its successful-hit reaction and the healing arrow's hit-check exception are implemented in `ZillianLongbow`; the class requests shared combat primitives instead of placing character logic in `BattleRoom`.

## Implemented Skills

| Slot | Skill | Server behavior |
| --- | --- | --- |
| 1 | 내가 성녀라니 | Battle-start passive. Zillian receives 1.5x morale loss. A successful hostile direct or area hit applies Dizzy +1 to the attacker; evades, DOT, and self-damage do not trigger it. |
| 2 | 기초 활질 | DEX-scaled physical ranged attack, range 1-4. |
| 3 | 조금 거친 치유법 | SPELL-scaled allied heal, range 1-4. `ZillianLongbow` marks this support skill as an arrow that uses the normal hit/evasion roll; on a miss neither heal nor BLEED applies. On hit, it applies non-stacking BLEED for 2 target turns; BLEED deals 6 direct HP damage at the target turn start. |
| 4 | 비장의 몽둥이질 | High STR-scaled adjacent melee attack. |
| 5 | 살고싶어! | SPELL-scaled self heal. |
| 6 | 퍼뜩 인나라! | Range 1-4 allied heal. Zillian sacrifices floor(current HP × 0.5), ignoring armor/barrier, and the target recovers 50% max HP. |
| 7 | 나 돌아갈래 | Adjacent allied heal and removal of all cleanseable harmful statuses. |

## Generic Status Extensions

- `APPLY_DOT`: registers a harmful, cleanseable owner-turn HP DOT.
- `CLEANSE_HARMFUL`: removes all flagged harmful statuses, including BLEED, FROSTBITE, DIZZY, STUN, and the existing Suen accuracy-down debuff.
- `SACRIFICE_HP`: directly spends current HP without consuming armor or barriers.
- `ON_HIT_RECEIVED`: runtime passive trigger for a successful hostile hit.

DOT changes are included in the next pawn delta at turn start. The client should use the `BLEED` status duration and next turn delta to present the tick.
