# Alen Shield Implementation

Updated: 2026-08-01.

## Ownership

`BattlePawn -> Alen -> AlenShield` owns the class-only decisions: whether the current stance enables a counterattack and whether Responsibility is active for interception. `BattleRoom` remains class-agnostic and only asks the pawn hooks.

## Data-driven skills

`ALEN_SHIELD` has slots 1 through 7 in `Data\BattleSkill.csv`. Carbas Will and Morale Boost share `ALEN_CARBAS_WILL_EFFECTS` and `ALEN_MORALE_BOOST_EFFECTS` with `ALEN_SPEAR`; class-specific rows only provide action-slot lookup and presentation identity.

## Shared server primitives

- `TOGGLE_STANCE`: toggles the persistent Carbas and Royal statuses. Royal Guard writes a `DAMAGE_TAKEN ×0.7` status modifier.
- `APPLY_STAT_MODIFIER` with `DEFENSE / ADD_FLAT`: adds temporary armor and expires it with the status.
- `DASH`: uses `BattleDisplacementService` to move toward an empty tile or to the tile before a targeted enemy. On Pride's successful push, the generic push primitive advances Alen into the enemy's original tile.
- `APPLY_TAUNT`: applies an owner-turn status to adjacent enemies. Server target validation rolls the stored chance and rejects another enemy target when the roll requires the Alen target.
- `CONVERT_STAT_RATIO`: applies a timed stat bonus and removes it on status expiration.

## Verification

`GameServer Debug|x64` build: warning 0, error 0.

The default even-id test player roster now includes `PAWN_CLASS_ALEN_SWORD_SHIELD` in place of Alen Spear, keeping the default PvP test at two pawns per side.

## Client requirements

Apply every returned pawn delta. In particular, render the stance, Responsibility, taunt, and Duel Master status keys; reposition the caster after Pride's dash; and use action logs/deltas for the automatic Carbas counterattack.
