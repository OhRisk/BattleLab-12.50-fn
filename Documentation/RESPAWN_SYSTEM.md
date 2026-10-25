# Respawn System (Battle Lab, 12.50)

Sources: `Playlist_BattleLab` (FortPlaylistAthena), `AthenaCompositeGD_BattleLab`, and
`AthenaCompositeGD_Creative` (curve tables decoded by `Tools/uasset.py`).

## Rules

| Setting | Value | Source |
|---|---|---|
| Respawn type | `EAthenaRespawnType::InfiniteRespawnExceptStorm`: unlimited respawns, except after dying in the storm | playlist |
| Knocked state | `EDBNOType::NotWhenRespawning`: no DBNO while respawning is active | playlist |
| Respawn delay | **5 s** (`Default.RespawnTime`) | BattleLab GD |
| Camera fade | `MinTimeBeforeRespawnCameraFade 1.0` s | playlist |
| Respawn height | `Playground.RespawnHeight` **15000**, `Playground.MaxRespawnHeight` **22575** | Creative GD (referenced by the playlist) |
| Level-stream fade threshold | `PlaygroundIslandData` row `Default.MaxSafeTeleportDistance` | playlist |
| Default respawn geometry | direction deviation 360°, distance from centre 0.8–1.0, min 2500 above ground, min height 7500, trace height 18000 | BattleLab GD |
| Win condition | `LastManStandingIncludingAllies` (never met with infinite respawn) | playlist |
| Storm | playlist `StormEffectDelay 3240` s (54 min); no user option | playlist |
| Drop items on elimination | user option (default On) | `PlaygroundMutator_ItemDropOverride` |
| Starting health / shields | user options (default Full / Empty) | `PlaygroundMutator_HeathAndShield` |

## Flow

```
eliminated → 1 s camera fade → 5 s respawn timer → redeploy in the air at ~15000 (≤22575)
→ skydive / glide back in → inventory per "Drop Items When Eliminated"
Dying to the storm is the only exception ("ExceptStorm"), and the storm arrives only after 54 minutes.
```

Bot Grenade bots do **not** respawn (`BD_FrenchYedoc_JW.bOverrideCanRespawnOnDeath`).

## Bots

`BattleLab/OverrideGameData` changes exactly one row, `Default.Phoebe.Enable = 1`, which turns on the C2 bot
system ("Phoebe") that Bot Grenade bots run on. `Default.Phoebe.EnableDynamicDifficulty = 1` is inherited.
