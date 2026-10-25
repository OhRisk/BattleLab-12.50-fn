# Bot Grenade System (12.50): decoded from the paks

Internal codename **FrenchYedoc** ("Decoy" reversed). Decoded with `Tools/uasset.py`; raw JSON under
`Tools/json/Athena/Items/Gameplay/FrenchYedoc/` and `Tools/json/Athena/Items/Consumables/FrenchYedoc/`.

## Items

| | Friendly | Enemy |
|---|---|---|
| Item | `/Game/Athena/Items/Consumables/FrenchYedoc/WID_Athena_FrenchYedoc_JWFriendly` | `…/WID_Athena_FrenchYedoc_JWUnfriendly` |
| Class | `FortWeaponRangedItemDefinition` | same |
| Display | "Friendly Bot Grenade": "Creates a Friendly Bot at the impact location." | "Enemy Bot Grenade" |
| Rarity / stack | Common, MaxStackSize 20, drops on death | same |
| Weapon actor | `B_FrenchYedoc_Friendly_Athena_Weap` | `B_FrenchYedoc_UnFriendly_Athena_Weap` |
| Fire ability | `GA_Athena_FrenchYedoc_JWFriendly` (child of `GA_Athena_FrenchYedoc_Parent`) | `GA_…_JWUnfriendly` |
| Projectile | `Prj_Athena_FrenchYedoc_JWFriendly` | `Prj_…_JWUnfriendly` |
| Spawner actor | `B_Athena_FrenchYedoc_JWFriendly` (`IsFriendly=true`) | `B_…_JWUnfriendly` (`IsFriendly=false`) |

Only the **JW** item pair exists in 12.50. `WID_Athena_FrenchYedoc_Friendly`/`_Unfriendly` (without
JW) do not exist as items; those names survive only in the weapon-actor blueprints.

## Runtime chain

```
Player fires WID → GA_Athena_FrenchYedoc_* (cook/throw montages, GameplayCue.BattleLab.FrenchYedoc.Throw)
  ├─ precheck: ClassToSpawnCheck = BP_Pawn_FrenchYedoc_C; on failure tag Athena.Ability.Failed.SpawnBot
  └─ throws Prj_Athena_FrenchYedoc_* (FortProjectileMovementComponent)
       └─ on impact spawns ActorToSpawn = B_Athena_FrenchYedoc_JW(Un)friendly  (InitialLifeSpan 1 s)
            ├─ GameplayCue.BattleLab.FrenchYedoc.Spawn / .Hit
            └─ spawns bot from BotData = BD_FrenchYedoc_JW via the native server bot manager
                 (UFortServerBotManagerAthena::SpawnBot)
```

## Bot data `BD_FrenchYedoc_JW` (FortAthenaAIBotCustomizationData)

| Field | Value |
|---|---|
| PawnClass | `BP_Pawn_FrenchYedoc_C` |
| Controller | `BP_FrenchYedocPlayerController` (AIPerception: sight, hearing, damage; FortAthenaAIBotPathFollowingComponent) |
| BehaviorTree | `/Game/Athena/AI/FrenchYedoc/BehaviorTrees/BT_FrenchYedoc` (+ `BB_FrenchYedoc`) |
| SkillLevel | 1100 (skill sets `AI_skill_frenchyedoc_bot_attacking`, curve `FrenchYedoc_SkillSets`) |
| Squad | `bHasCustomSquadId`, `CustomSquadId=50`; `bRequiresUniqueNetId` |
| Respawn | `bOverrideCanRespawnOnDeath` (bots do not respawn) |
| Startup inventory | `WID_Harvest_Pickaxe_Athena_C_T01`, **`WID_Assault_Auto_Athena_C_Ore_T02`** (Common AR, matches the wiki) |
| Cosmetics | `FrenchYedoc_CosmeticLibraryDefault`; fallback outfit `CID_416_Athena_Commando_M_AssassinSuit` |
| Naming | skin name + player-ID suffix |

Team assignment (friendly = thrower's team, enemy = separate squad) is done in the spawner's
blueprint graph from `IsFriendly`. The bytecode is not decoded, but no substitute is needed: it runs as-is
on a working server.

## Loot (why they did not drop, and the authentic fix)

`Playlist_BattleLab.LootPackages = AthenaCompositeLP_BattleLab` (composite of
`Playground/AthenaLootPackages_Client` + `BattleLab/OverrideLootPackagesDataBackup`). It contains 6 rows,
all **Count 5, Weight 0.0** as cooked:

| Row | List |
|---|---|
| `WorldList.AthenaConsumables.50/.51` | floor loot |
| `WorldList.AthenaHighConsumables.60/.61` | chests |
| `WorldList.AthenaSupplyDropConsumables.50/.51` | supply drops |

Epic's own re-enable values live in `/Game/Athena/Playlists/BattleLab/LootOverrides/OverrideLootPackagesDataBackup_Exp`:
**the same 6 rows at Weight 2.0**. This is consistent with the Dec 2019 "temporarily disabled" and later
re-enable happening through data swaps/hotfixes rather than a client patch.

**Confirmed mechanism (see LOOT_SYSTEM.md):** `BattleLabMutator_LootSelection` swaps in the chosen loot
pool's tables, and the "Experimental" option (default **On**) picks the `_Exp` variants. Every `_Exp`
composite includes `OverrideLootPackagesDataBackup_Exp`, so Bot Grenades drop at weight 2.0 in all 7 pools
unless Experimental is turned off. The client applied the `LocalServer/hotfixes/DefaultGame.ini` stand-in
(log: "updated column Weight from 0.00 to 2.00" ×6), but it only affects the pre-mutator default table.

## Server-side requirements

- A bot manager on the GameMode (playlist `AISettings = Athena_AI_Settings`, modifier `GameplayMod_DeimosAI`).
- Nav mesh on the Apollo map for `FortAthenaAIBotPathFollowingComponent`.
- A UniqueNetId for each bot (`bRequiresUniqueNetId`).
