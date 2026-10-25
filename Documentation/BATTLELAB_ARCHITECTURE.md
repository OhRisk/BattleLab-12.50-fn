# Battle Lab Architecture (Fortnite 12.50, CL-13137020)

Status: **Milestone 1, pass 2: pak contents decoded.** Tools: `Tools/pak.py` (pak v10 reader),
`Tools/uasset.py` (cooked-package → JSON). Decoded assets: `Tools/json/`. Exe string evidence:
`Tools/strings_pass*.txt`.

## 0. Build identity

| Source | Value |
|---|---|
| Exe | `++Fortnite+Release-12.50-CL-13137020` (code section encrypted on disk, see KNOWN_ISSUES) |
| `.egstore` manifest | `++Fortnite+Release-12.50-CL-13137020-Windows` |
| Paks | 27 × pak v10, all main-key encrypted index, 959,764 files, **no pak-level compression** |

v12.50 = Chapter 2 Season 2 (not 12.41). Folder names reflect the real version.

## 1. The playlist: `/Game/Athena/Playlists/BattleLab/Playlist_BattleLab` (FortPlaylistAthena)

| Area | Value |
|---|---|
| Identity | PlaylistId 6635, PlaylistStatId 35, UIDisplayName "Battle Lab" |
| Description | "Jump into your own Battle Royale island and set the rules! Select from favorite LTM loot pools, set up combat scenarios with the new Bot Grenades, and create your own mode by setting options like gravity and fall damage. Fill your game with up to 15 friends on your own island, or matchmake with default options in a public Battle Lab." |
| Game type | `EFortGameType::Creative`, `bEnableCreativeMode`, `bUseCreativeStarterIsland`, JoinInProgressMatchType `Playground` |
| Tags | `Athena.Playground`, `Athena.BattleLab`, `Plugin.Playlist.BattleLab` |
| Map | Apollo (C2 island): `MapManagerClass=UIMapManagerApollo`, heatmap minimap `M_MiniMapApollo_Heatmap` |
| Extra levels | `/Game/Athena/Apollo/Maps/BattleLab/BattleLab_Apollo_PlayerSpawns`, `…_Portals`, `…_SaveVolume` |
| Warmup / Bus | `bSkipWarmup`, `bSkipAircraft`: **no Battle Bus** |
| Players / teams | 1–16 players, MaxTeamCount 16, team switching allowed, `DefaultLastTeam 19` |
| Respawn | `RespawnType=InfiniteRespawnExceptStorm`, `DBNOType=NotWhenRespawning`, respawn heights from `AthenaCompositeGD_Creative` rows `Playground.RespawnHeight/MaxRespawnHeight` |
| Win condition | `LastManStandingIncludingAllies` (with infinite respawn it effectively never ends; matches the historical sandbox) |
| Storm | `StormEffectDelay 3240` s (54 min) |
| Supply drops | `bUseDefaultSupplyDrops=false` (the SupplyDrop loot lists exist; spawning is knob/device-driven) |
| Loot tiers | `AthenaCompositeLTD_BattleLab` = `Playground/AthenaLootTierData_Client` + `BattleLab/OverrideLootTierData` |
| Loot packages | `AthenaCompositeLP_BattleLab` = `Playground/AthenaLootPackages_Client` + `BattleLab/OverrideLootPackagesDataBackup` |
| Game data | `AthenaCompositeGD_BattleLab` = `AthenaGameData` + `Playground/OverrideGameData` + `BattleLab/OverrideGameData` |
| Resources | `BattleLab/AthenaResourceRates` |
| User options | `BattleLab_UserKnobs` (PlaylistUserOptions: the configurable rules) |
| Mission gen | `MissionGen_BattleLab_C` |
| Modifiers | `Playground/ResourceCapModifier`, `Creative/Creative_UserKnobsModifierv3`, `BattleLab/BattleLabModifiers`, `BigTeamMode_Glider/BigTeamGliderModifier`, `GameplayModifiers/CreativeSpawnModifier`, `Deimos/GameplayMod_DeimosAI` |
| AI | `AISettings=/Game/Athena/Playlists/Deimos/Athena_AI_Settings` |
| HUD | `PerformanceHUD` (primary), `CenterBattleLabWidget` (upper centre), `AthenaHumansAndBotsLeftWidget`; hides team kills / players left |
| Portals | `ConditionalAssetGroup=Conditional_BR_Portal_Group` |

## 2. Chain (confirmed where marked ✔)

```
Frontend: UFortServerBrowserBattleLab ("CREATE A BATTLELAB")              exe ✔ (widget BP: to trace)
   ▼  matchmaking ticket → session (BATTLELABSESSIONOWNERID)               exe ✔ / LocalServer ✔ (scripted)
Playlist_BattleLab                                                         pak ✔
   ▼  GameType Creative, no warmup/bus, infinite respawn
Apollo map + BattleLab_Apollo_{PlayerSpawns,Portals,SaveVolume}           pak ✔ (paths)
   ▼  modifiers + BattleLab_UserKnobs + MissionGen_BattleLab
Loot: AthenaCompositeLTD/LP_BattleLab                                      pak ✔
Bot Grenades: WID_…FrenchYedoc_JW(Un)friendly → … → SpawnBot               pak ✔ (BOT_GRENADE_SYSTEM.md)
AI: Athena_AI_Settings, GameplayMod_DeimosAI, FortAthenaAIBotController     pak ✔ / exe ✔
Island save: SaveVolume + creative profile (last_used_battlelab_file)      partial
```

## 3. Subsystems

| System | Evidence | Status |
|---|---|---|
| Bot Grenades | full asset chain, see `BOT_GRENADE_SYSTEM.md` | decoded; loot weight fix staged as hotfix |
| AI bots | `AFortAthenaAIBotController` + ~30 evaluators (exe); FrenchYedoc BT/skills (pak) | needs server + nav mesh |
| Vehicles | exe: `AFortAthenaVehicleSpawner`, `"Vehicles - Choppa"`, Antelope/Jackal/Octopus classes | spawners on map: to trace |
| Respawn | playlist `InfiniteRespawnExceptStorm` | native, server-side |
| Battle Lab devices | exe: `ABattleLabDeviceGameplayActor(_VendingMachine)`, `BattleLabTeleporterManager`; pak: `GCN_BattleLab_{BotSpawner,DeviceSpawner,PlateTrigger,SpeedBoost}` | to trace |

## 4. Open items

- `BattleLab_UserKnobs` option list (storm, gravity, fall damage, loot pool…) → `BATTLELAB_CONFIGURATION.md`.
- Which vehicle spawners are placed on the 12.50 Apollo map.
- Nav mesh presence in the Apollo world packages.
- Frontend widget path for the Battle Lab tile / server browser.
