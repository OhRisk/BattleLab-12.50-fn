# Bot Grenade AI progress

This log records each iteration on the Battle Lab Bot Grenade AI: observation, hypothesis, change, result. The traced chain is in `BOT_INITIALIZATION_TRACE.md`. Tests 001–005 were run headlessly with the dev self-test (`Native\bin\bot-selftest.flag`). The self-test spawns two `BD_FrenchYedoc_JW` bots through the same `SpawnBot` UFunction the grenade uses, sets their game phase to SafeZones, and moves the second bot to team 3 with `SwitchTeam`.

## Test 000 (live, user): bots only look around; all bots share one skin
- Skin: `OverrideCosmeticMode = CosmeticLibrary`, but the library tables are server-only. Fixed with a random CID pool.
- AI: the host log showed a built navmesh and a running BT on every bot. `BT_Phoebe_Gameplay` gates all ground behavior on `HasEverJumpedFromBusAndLanded`. Fixed: `MarkBotLanded`.
- Result (live): random skins work, bots still idle, and the friendly arrow only appears after the player respawns.

## Test 001: diagnostics
- Added `[BotAI]` state dumps: possession, blackboard, path test, perception, team, weapon.
- Observation: possessed, BT and blackboard OK, path valid, every evaluator status 0, blackboard GamePhase = 1.
- Cause found: the root decorator of `BT_Phoebe_Gameplay` is "In Gameplay? GamePhase >= Aircraft". A headless host with no players stays in Setup. A live Battle Lab match (bSkipWarmup/bSkipAircraft) is in SafeZones (confirmed from the client log).

## Test 002: phase SafeZones (self-test only)
- Result: the gameplay tree runs and the evaluators answer (1 = Denied: patrol, POI, loot, range, melee), but there is no perception between enemies.

## Test 003: perception
- Ruled out: `Mutator_AISupport` (spawned manually, no change); squad IDs; manual sight registration (pawns are registered natively).
- Sight works when it may detect every affiliation, so the cause is attitude.
- `IGenericTeamAgentInterface` (slot 3 GetGenericTeamId, slot 4 GetTeamAttitudeTowards): team ids are correct, the pawn reports Hostile, the bot **controller reports Friendly**.
- Cause: `bBotHostileToHumanPlayersOnly` on the server bot manager (default true) makes bots friendly to all other bots.
- Change: clear it for Battle Lab, as Fortnite's own `BP_Phoebe_Mutator_DM` does.

## Test 004: listener team
- The controller attitude is now Hostile, but sight still did not fire until `RequestStimuliListenerUpdate`. The sight listener caches its team when the controller registers, before the bot has one.
- Native `SwitchTeam` refreshes it. Change: refresh once after `SpawnBot` for bots that keep their spawn team.
- Result: both bots perceive each other, alert level 3 (Threatened), target set, line of sight true.

## Test 005: combat
- Range and melee evaluators stay Denied and DesiredWeapon stays empty. No condition failures are logged (all bot AI log categories are already at Error or above), so this is a decision.
- Suspect: the headless match is WaitingToStart / Setup. This needs a live test.

## Test 006 (live, user): Friendly + Enemy grenades with a player
- Also added: a `SwitchTeam` hook that forces a net update of the bot's player state, team info and pawn right after the team change, and logs `SwitchTeam <bot>: team a -> b, squad x -> y`.
- Result: nothing visibly changed (bots stand, turn, do not shoot; arrow only after respawn).
- Log: team logic is right. Friendly bots move to team 3 (the player's team, squad stays 50) with attitude Friendly (0) to the player; Enemy bots move to a new team 18 with attitude Hostile (2) to the player and to friendly bots. Every bot perceives, reaches alert 3, sets a target, has line of sight. But `path+8m=INVALID` at Dirty Docks, and range attack stays Denied with `desiredWeapon=none`.

## Test 007: navmesh coverage
- A 32x32 probe grid showed navmesh on only 265 of 1024 cells; a whole-island tile map (1024 uu) showed an exact lattice aligned to powers of two (16-tile cells, ±65536 centre, radial stripes): the navigation octree's node layout.
- Ruled out: landscape (all 64 streaming proxies loaded, 246 collision components, bUsedForNavigation), gathering mode (Lazy vs Instant: identical), unfinished build (`IsNavigationBeingBuilt` false, a second build does nothing).
- Octree walk (`FNavigationOctree` at NavSys+0x298, UE 4.25 TOctree layout): 36602 of 36946 elements sit in nodes that do not contain them (root extent 64000, island ±130000); a tile's geometry query descends by position and misses them.
- Change: keep the octree flat (MinLeafExtent = root extent via a per-instance vtable hook of CreateNavOctree, vtable +0x328), so each query tests every element.
- Result: navmesh on the whole island (58902 of 65536 tiles, all sample loot spots), build 10–12 s at startup. Bots now move: patrol Allowed, move status "moving", positions change, melee on blocking props.

## Test 008: combat
- Range attack decision (+0x186a680) requires a ranged weapon with ammo in the controller's `SlotItems` (+0x360, 6 x `FFortBotInventoryInfo`); `weaponCache=[]` on every bot.
- No code in the client executable writes `SlotItems` (all addressing forms searched; the owner-interface inventory handler +0x16776d0 does not either): server-only inventory handling not compiled into the client.
- Change: `SyncBotSlotItems` fills the slots from the bot's inventory using the game's own slot preference (+0x1685010) and weapon category (+0x26565a0); refreshed every 2 s (no-op when unchanged).
- Result (headless, two opposing bots): desired weapon = AR, range attack Allowed (4), bursts of fire (30 → 23 → 18 → 9 → 4 → 1), reloads, chase over ~70 m, damage applied (GAS Damage executions), one bot killed the other.

## Test 009 (live, user): combat against the player, friendly marker
- Five Friendly bots (team 2 → 3) and five Enemy bots (team 2 → 18). User: "they work".
- Log: every bot reaches alert 3 with line of sight and range attack Allowed (4). Friendly bots target the enemy bots. Enemy bots target the friendly bots and the player (the player pawn appears in the enemy bots' perception 33 times). Ammo drains and refills, so the bots fire and reload. Loot and POI stay Denied (1) and no beacon is set.
- Friendly marker: the `Channel opened for` diagnostic logged nothing, so the arrow timing is still unmeasured.
- Open questions: `patrolDest` reads (0,0,-2392) on every bot. This could be a diagnostic misread or a real patrol target, so it needs checking.

## Test 010 (live, user): bots gather in the water under the dock at the map centre
- The stuck bot sat at (0,0,-2400), which is its patrol destination. Every bot patrolled to the world origin.
- Chain: patrol evaluator `FortAthenaAIBotEvaluator_PatrolAround` → `GetRandomLocationSafeToReach` (+0x1673940). The controller's `ReachLocationValidationMode` (+0x351) is 1, so the bot manager picks from the bot mutator's placement results (+0x1a1a1e0 → +0x1a17000, the native array at mutator +0x3f0, filtered by the phase's safe zone) and falls back to `GameMode.SafeZoneLocations[phase]`.
- Safe zone: Battle Lab's own zone data centres every phase at (0,0,0) (`PickSafeZoneLocation`: RejectOuterDistance 9999999 → "Centering"). That is native and correct; the fallback is the only thing that was wrong.
- Placement results were empty. The host (Project Reboot `SpawnBotMutator`) spawns a bare native `FortAthenaMutator_Bots`, which has no `BasePlacementQuery`. Fortnite's own bot mutator `BP_Phoebe_Mutator` sets `BasePlacementQuery = EQS_Phoebe_BaseNavigation` (a 120000 grid around the world origin, 3000 apart, projected to the navmesh) and `BaseQueryingAttemptIntervalTimeSeconds = 0.5`.
- Change: spawn the mutator deferred and set those two values before BeginPlay. `BasePlacementQuery` is a `TSoftObjectPtr`: a raw pointer there makes the resolve (+0x7c9210) fail silently. The mutator's BeginPlay query runner (+0x1ae9fa0) retries on a timer until the navmesh is ready, then runs the query; the completion callback (+0x1afc980) fills the array.
- Result (headless): query runs 2 s after the navmesh build, 3699 patrol locations, and bots walk from point to point across the island ((3024,33000) → (6048,45000) → (12000,33000) → …), fighting when they meet.

## Test 011 (live, user, open): bots stand still again but still fight
- Host log 20:13: 3699 patrol locations. The bots cluster near (15500,112500,-2995) with valid patrol destinations about 75 m away (e.g. (9000,105000), (15000,105000)), yet move=0 (idle), and one bot fell back to (0,0). The destination is fine now, but the move toward it fails from that area. Next: run a path test from that spot to the destination and look for a navmesh gap or an unconnected area around there.

- Update (live, user, west of Dirty Docks): the bots now scatter across the map, but one bot keeps jumping into a large dark wall (container or building side). Hypothesis: that wall's collision is missing from the runtime navmesh, so the path runs straight through it, and the jumping is the game's unstuck behaviour (`BT_Phoebe_UnstuckNew`). Next: path test at that spot, then check whether the wall's static mesh has navigation relevance / collision in the navigation octree.

## Test 012 (live, user, full match): late friendly arrow, bots jumping into walls, idle bots
- User: all three still seen in a full match (2026-09-27 evening host, 481 bots over the session).
- Host log analysis (stuck episode = 20 s or more within 4 m): 743 episodes, the worst several hours long. Pattern: move status Moving, movement mode alternating Walking / Falling (the jump loop), patrol destination 20–70 m away behind a wall. Many bots were pressed against the same wall planes (x = 91958, y = 103734, y = -85713).

## Test 013 (headless probe): the walls are not in the navmesh
- Probe (`BattleLabNavProbe.h`, `Native\bin\nav-probe.flag`): for twelve live stuck spots, follow the navmesh path the bot takes and trace each segment. Every path ran straight through a `FortStaticMeshActor` (Dirty Docks and Steamy Stacks water walls, power transformers, cave pieces, cliffs). None of them had an element in the navigation octree (36946 elements, none containing the hit point).
- Ruled out: the level (loaded, visible, listed in `World.Levels`), `AthenaNavSystem::AddLevelToOctree` (+0x16fd9c0; its `NavOctTreeInclusionBounds` filter is off, bit 0 of +0x1908), the static-navigation flag (+0x82bfb24 = 0).
- Cause: `UStaticMeshComponent::IsNavigationRelevant` (+0x40fc6e0, primary vtable +0x3e0, and through `INavRelevantInterface` +0x48 by an adjustor thunk) is `StaticMesh && StaticMesh->NavCollision && UPrimitiveComponent::IsNavigationRelevant` (+0x40a7120). `NavCollision` is a server-only subobject: the client paks do not contain it (e.g. `S_Athena_WaterWall_1x2` cooks only `BodySetup_71` and `FortStaticMeshUserData`). So on this host no static mesh is ever navigation relevant. `RegisterNavOctreeElement` (+0x48a3470) asks the interface entry; the cached `bNavigationRelevant` (+0x86 mask 4) comes from the primary entry.
- Side finding: `BattleLabNav.h` had written an FBox into `NavOctTreeInclusionBounds`, which actually holds actor pointers; harmless while the filter is off, now removed.

## Test 014 (headless): static mesh navigation relevance restored
- Change (`BattleLabNav.h`, `NavRelevance`): before the map loads, the primary and interface entries of every static-mesh component class that still point to the engine's implementation (StaticMeshComponent, InstancedStaticMeshComponent, HierarchicalInstanced, Foliage, SplineMesh and others, 15 classes) get one that treats "would have NavCollision on the server" like the NavCollision itself. The cooker builds it exactly when the mesh has `bHasNavigationData` and a `BodySetup` (UStaticMesh::CreateNavCollision). Everything else is the game's own code, including `UPrimitiveComponent::IsNavigationRelevant`. Geometry export falls back to the BodySetup collision, as the engine does for meshes without NavCollision.
- Result: octree 36946 → 90265 elements, build 14–20 s. The stuck spots' paths now go around the walls (spot at the Steamy Stacks water wall detours via y ≈ 100700; the Test 011 spot (15500,112500), INVALID before, is a valid 16-point path). Remaining trace hits along paths are Fortnite building pieces (`BuildingActor`, navigation modifiers the bots open or smash natively).
- Soak test (`Native\bin\bot-soak.flag`: one Bot Grenade bot at each of the twelve live stuck spots, same team so they patrol): 0 stuck episodes, every bot walked 1.0–1.6 km in 6.5 minutes, movement almost entirely Walking.

## Test 015 (headless soak, 10 min): Steamy Stacks vortex
- With a 30 s heartbeat in the `[BotAI]` dump (so motionless bots are visible), 10 of 12 bots walked 1.1–2.5 km; the only two stuck episodes were at the Steamy Stacks cooling tower: movement mode Custom / `EFortCustomMovement::Skydiving` (4), move status Idle, bobbing in place, patrol destination on the tower (z 2984).
- The cooling tower's `BGA_Vortex_Area_Effect_CoolingTower_C` lifts pawns into skydiving; it is not a navigation modifier (`bIsNavigationRelevant` false) and no nav modifier volume covers it in the client data. `BT_FrenchYedoc` runs `BT_Phoebe_Gameplay` directly, without `BT_Phoebe`'s skydive / landing branch, so a Bot Grenade bot caught in the vortex does not steer out. Left as is (a replacement behaviour would not be the original); 2 of 481 bots in the Test 012 match.

## Test 016 (live, automated client): friendly marker
- Client diagnostic `TeamWatch.h` (read-only): after a Friendly Bot Grenade the bot's player state reaches the client already on the player's team (team 3, squad 50) within 200 ms. `B_Athena_FrenchYedoc_JWFriendly` sets `bHasCustomSquadId = false` (bytecode: EX_LetBool … EX_False), and native `SwitchTeam` → `PlaceBotOnTeam` (+0x192c0c0) keeps the bot manager's squad, so a Friendly bot is by design a non-squad teammate.
- The 3D team arrows are drawn by `STeamIndicatorLayer` (created by `AthenaIndicatorLayerBase`, +0x4ac61e1) from the layer's indicated-players cache (layer +0x418, refresh +0x26120e0, list +0x50). Non-squad teammates are shown within `FortRuntimeOptions.FadeOutTeamIndicatorsAfter` (27500 uu). The cache refreshes only on events: game state +0x16d8 / +0x16f0 (a player state filed into the team / squad buckets, from the game state's player-state handler +0x1980110), the local player state's +0xd80 / +0x1020, the local pawn +0x2fa0, and the minigame's `HandleRefreshIndicatorPlayersCache`. A respawn (new local pawn) rebuilds it, which is why the arrow appears then.
- Open: why the join of a bot teammate does not trigger a refresh on this client (`OnRep_TeamIndex` +0x1a30630 broadcasts +0xd80 only when the old team is not 0). Next: log which of those delegates fire on the client when the bot's player state arrives.

## Test 017 (live, automated client): commands
- Short commands (`friendly 3`, `friendlybot`, `fly`, `speed 3`, `flyspeed 4`, `noclip`, `walk`, `kill`) reach the server through the client's `APlayerController::ConsoleCommand` hook (+0x442aef0). `friendlybot` spawns the grenade's projectile with `FortKismetLibrary.SpawnProjectile` (native `SpawnBot` + `SwitchTeam` followed). Fly: the client did not enter flight from the server's `ClientCheatFly`, which explains the jerky flight (server corrections); the client module now starts the same engine function locally and the server accepts the client's position while flying. Flight itself could not be judged visually in the automated test (injected mouse input does not reach the game).

## Loot (live, user): faction chests empty, friendly grenades rare
- The opened chests were faction chests (`Loot_AthenaTreasure_Red/Blue/White`). The default "Playground" loot set of Battle Lab, which the cooked composites mirror, has them at weight 0. All other Battle Lab loot sets (HE, Low, Shotguns, Snipers, SolidGold, WW) enable them with identical values. Change: backend hotfix enables them with those values.
- Friendly Bot Grenade weight raised to 6.0 (Epic's value is 2.0; enemy stays 2.0) at the user's request. Both are backend hotfixes in `LocalServer/hotfixes/DefaultGame.ini`, confirmed applied on the host (29 rows).

## Commands
- Added `cheat fly`, `cheat noclip` (alias `ghost`) and `cheat walk`, which work like Unreal's cheat manager: flying movement and cheat flying on the server, plus the engine's `ClientCheatFly/Ghost/Walk` RPC to the owning client.
