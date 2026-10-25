# Vehicle System (Battle Lab, 12.50)

Placements counted with `Tools/find_classes.py` over every level under `Athena/Apollo/` and
`Plugins/GameFeatures/` (exports that are instances of each spawner class). Values from
`AthenaCompositeGD_BattleLab` (curve tables via `Tools/uasset.py`).

**Caveat on counts:** many POIs exist as alternate level variants (e.g. `Apollo_POI_Agency`, `_01`, `_FT_a`,
`_FT_b`), of which only one loads per match. Counts are placements in files, not vehicles per match.

## Codenames

| Codename | Vehicle | Spawner class | Placements in files |
|---|---|---|---|
| **Hoagie** | Choppa | `Hoagie/Apollo_Hoagie_Spawner` | 6 |
| Hoagie (spy base) | Choppa at faction bases | `Hoagie/Apollo_Hoagie_Spawner_SpyBase` | 6 |
| **Meatball** | Motorboat | `DrivableVehicles/Meatball/Athena_Meatball_L_Spawner` | 71 (+8 Infiltration-LTM variant) |
| Cart | Shopping cart | `Athena_CartSpawner` | 12 |
| GolfCart | Golf cart | `Athena_GolfCartSpawner` | 9 |
| Quad | Quadcrasher | `Athena_QuadSpawner` | 4 |
| Jackal | (Jackal vehicle; `JackalVehicle_Athena_C`) | `Athena_JackalSpawner` | 5 |

Whether the Cart/Quad/Jackal sublevels actually load in the 12.50 island rotation is unverified.

## Choppas

All Choppa spawners are **level overlays inside the `Hoagie` Game Feature plugin**
(`Plugins/GameFeatures/Hoagie/Content/LevelOverlays/`), not in the base island. The plugin must be active for
Choppas to appear.

| Overlay | Spawner |
|---|---|
| `Apollo_Hoagie_Garage`, `_Helipad_a`, `_Holly` (Holly Hedges), `_Lake` (Lazy Lake), `_PleasantSoccer_b` (Pleasant Park), `_WeatherStation` | regular |
| `Apollo_Hoagie_MB_Agency` (The Agency), `_MB_Helipad_a`, `_MB_Helipad_b`, `_OilRig` (The Rig), `_SJI_a` (The Shark), `_Yacht` (The Yacht) | spy base |

| Tuning (`Default.Hoagie.*`, level 0–1) | Value |
|---|---|
| Spawn chance (`Default.Choppa` / `Default.SpyBaseChoppa` Min/MaxSpawnPercent) | **100% / 100%** |
| Rotor damage: player / building inner / building outer / self inner / self outer | 50 / 300 / 15 / 125 / 10 |
| Explosion: player / vehicle / building, radius | 70 / 400 / 450, 1280 |
| Rotor hit launches players at | 1500 |
| Body hit: min speed to launch, damage curve | 20; 10 → 60 → 100 by speed |
| Weak-point multiplier | 2.25 |
| Exit launch velocity multiplier | 0.65 |
| Misty Meadows "Bop" damage multiplier vs Choppas | 2.0 |

## Motorboat (Meatball)

Launches players (speed ≥ 20, ×15 impulse), 200 building damage on hit (speed ≥ 10), damage on landing,
pickaxe impulse 1000 (Z ×1.1). Missile launcher turn 100→1200 over 3 s, missile launch ×1500 (min Z 500),
and missiles can launch boats.

## Other spawners on the island (non-vehicle, for reference)

`B_Athena_SuperDingo_Spawner` ×24 (all in `Apollo_DroneSpawners`), `BP_MANG_Spawner` ×11 (henchmen; see
below), `Apollo_Ghost_Spawner` ×1 (`POI/Apollo_POI_Camp_001`), propane tanks ×18, apple/mushroom/slurp-shroom
spawners, buried chests ×4, soccer balls ×5.

### Henchmen (`Athena/AI/MANG/BP_MANG_Spawner`)

Placed in `…_MANG` level variants of: MountainBase (Grotto) ×2, OilRig2 (Rig), SJI (Shark), Yacht, HDP, and
outposts (abandoned house, remote house, radio station, gas station, Pleasant soccer). These are the C2S2
faction guards, relevant to the planned faction sandbox. Their spawn rules were not decoded.
