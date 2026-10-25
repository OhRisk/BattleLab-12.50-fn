# Bot Grenade initialization trace (Fortnite 12.50 host)

This file traces one Bot Grenade bot from spawn to combat on the 12.50 local host. For each stage it gives what the game does natively and what we observed, and marks the stages the host had to repair. Addresses are offsets into `FortniteClient-Win64-Shipping.exe`. Observations come from the `[BotAI]` diagnostic (`BattleLabBotDebug.h`) and the headless self-test (`Native\bin\bot-selftest.flag`).

## Spawn

| Stage | Native path | Observed |
|---|---|---|
| Grenade | `B_Athena_FrenchYedoc_JWFriendly/JWUnfriendly` (a BuildingGameplayActor) calls `GameMode.GetServerBotManager` then `FortServerBotManagerAthena.SpawnBot(BD_FrenchYedoc_JW)`. The Friendly variant then calls `FortAthenaAIBotController.SwitchTeam(thrower's team)`. | OK |
| Pawn | `BP_Pawn_FrenchYedoc_C`, walking (`MovementMode=1`) | OK |
| Controller | `BP_FrenchYedocPlayerController_C` (FortAthenaAIBotController), possessing the pawn (`Pawn.Controller == Controller`) | OK |
| Player state | `FortPlayerStateAthena`. The team comes from the bot manager: team 2, squad 50 (`CustomSquadId`). Team 2 has no FortTeamInfo. | OK (team 2 is the native choice) |
| Cosmetics | The bot data uses `OverrideCosmeticMode = CosmeticLibrary`, but that library's DataTables are server-only. | Fixed: random CID from the paks (`BattleLabBotSkins.h`) |
| Inventory | Pickaxe + `WID_Assault_Auto_Athena_C_Ore_T02` + ammo. The AR is equipped and holds 30 rounds. | OK |

## Brain

| Stage | Native path | Observed |
|---|---|---|
| Behavior tree | `BT_FrenchYedoc`: MoveTo beacon (ReachBeacon evaluator), otherwise RunBehavior `BT_Phoebe_Gameplay` | Running (`BTComponent`) |
| Blackboard | `BB_FrenchYedoc`, a child of `BB_Phoebe` | Initialized |
| Skill sets | 16 `DigestedBotSkillSets` from `BotSkillSetClasses` | OK |
| Evaluator managers | Loot and ReachBeacon cache the server bot manager through a getter that identical code folding turned into a null stub | Fixed earlier (call-site redirect, Test023) |
| Game phase gate | `BT_Phoebe_Gameplay` root decorator: `AIEvaluator_Global_GamePhase >= Aircraft`. The controller copies `GameState.GamePhase` at spawn (+0x167bee0) and on every change (+0x167c5f0). | Battle Lab skips warmup and aircraft, so a live match is in SafeZones. A headless host without players stays in Setup (the self-test sets SafeZones on its own bots only). |
| Landed gate | Every on-ground branch needs `AIEvaluator_Global_HasEverJumpedFromBusAndLandedKey`. Only the bus landing path (+0x166d2a0) and a debug spawner (+0x16844e0) set it. | Fixed: set after spawn (`MarkBotLanded`) |

## Navigation

| Stage | Observed |
|---|---|
| Nav data | The client paks have no Battle Royale navmesh. `BattleLabNav.h` builds `AthenaNavMesh-Phoebe` at runtime (10–12 s). |
| Navigation octree | Created by NavSys vtable +0x328 with fixed root bounds (center 0, extent 64000); the island spans ±130000. Populated from the whole island, 36602 of 36946 elements landed in nodes that do not contain them, so tile geometry queries missed most geometry: navmesh on a lattice of strips only (none at Dirty Docks). Fixed: the octree is kept flat (MinLeafExtent = root extent). |
| Static meshes | `UStaticMeshComponent::IsNavigationRelevant` (+0x40fc6e0) requires the mesh's `NavCollision`, a server-only subobject missing from the client paks, so no static mesh (POI walls, cliffs, props) reached the octree and paths ran through walls. `RegisterNavOctreeElement` (+0x48a3470) asks through `INavRelevantInterface` +0x48 (adjustor thunk to +0x40fc6e0); the cached `bNavigationRelevant` (+0x86 mask 4) comes from the primary vtable +0x3e0. Fixed: both entries of the 15 static-mesh component classes accept `bHasNavigationData` + `BodySetup` (exactly when the cooker builds NavCollision) and otherwise run the game's own code; 90265 octree elements. |
| Coverage | Whole island (58902/65536 tiles at 1024 uu; 8/8 sample loot spots). |
| Path test | `FindPathToLocationSynchronously` from the bot to +8 m: valid on open ground. |
| Patrol destinations | `GetRandomLocationSafeToReach` (+0x1673940, mode = controller +0x351 `ReachLocationValidationMode` = 1) → bot manager +0x1a1a1e0: a random point from the bot mutator's `BasePlacementQuery` results (native array at mutator +0x3f0) inside the phase's safe zone; otherwise the safe zone centre, which is (0,0,0) for every Battle Lab phase. The host spawns a native `FortAthenaMutator_Bots` without Blueprint defaults. Fixed: spawned deferred with `BP_Phoebe_Mutator`'s `BasePlacementQuery` (`EQS_Phoebe_BaseNavigation`, a soft pointer) and 0.5 s retry interval. The mutator's BeginPlay runner (+0x1ae9fa0) retries until the navmesh is ready. Result: 3699 points across the island. |

## Perception and targeting

| Stage | Native path | Observed |
|---|---|---|
| Senses | `FortAIPerceptionSystem` in the match world has Sight, Damage and AthenaHearing. Pawns register as sight sources natively (`bAutoRegisterNewPawnsAsSources=false` for Sight). | OK |
| Listener team | `AISense_Sight` caches the listener's team when the controller's `AIPerceptionComponent` registers, which happens before the bot has a team. Native `SwitchTeam` (+0x168a110) refreshes it with `RequestStimuliListenerUpdate`. Bots that keep their spawn team never get the refresh. | Fixed: refreshed once after spawn |
| Attitude | The bot controller's `GetTeamAttitudeTowards` (+0x16761b0) returns Friendly for any other bot while the server bot manager's `bBotHostileToHumanPlayersOnly` (+0x1d8, native default true) is set. Fortnite clears it for team modes (`BP_Phoebe_Mutator_DM`). | Fixed: cleared for Battle Lab |
| Result | Self-test pair (team 2 vs a bot moved to team 3): each perceives the other within 3–10 s, alert level 3 (Threatened), target set, line of sight true | OK |

## Combat

| Stage | Observed |
|---|---|
| Range attack decision | `FortAthenaAIBotEvaluator_RangeAttack` (vtable +0x270 evaluate → +0x186a680): current threat entry (controller +0x458 into the threat list +0x470, 0xc8 per entry, type byte +0x50), a ranged weapon (+0x1670110) and a gate (+0x1678050) that needs a ranged weapon with ammo in `SlotItems`. |
| `SlotItems` | `AFortAthenaAIBotController::SlotItems[6]` (+0x360), `FFortBotInventoryInfo` { ItemDefinition +0, FortItem +8 (vtable +0x378 loaded ammo, +0x380 total ammo, +0x3e8 definition), native byte +0x10 (0x70), weapon category +0x11 }. Constructed empty (+0x165cc00) and reset in setup (+0x1679450); never written by the client executable. Fixed: `SyncBotSlotItems` fills them from the inventory with the game's slot preference (+0x1685010) and weapon category (+0x26565a0: pickaxe 14, AR 4). |
| `AIEvaluator_Global_DesiredWeapon` | Set to the AR once `SlotItems` is filled |
| Result | Range attack Allowed (4); bursts, reloads, damage, kills (headless bot vs bot) |

## Friendly marker

The Friendly grenade assigns the team after `SpawnBot` returns (`SwitchTeam`), and native `SwitchTeam` adds the player state to the new team's FortTeamInfo but keeps squad 50. The host now forces an immediate net update of the bot's player state, its team info and its pawn after `SwitchTeam`, and logs `SwitchTeam <bot>: team a -> b, squad x -> y`. Whether the client's white arrow keys on squad or team is still unverified.

## Enum values (from the tree's decorator constants)

- `EExecutionStatus`: 0 = Error, 1 = Denied, 2 = Success, 3 = Pending, 4 = Allowed
- `EAlertLevel`: 0 = Unaware, 1 = Alerted, 2 = LKP, 3 = Threatened
- `EAthenaGamePhase`: 1 = Setup, 3 = Aircraft, 4 = SafeZones
- `ETeamAttitude`: 0 = Friendly, 1 = Neutral, 2 = Hostile
