# Battle Lab Configuration (12.50): the original user options

Source: `/Game/Athena/Playlists/BattleLab/BattleLab_UserKnobs` (`PlaylistUserOptions`, referenced by
`Playlist_BattleLab.UserOptions`). Decoded with `Tools/uasset.py`; JSON in
`Tools/json/Athena/Playlists/BattleLab/UserKnobs/` and `Tools/json/Athena/Playlists/Playground/UserKnobs/Knobs/`.

These are the only settings Battle Lab exposed. Each option writes `OptionKey` into its `OptionScope`
mutator blueprint, which applies it in the match. Defaults: `DefaultValueIndex` where the asset sets
it, otherwise index 0 (UE omits default-valued properties when cooking).

| # | Option (UI) | Key | Values (UI name → value) | Default | Applied by |
|---|---|---|---|---|---|
| 1 | Spawn Location | `SpawnPortalIndex` | Dirty Docks 21 · Holly Hedges 22 · Frenzy Farm 23 · Misty Meadows 24 · Craggy Cliffs 25 | Dirty Docks | `BattleLab/Mutators/BattleLabMutator_Base` |
| 2 | Loot | `LootSelection` | Default 0 · High Explosives 1 · Close Encounters 2 · Solid Gold 3 · Wild West 4 · Sniper Shootout 5 (plus one empty entry in the list) | Default | `BattleLab/Mutators/BattleLabMutator_LootSelection` |
| 3 | Experimental | `bUsingExperimentalTables` | On / Off. "Determines whether the game should use the currently experimental gameplay options." | **On** | `BattleLabMutator_LootSelection` |
| 4 | Shields | `StartingShield` | Empty 0 · Half 50 · Full 100 | Empty | `Playground/Mutators/PlaygroundMutator_HeathAndShield` |
| 5 | Health | `StartingHealth` | 1 · Half 50 · Full 100 | Full (index 2) | `PlaygroundMutator_HeathAndShield` |
| 6 | Drop Items When Eliminated | `DropAllItemsOverride` | On (ForceDrop 1) · Off (ForceKeep 2) | On | `PlaygroundMutator_ItemDropOverride` |
| 7 | Fall Damage | `FallDamageMultiplier` | On 1.0 · Off 0.0 | On | `PlaygroundMutator_FallDamage` |
| 8 | Gravity | `GravityOverride` | Earth 0 · Moon 1 · Asteroid 2 | Earth | `PlaygroundMutator_Gravity` |
| 9 | Time of Day | `TimeOfDayOverride` | Day Cycle 0 · Day 1 · Night 2 | Day Cycle | `PlaygroundMutator_TimeOfDay` |
| 10 | Player Names & Location | `DisplayMode` | Team Only 0 · Always Show 1 · Always Hide 2 | Team Only | `PlaygroundMutator_NamePlates` |

## Notes

- **No storm option exists.** The storm follows the playlist's fixed `StormEffectDelay` (3240 s = 54 min).
- **Spawn locations** are five portals, matching the extra level `BattleLab_Apollo_Portals`. The values
  21–25 are portal indices.
- **Loot pools 1–5** are the "favorite LTM loot pools" named in the playlist description.
- **Experimental ↔ Bot Grenades (hypothesis, unverified):** "Experimental" defaults to On and belongs to the
  loot-selection mutator. Epic's Bot Grenade weights (2.0) live in
  `BattleLab/LootOverrides/OverrideLootPackagesDataBackup_Exp` (`_Exp` = experimental). That fits
  Experimental swapping in the `_Exp` tables, which would mean Bot Grenades dropped whenever it was On.
  Confirming this needs the `BattleLabMutator_LootSelection` blueprint logic, which `uasset.py` does not
  decode yet (class bodies/bytecode).
