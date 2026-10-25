# Original Battle Lab playlist in the supplied build

Build: **12.50 CL-13137020**. These are supplied-asset findings, not claims about 12.41 or successful gameplay. The existing detailed research remains under `Documentation/`.

Primary evidence: `Tools/json/Athena/Playlists/BattleLab/Playlist_BattleLab.json`.

| Field | Confirmed value |
|---|---|
| Asset | `/Game/Athena/Playlists/BattleLab/Playlist_BattleLab.Playlist_BattleLab` |
| Class | FortPlaylistAthena |
| Identifier | Playlist_BattleLab; PlaylistId 6635; PlaylistStatId 35 |
| Game type | Creative; Battle Lab/Playground tags; JoinInProgressMatchType Playground |
| GameMode / GameState | Runtime classes UNKNOWN; not directly specified by decoded playlist properties |
| World | Apollo references confirmed; precise runtime travel URL UNKNOWN |
| Additional levels | BattleLab_Apollo_PlayerSpawns, BattleLab_Apollo_Portals, BattleLab_Apollo_SaveVolume |
| Start | SkipWarmup=true; SkipAircraft=true |
| Players | Min 1, max 16; MaxTeamCount 16; MaxTeamSize 16; MaxSquadSize 4 |
| Teams | Team switching enabled; DefaultLastTeam 19 |
| Respawn | InfiniteRespawnExceptStorm; DBNO NotWhenRespawning; 1 second minimum fade |
| Storm | StormEffectDelay 3240 seconds |
| Mission generator | MissionGen_BattleLab_C |
| Loot tiers / packages | AthenaCompositeLTD_BattleLab / AthenaCompositeLP_BattleLab |
| User options | BattleLab_UserKnobs; experimental loot defaults On in the decoded options |
| AI settings | Deimos/Athena_AI_Settings |
| Modifiers | ResourceCapModifier, Creative_UserKnobsModifierv3, BattleLabModifiers, BigTeamGliderModifier, CreativeSpawnModifier, GameplayMod_DeimosAI |
| HUD | PerformanceHUD, CenterBattleLabWidget, AthenaHumansAndBotsLeftWidget |
| Frontend metadata | Display name Battle Lab; Creative game type; Athena.BattleLab tag |

The loot-selection mutator references the original experimental tables. Their six Bot Grenade rows have weight 2.0 and count 5 for each friendly/enemy item, covering floor, chest and supply-drop lists. Normal variants have weight 0.0. The current backend's earlier hotfix changes only the pre-mutator composite; actual server selection remains untested.

Friendly item: `WID_Athena_FrenchYedoc_JWFriendly`; enemy item: `WID_Athena_FrenchYedoc_JWUnfriendly`. Original projectile/spawner references lead to `BD_FrenchYedoc_JW`, `BP_Pawn_FrenchYedoc`, `BP_FrenchYedocPlayerController`, and `BT_FrenchYedoc`. Team-switch calls exist; their precise Blueprint branch and runtime results remain unverified.

Vehicle research identifies original Hoagie (Choppa) overlay spawners and Meatball motorboat spawners. Their successful activation in a Battle Lab world remains untested. Respawn data specifies a five-second delay; repeated respawns remain untested.

See `Documentation/BATTLELAB_CONFIGURATION.md`, `LOOT_SYSTEM.md`, `BOT_GRENADE_SYSTEM.md`, `AI_SYSTEM.md`, `RESPAWN_SYSTEM.md` and `VEHICLE_SYSTEM.md` for preserved asset-level findings and caveats. A locally running gameplay server has not yet been established.
