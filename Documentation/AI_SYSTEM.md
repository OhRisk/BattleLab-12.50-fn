# AI System (Battle Lab / Bot Grenades, 12.50)

Decoded with `Tools/uasset.py` and `Tools/find_classes.py`; config from `Tools/config/FortniteGame/Config/DefaultEngine.ini`.
The Bot Grenade chain (item → projectile → spawner → bot data) is in `BOT_GRENADE_SYSTEM.md`.

## 1. Playlist-level AI setup

| Asset | Content |
|---|---|
| `Playlist_BattleLab.AISettings` = `Deimos/Athena_AI_Settings` (`AthenaAISettings`) | `MaxFootstepHearingRange 3000`, `ReducedDeAggroRange 3500` |
| Modifier `Deimos/GameplayMod_DeimosAI` | adds mutator `GameplayMutators/Mutator_AISupport` (`bMutatorActive=true`; logic in blueprint, not decoded) |

Battle Lab ships no pre-placed bots. AI comes only from Bot Grenades (and Battle Lab devices, still to trace).

## 2. Bot Grenade bot

| Part | Value |
|---|---|
| Bot data | `BD_FrenchYedoc_JW`: pawn `BP_Pawn_FrenchYedoc`, skill level 1100, `CustomSquadId 50`, needs a UniqueNetId, no respawn |
| Controller | `BP_FrenchYedocPlayerController` (`FortAthenaAIBotController` family): AI perception (sight, hearing, damage), `FortAthenaAIBotPathFollowingComponent` |
| Loadout | Harvesting pickaxe + Common Assault Rifle (`WID_Assault_Auto_Athena_C_Ore_T02`) |
| Skill set | `AI_skill_frenchyedoc_bot_attacking` → curve `FrenchYedoc_SkillSets` row `FrenchYedoc.Skill.Attacking.EnableWTFBehavior` |
| Blackboard | `BB_FrenchYedoc` (child of `BB_Phoebe`) + 3 keys: ReachBeacon status / target / movement state |

### Behavior tree `BT_FrenchYedoc`

```
Selector   [service: AIEvaluator_ReachBeacon]
├─ "Can Reach a Beacon?" (ReachBeacon status ≥ Execution Pending)
│    └─ MoveTo Beacon (radius 30, strafe allowed, no partial paths)
│         services: UpdateTarget (0.1 s) · Sprinting evaluator (0.2 s) · Sprinting ·
│                   MeleeAttack evaluator (1 s) · ManageWeapon (1 s) · HandleFocusing
└─ RunBehavior BT_Phoebe_Gameplay          ← the standard Chapter 2 player-bot brain
```

Bot Grenade bots are **standard C2 bots ("Phoebe") with a "reach beacon" layer on top**. What places the
beacons in Battle Lab is not yet traced (candidates: Battle Lab devices; `GCN_BattleLab_BotSpawner_Spawn` exists).
C++ evaluators backing the brain are in the exe (`UFortAthenaAIBotEvaluator_*`: Attack, RangeAttack, Loot,
Heal, Revive, Storm, PatrolAround, Investigate, AvoidThreat, …).

### Teams

Battle Lab has built-in team switching: `Playlist_BattleLab` sets `bAllowTeamSwitching=true`,
`bShowTeamSelectButton=true`, `MaxTeamCount 16`, `DefaultLastTeam 19`.

Bot team assignment, from the spawner's imports (`B_Athena_FrenchYedoc_JWFriendly`; the Unfriendly variant is
the same graph with `IsFriendly=false` and adds no calls of its own):

| Call | Role |
|---|---|
| `FortServerBotManagerAthena::SpawnBot` with `BD_FrenchYedoc_JW` (`bHasCustomSquadId`, `CustomSquadId 50`) | spawns the bot |
| `FortKismetLibrary::GetActorTeam` | reads the thrower's team |
| `FortAthenaAIBotController::SwitchTeam` | moves the bot onto a team |

Most likely reading: **friendly** bots are switched to the thrower's current team at spawn, and **enemy** bots
keep the shared custom squad 50, so all enemy bots end up on one common hostile team. That is also how the
owner remembers it. The branch on `IsFriendly` is in bytecode, which is not decoded, so this is
**strongly indicated, not proven**. Whether friendly bots follow the thrower after a later team switch is unknown.
The older `B_Athena_FrenchYedoc` (with `BD_FrenchYedoc_Default`) used `FortServerBotManagerAthena::JoinTeam` instead.

### Cosmetics (build defect)

`OverrideCosmeticMode = CosmeticLibrary` → `FrenchYedoc_CosmeticLibraryDefault`, which points to five data
tables (`FrenchYedoc_Cosmetic_Characters/Gliders/Contrails/Backpack/Pickaxe_Default`). **None of the five is in
the 12.50 paks.** The bot data also has a direct fallback outfit, `CID_416_Athena_Commando_M_AssassinSuit`.
Which one the game uses with the tables missing is untested.

## 3. Navigation

| Setting (DefaultEngine.ini) | Value |
|---|---|
| Nav agent for C2 bots | `Phoebe`: `AthenaNavMesh`, radius 42, height 150 (also `Deimos`: radius 34, height 110) |
| `[/Script/NavigationSystem.RecastNavMesh]` | `RuntimeGeneration=Dynamic`, tile 1024 uu, cell 17×16, max slope 44.5°, step 45 |
| `[/Script/NavigationSystem.NavigationData]` | `RuntimeGeneration=Dynamic` |
| `[/Script/NavigationSystem.NavigationSystemV1]` | `bInitialBuildingLocked=True` |
| `FortNavSystem` | static world links up/down, jump-link actors |

Cooked nav data on the island exists **only at The Agency** (`POI/Apollo_POI_Agency_01`: `FortNavMesh` +
`NavMeshBoundsVolume`, probably for the henchmen). Everywhere else the server generates the nav mesh at
runtime. No pre-built island-wide navigation is missing from the build; it was never shipped.

## 4. Open questions

- Blueprint logic of `Mutator_AISupport` and the spawner's team assignment (needs a bytecode decoder).
- Who places ReachBeacon targets in Battle Lab.
- Which outfit bots actually wear, given the missing cosmetic tables.
