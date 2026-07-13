# Battle Data Tables

Updated on 2026-07-10.

## Overview

Battle data is split by ownership:

- `Data/PawnKey.csv`: short design key to proto `PawnClass`
- `Data/BattlePawnTemplate.csv`: pawn role and base stats
- `Data/BattleSkill.csv`: server-authoritative skill identity and numeric judgment data
- `Data/BattleSkillEffect.csv`: one-to-many skill effects
- `Data/BattleSkillPresentation.csv`: client presentation data keyed by `SkillKey`

The server should load `PawnKey`, `BattlePawnTemplate`, `BattleSkill`, and `BattleSkillEffect`.

The client should load `BattleSkill` for UI preview/tooltips and `BattleSkillPresentation` for display, animation, VFX, SFX, and icons.

## Skill Table

`BattleSkill.csv` keeps one row per skill slot or non-slot ability.

- `SkillType`: `PASSIVE`, `ACTIVE`, `ULTIMATE`, `SUB_ACTION`
- `TargetType`: `SELF`, `ENEMY_SINGLE`, `ALLY_SINGLE`, `ENEMY_AREA`, `TILE`, `TILE_OR_ENEMY`, `SELF_TOGGLE`
- `SkillSlot`: passive and sub-action use `0`; active skills use `1-4`; ultimate uses `5`
- `ScalingStat`: `NONE`, `BASE_STR`, `BASE_CON`, `BASE_DEX`, `BASE_INT`, `BASE_DEFENSE`, `BASE_FOCUS`, `BASE_WILL`
- `BaseValue` and `Coefficient`: default formula is `final_value = BaseValue + selected_stat * Coefficient`
- `EffectGroupKey`: links to zero or more rows in `BattleSkillEffect.csv`

## Effect Table

`BattleSkillEffect.csv` allows one skill to express multiple server effects without cramming structured logic into a single CSV cell.

- `EffectGroupKey`: joins to `BattleSkill.csv`
- `EffectOrder`: deterministic execution order inside the group
- `EffectKey`: server effect handler key
- `Trigger`: when the effect is evaluated
- `Target`: target scope for that effect
- `ParamKey` / `ParamValue`: one parameter per row

Some effect handlers need multiple rows with the same `EffectKey`; the server loader can group rows by `EffectGroupKey + EffectOrder + EffectKey` if a richer parameter map is needed later.

## Beige Ice Notes

Beige Ice currently uses cold stack mechanics:

- max cold stack: 20
- cold decays by 1 at turn start
- turn-end backlash at 70% and 90% cold thresholds
- active skills add cold stacks to the caster
- ultimate ignores cold backlash and empowers all active skills for 3 turns

The exact formulas for backlash damage, barrier value, frostbite behavior, ice tile movement cost, and empowered skill details still need server effect handlers.
