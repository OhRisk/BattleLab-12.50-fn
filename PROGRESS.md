# Local preservation progress

The supplied installation is **12.50 CL-13137020**, confirmed by the running executable. It is not 12.41. Source installation is unchanged. Existing backend, launchers, asset tools, and research are preserved.

## Test 001

Hypothesis: the recorded pre-lobby crash is reproducible with the existing local backend.

Change: none to game behavior. Verified the existing outbound block applies to the working-copy executable, then launched it with the existing module/backend. A sandboxed attempt stalled before producing a fresh game log and was stopped; the normal isolated launch reproduced the failure.

Result: OhRisk identity/profile succeeds, party restore returns no parties, CreateParty waits for XMPP, XMPP fails, then multiple threads fault through the same invalid pointer. Evidence: `Logs/Test001/`.

Conclusion: timing correlation is confirmed; XMPP causation is not proven.

Next: collect actual unwind frames and fault instructions.

## Test 002

Hypothesis: the crash is in a shared native facility rather than a missing party UObject.

Change: added read-only fault-byte and x64 unwind logging to the existing module; original source and DLL saved in `Logs/OriginalNative/`.

Result: fault at executable RVA `0x2ba659b`, function `0x2ba6570`, instruction `call [rax+0x30]`. Its receiver comes from shared global RVA `0x8195718`. Other threads fail through the same receiver. Evidence: `Logs/Test002/`.

Conclusion: shared-pointer corruption is confirmed; the writer and object type require further evidence.

Next: test a built-in local path and watch the global's writes.

## Test 003

Hypothesis: the executable's existing `-nomcp` option avoids social initialization in this shipping client.

Change: appended `-nomcp` for this launch only. Existence established by executable strings, including explicit NoMcp messages.

Result: option appears in Filtered Command Line, but the client still logs in and attempts XMPP; the same shared-pointer crash occurs. Evidence: `Logs/Test003/`.

Conclusion: `-nomcp` alone does not provide an offline entry path for this client. No permanent flag change made.

Next: capture the instruction writing the shared global with a hardware data breakpoint.

## Test 004

Hypothesis: a hardware data breakpoint identifies the allocator-global writer.

Change: attached a local diagnostic debugger; saved a mapped image for local disassembly.

Result: executable-owned debug-register events were initially mistaken for our watchpoint. Repeated events produced a large log; test stopped. No corrupting writer identified in this run.

Conclusion: inconclusive diagnostic attempt. Do not treat its WRITE labels as proof of a write.

Next: distinguish game-owned debug events.

## Test 005

Hypothesis: passing unrelated debug events to the game permits a valid watchpoint capture.

Change: checked DR0 as well as DR6; passed unrelated exceptions through.

Result: the game installs its own debug registers, displacing the watch; later exits with access violation. Evidence: `Logs/Test005/`.

Conclusion: hardware watch is unsuitable here.

Next: opt-in page write trap that permits the original writes after logging.

## Test 006

Hypothesis: page write tracing identifies the precise allocator-global mutation.

Change: temporary diagnostic write protection and single-step restoration, enabled only with BATTLELAB_WATCH_GLOBAL. No write was suppressed.

Result: captured `0x20dac5a` adding `0x077192cc` to allocator global `0x8195718` immediately before the shared Free path faults. Evidence: `Logs/Test006/module.log`.

Conclusion: exact pointer corruption confirmed. Registering function decodes `fltoken=` and schedules this callback when the supplied argument's length is invalid.

Next: test whether eliminating the addition resolves the crash, then prefer the existing valid startup branch.

## Test 007

Hypothesis: suppressing only the allocator addition allows native cleanup to complete.

Change: experimental 11-byte runtime patch; no disk executable modification.

Result: earlier different access violation, before local login. Evidence: `Logs/Test007/` and Windows Application Error.

Conclusion: rejected and patch code removed. This is not the retained fix.

Next: omit the old launcher's malformed `-fltoken=none` argument, using the existing absent-argument branch.

## Test 008

Hypothesis: removing the malformed fltoken argument avoids the timer-driven allocator corruption.

Change: one launcher argument removed; game machine code unchanged.

Result: process remained alive but stalled earlier during asset/file loading. User confirmed a loading screen without progress. Thread stacks saved in `Logs/Test008/threads.txt`; the main thread waits in native file-loading code. No lobby or Level 2 success claimed.

Conclusion: this run does not yet validate the correction through the previous crash point.

Next: repeat corrected launch to determine whether this stall is reproducible.

## Test 009

Hypothesis: corrected launch can traverse the old crash point with original code.

Change: repeated Test008 unchanged after preserving its hung-thread evidence.

Result: identity/profile succeeds; XMPP fails; ten seconds later original party code logs `Failed to be connected in time, yielding success for create`, then `CreatingPartyCreateComplete Result=[Succeeded]`. No allocator crash. A separate "Fortnite was not started correctly" UI error appears; user supplied screenshot, then the client shuts down cleanly. Evidence: `Logs/Test009/`.

Conclusion: the crash was caused by malformed launcher input, not unavailable XMPP. The game's existing disconnected fallback works. Lobby not yet usable.

Next: test a local launch marker of the 24-character length established from the parser, with no machine-code patch.

## Subsequent tests (010–014)

- The 24-character local marker reaches the original lobby and locker, confirmed by the user. The delayed startup validation error remains.
- Removing `-fromfl=eac` and adding `-noeac` did not eliminate the error. Test012 records `LogAppES: closing code 1` at 09:31:38 UTC. User subsequently reproduced it in the locker.
- Battle Lab is exposed through the supplied FrontEndPlaylistData configuration. User confirms selection. The client requests bucket `12895384:0:NAE:playlist_battlelab` with original mutator options.
- Local WebSocket assignment completes, followed by FindSessionFailure and "Session has expired". No gameplay host is running on 7777.
- Backend buildUniqueId corrected from executable CL 13137020 to compatible build 12895384. Runtime validation pending.
- Direct map/nullrhi experiment stayed in frontend. Filtered command line presents map as a switch; standalone gameplay was not reached.
- Fixed undersized/uninitialized UTF-8 URL logging buffers. Long URLs previously produced misleading diagnostics. Native build succeeds.
- Test014 archives current evidence. Batch wrapper works; direct ps1 execution is blocked by machine policy. See LAUNCH_INSTRUCTIONS.md.

Outstanding: delayed startup rejection, session response validation, Athena/Battle Lab host initialization, and all gameplay tests.

## Test022: bot cosmetic loadout, deferred skin, client cheat forwarding

Test021 result (user): bots still idle, skin wrong, couldn't get a Friendly Bot Grenade. Logs:
- `Bot hero owner is null`: the native read is `(Controller+0x348)->vfunc[3]()` = the bot's cosmetic loadout (same pattern as `[PC+0x598]` in ApplyCharacterCustomizationToPlayerOnAuthority, where +0x40 is `Loadout.Character`). So `+0x5d0` is `CID->HeroDefinition`, and the bot's loadout has no Character.
- The host crashed (`FAULT +0x19c16ba`, StaticShutdownAfterError) right as the skin was re-applied inside the native refresh (re-entrancy).
- The server received no `cheat` at all. The shipping `FortPlayerController::Cheat` exec is a stub and never calls ServerCheat.

Changes:
- Bot setup hook fills empty Character/Backpack/Pickaxe/Glider/Contrail of the controller's cosmetic loadout from the spawning bot data (guarded: Character offset must be 0x40). Logs `Bot X loadout character CID_…`.
- Skin re-apply is queued and runs about 0.5s later from TickFlush, once per pawn.
- Player client module: ProcessEvent hook forwards `FortPlayerController.Cheat` to `ServerCheat`.

## Test021: skin re-applied after native refresh; bot hero owner; console fixes

Test020 result (user): bots now hold the AR (log: `items=3 [...Pickaxe, Assault_Auto, BulletsMedium] weapon=B_Assault_Auto`) but don't move or shoot. Friendly bots show no teammate arrow. Skin still wrong. Commands don't work.
- Bots: `RunBehaviorTree -> false`, and the host log (FortniteGame_2.log this run) still shows `condition failed: HeroType`, although PlayerState->HeroType was set first. The native step reads `(Controller+0x348)->vfunc[3]()->+0x40->+0x5d0`, which is apparently not (or not only) the player state Reboot resolves. The only caller of the check logger is +0x16732c0.
- Skin: the hero swap worked (`HID_556_…RebirthDefaultA -> HID_084_…Assassin`), but `ApplyCharacterCustomizationToPlayerOnAuthority` then reported "missing a body" and fell back to DefaultPartTypes (the hero caches parts at creation).
- Commands: no server log for them. The operator check required exactly "127.0.0.1", and giveitem only finds already-loaded items.

Changes:
- Hook `ApplyCharacterCustomizationToPlayerOnAuthority` (+0x23c6700, found through its "Failed to initialize character parts" string). After the native refresh, re-apply the Locker loadout (human controllers only, recursion-guarded). Removed the hero swap.
- The bot hero hook mirrors the native read, logs the object it finds (`Bot hero owner … HeroType …`) and fills its +0x5d0 if empty.
- Commands: every cheat is logged with the sender's address. Any 127.x counts as operator. giveitem/spawnpickup load the Bot Grenade WIDs (or any full /Game/ path) when they are not in memory.

## Test020: bot setup aborts on HeroType; hero item overrides skin

Test019 result (user): skin still wrong, bots still idle and unarmed. Logs:
- Every bot logged `items=0 [] weapon=none brain=none` and `RunBehaviorTree BT_FrenchYedoc -> false`. The host log shows `condition failed: HeroType [FortAthenaAIBotController.cpp:5212]` during the native SpawnBot, and the game's own setup (+0x16732c0, reads PlayerState+0x5d0) stops there. Test019 set HeroType only after SpawnBot returned, too late.
- The fallback item grant added nothing: Reboot's `AFortInventory::AddItem` returned early unless the inventory owner is an `APlayerController`, and bot owners are AI controllers.
- Skin: the server applied `CID_084_Athena_Commando_M_Assassin`, but 2s later `AFortPlayerState::ApplyCharacterCustomization` re-applied `CP_Body_Commando_F_RebirthDefaultA` from the player's hero item (`GetOrInitializeHero`, default without MCP).

Changes:
- SpawnBot hook records the bot data in progress. New hook at +0x16732c0 (prologue-checked), and the existing +0x1679450 hook fills `PlayerState->HeroType` from the bot data's character HeroDefinition *before* the native check.
- `AddItem` accepts any `AController` owner.
- Loadout: also points the controller's `StrongMyHero` item definition at the Locker character's HeroDefinition (logged `Hero X: old -> new`).

Expected log: `Bot ... HeroType = HID_...`, no `condition failed: HeroType`, `Bot state: items=2+ weapon=... brain=...`, and `Hero ...: HID_... -> HID_084...`.

## Test019: skin via HeroType, bot completion, console usage

User result (Test018): pickaxe + glider correct; skin wrong (the lobby shows John Wick). Bots are killable and react to crash pads but stay idle; friendly bots are the same. Console opens; "cheats don't work".
- Console: the player log shows `Command not recognized: giveitem ...`, i.e. typed without the `cheat` prefix. `FortPlayerController:Cheat` exists in this client. COMMANDS.md now stresses the prefix.
- Skin: server applied CID_084 to the pawn, then 2s later the game's own `AFortPlayerState::ApplyCharacterCustomization` rebuilt parts from the player state's HeroType (default recruit: CP_Head_F_RebirthDefaultA / CP_Body_Commando_F_RebirthDefaultA). Fix: set the player state's HeroType to the Locker character's HeroDefinition so the native refresh applies the correct skin.
- Grenade loot: both variants are w=2.0/c=5 in AthenaConsumables, AthenaHighConsumables and AthenaSupplyDropConsumables; odds are already equal.
- Bots: the inventory error is gone; `HeroType` check (+0x16732c0 reads PlayerState+0x5d0) still fails. BD_FrenchYedoc_JW: BT_FrenchYedoc, startup items Pickaxe + WID_Assault_Auto_Athena_C_Ore_T02, CID_416. New `CompleteNativeBot` after the native spawn: grant startup items (+999 ammo) and equip the AR if the inventory is empty; RunBehaviorTree(BT) if no BrainComponent; set HeroType. Diagnostic line `[BattleLabServer] Bot state: ...` per spawn.

## Test018: bot inventories, loadout ordering, developer console

User result (Test017): every grenade now spawns a bot (native SpawnBot works), but bots are unarmed and idle. Friendly bots can't be damaged by the player. Skin/pickaxe still default. Some Friendly Bot Grenade floor pickups are invisible.

Findings:
- Host log per bot: `condition failed: Inventory` (FortAthenaAIBotController.cpp:2223) and `condition failed: HeroType` (:5212). Traced via outlined loggers `+0x1660b10` / `+0x16623c0` to the controller setup at `+0x1679450`, which bails when `Inventory` (+0x548) is null. Nothing on this host writes that slot. Fix: prologue-checked MinHook on `+0x1679450` that spawns a world `FortInventory` owned by the controller (as Reboot does for its own bots), then runs the original setup.
- Friendly grenades: bots join team 3 (the player's team); enemy bots join team 18. "Can't damage friendly bots" is correct teammate behaviour.
- Loadout: applied at join (`Applied 3 Locker slot(s)`), then overwritten by the game's own join path (no MCP profile) before pawn/pickaxe creation. Now re-applied in SpawnDefaultPawnFor right before the pickaxe grant, and re-fetched at possession. Also copies to the pawn loadout + `OnRep_CustomizationLoadout`.
- Developer console (user request): the same module now runs in console-only mode when loaded without the host marker (no hooks/logger). The launcher loads it into the player client once the lobby is reached. Commands listed in COMMANDS.md.

Open: invisible Friendly Bot Grenade pickups (some); bot HeroType; bot movement pending the inventory fix.

## Test017: first playable session, then bots / cosmetics / inventory

User result (Test016 host): **joined Battle Lab on the local server**. Queue, session, key, connect and spawn all work. Problems reported:
1. Enemy Bot Grenade: the first spawned "John Wick" with no weapon and idle; later throws showed nothing. Server log: all 10 FrenchYedoc pawns spawned (`setting customized loadout data`); `SpawnBotHook` was called twice per throw. Cause: Reboot's SpawnBot replacement discards the grenade's bot data (behavior tree, weapon, team) and keeps only pawn class + skin. Fix (12.50): call the game's native SpawnBot with the full bot/runtime data; Reboot's path is only a fallback. Log line `[BattleLabServer] Native SpawnBot ...`.
2. Cosmetics missing. Server log: `GetAthenaLoadoutWithOverridesInternal(): MCP is enabled but we were unable to load AthenaProfile`; Reboot NoMCP mode hard-codes default recruit parts. Fix: backend `GET /battlelab/loadout` (read-only view of the saved Locker → asset paths). The host sets the controller's loadout at join and applies skin + back bling on every possession (incl. respawn). Locker/profile code unchanged.
3. Swap failed + invisible 6th slot. Reboot's pickup loop counted slots while iterating and removed the swapped item from the list it was iterating; after a swap it still treated the inventory as full and re-dropped the new item. Fix (12.50): stack → swap/add with a precomputed 5-slot count → overflow.
4. No Friendly Bot Grenades found (10 enemy). Loot data weights are equal (Codex notes), so this needs more samples before concluding.
5. Cars: none exist in C2S2 (added C2S3). Choppa/motorboat are the vehicles to verify.

Host rebuilt; unattended boot → READY in 85s.

## Test016: local Battle Lab host (Project Reboot 3.0, loopback build)

Hypothesis: the queue fails only because (a) the encryption-key route is missing and (b) nothing hosts 127.0.0.1:7777. Codex had already built Reboot 3.0 (loopback bind guard, Battle Lab default playlist, headless), but nothing launched it.

Changes:
- Backend: `GET /fortnite/api/game/v2/matchmaking/account/:id/session/:sid` returns a per-session 32-byte key. Host manager: the matchmaker keeps the client `Queued` until the host reports READY, starting `BattleLabLauncher.exe --host` on demand. New routes: `/battlelab/host/{ready,status,start}`. Config in `LocalServer/LocalServerConfig.json`. Health version 3.
- Launcher `--host [--visible]`: second working-copy client, `-nullrhi -nosound -nosplash`, fltoken marker `localbattlelabhost000000` (also identifies its log). Client DLL is injected at start; `BattleLabHost.dll` (Reboot) once the frontend is idle.
- Reboot: READY callback to the backend after `Listen()`, plus a first-chance fault logger (module+offset+unwind).

Iterations:
1. Injected at "sign-in Completed". Crashed: Reboot ran while the game thread was still synchronously streaming the lobby rooms. Now waits for an idle frontend state.
2. Crash in `Hooking::MinHook::Hook(UObject*,UFunction*)`: the 12.50 `SpawnBot` exec thunk yields a bogus vtable index. Added a plausibility guard, and 12.50 now uses the string-anchored `SpawnBot` finder (native +0x1a2f1d0).
3. The playlist is set to the real `Playlist_BattleLab` and Apollo loads, then a null read at +0x3d19c56 (`UWidgetBlueprintGeneratedClass::InitializeWidgetStatic`, widget `HUD_InteractionPoint_C`). `-nullrhi` is not the cause (the windowed host crashed identically). Root cause: Reboot sets `GIsClient=false`, so the loader filters not-for-server exports (widget trees), while this client-cooked build still creates HUD widgets. For 12.50, `GIsClient` now stays true (listen/standalone semantics); GetNetMode hooks unchanged.
4. Result: `Listening on port 7777`, `[BattleLabServer] Playlist active: Playlist_BattleLab`, `READY` 31s after the host module loaded.

User observation: the "headless" host still shows a window and waited at subgame select until the user clicked Battle Royale. The launcher now injects at SubgameSelect and hides the headless host's window. `LaunchBattleLab.ps1` ignores the host PID when checking for a running client.

Next: player queues → session → key → connect to 7777.

## Test015: compatible session build

Rebuilt native binaries successfully and launched through LaunchBattleLab.bat (client PID 13340). At 09:38:33 UTC the client passed session lookup and requested `/fortnite/api/game/v2/matchmaking/account/<account>/session/<session>`. The backend returned its unhandled-route 204, causing `FailedToQueryEncryptionKey`. This confirms the build-ID correction fixes the prior FindSessionFailure. The next requirements are a real local gameplay host and its matching session/encryption data; fabricating a key without a host would only move failure to connection timeout. Delayed startup rejection remains unresolved.

## Test021: native bot behavior-tree enable gate (2026-09-27)

Preserved the playable host DLL and relevant sources in `Logs/Before-BotSkin-Fix` before editing. Claude's native SpawnBot, inventory, hero, and deferred skin changes remain in place.

Read-only inspection of the running 12.50 host resolved AIController.RunBehaviorTree's exec thunk at RVA 0x49f7780. The FortAthenaAIBotController override at 0x1684700 stores the requested tree, then reads CachedBotManager (reflected controller offset 0x408) and refuses to call engine RunBehaviorTree while the manager byte at 0x4d0 is zero. The engine implementation is present at 0x4987d80; it is not a stripped constant-false stub.

Change: before native SpawnBot, enable that instance's manager flag. A 19-byte comparison of the exact native gate is required before writing; a mismatch leaves it unchanged and logs a warning. This retains the original behavior tree and controller. Build succeeded; deployed Native/bin/BattleLabHost.dll. Gameplay validation pending.

The previous run used the deferred skin fix: logs show the selected CID_084 applied again after native customization refresh. Visual confirmation remains necessary; no additional skin fix claimed. Last-run logs preserved as `reboot-last-run.log` in the backup directory.

## Test022: native ReachBeacon manager initialization (2026-09-27)

Test021 enabled the original BehaviorTreeComponent successfully, then the first bot AI tick crashed at game+0x1a142b5 reading address 0x1098. Saved the host log in Logs/Test021/reboot.log. The ReachBeacon evaluator's CachedServerManager was null: its initializer at 0x187c690 calls the client-build helper at 0x6879d0, which is a constant-null return. Native beacon selection dereferences the manager's BeaconList at +0x1098.

Added a 12.50-only, exact-prologue-checked hook on the evaluator initializer. It runs original initialization, then fills the missing CachedServerManager from CachedBotController.CachedBotManager using reflected properties. The shared null-return helper is not changed. Host build succeeded (Logs/bot-beacon-build.log); deployed and restarted. Gameplay and selected-skin visual validation still pending.

## Test023: Loot evaluator bot manager, replicated CharacterData (2026-09-27)

Test022 result (user): bots hold the AR but only turn occasionally; own skin still wrong in game. Evidence in `Logs/Before-BotManager-CallSites/` (sources, previous DLL, host logs).
- Host log: 5,402 × `condition failed: CachedOwnerComp && ... CachedServerManager && CachedBotController && LootingSkillSet [FortAthenaAIBotEvaluator_Loot.cpp:684]`, i.e. the original Battle Lab Loot evaluator bailed every tick. Its initializer (+0x184ed10) caches the manager through the same folded null getter +0x6879d0 as ReachBeacon. Scanning the xrefs of the "CachedServerManager" check strings shows only these two evaluators cache it. The getter itself has 1,686 callers and cannot be hooked.
- Skin: 12.50's FortPlayerState has no `CharacterParts`; the look replicates only through `CharacterData` (+0x4e8, OnRep_CharacterData) and the pawn reads it ("Using Character Parts In Player State: YES"). Native ApplyCharacterCosmetics goes through the controller's MCP hero, and Reboot never checked its result. The client log shows every bot stuck on `Hero:None ... DoCharacterPartsNeedReplication:1`.

Changes:
- Replaced the ReachBeacon post-hook with byte-checked call-site redirects (Loot +0x184ed0d, ReachBeacon +0x187c6ff) to a near stub that returns GameMode->ServerBotManager. The original initializers now run with the manager.
- `BattleLab::WriteCharacterData`: CID's hero-specialization parts + back bling -> `PlayerState->CharacterData.Parts[type]`, WasPartReplicatedFlags, OnRep_CharacterData, ForceNetUpdate. Used for the Locker skin (spawn + post-refresh re-apply) and for every native-spawned bot.

Expected log: `Loot evaluator now receives the server bot manager`, `ReachBeacon evaluator now receives ...`, `CharacterData for FortPlayerStateAthena_...: <parts> (flags ..)`; no Loot/ReachBeacon condition failures. Full build OK (`Logs/bot-manager-skin-build.log`). Gameplay validation pending.

## Test024: runtime navmesh for the bot AI; leave-to-lobby crash (2026-09-27)

Test023 result (user): Locker skin correct in game. Bots still only turn. Host crashed when the user left for the lobby.
- Host log after Test023: Loot/ReachBeacon failures gone; new `condition failed: NavData [FortAthenaAIBotEvaluator.cpp:253]`. The evaluators call `NavSys->GetNavDataForProps` and get null.
- Cause: the client paks contain no Battle Royale navmesh (server-only export). The level `Apollo_Nav_Gameplay` still loads the island's NavMeshBoundsVolume (extent 165000 x 160000 x 20000). The map's own config is `NullNavSysConfig`; Fortnite's AthenaNavSystem started with no nav data, no octree, octree lock and initial build lock held, `bAllowAutoRebuild` off.
- Crash: `FixupInvalidRespawnVisuals` -> +0x19c15d0 calls StartCameraFade on a null PlayerCameraManager (+0x2b0) for the remote controller.

Changes (new `BattleLabNav.h`, all native calls prologue-checked):
- At module load: AthenaNavMesh/FortNavMesh/RecastNavMesh defaults use RuntimeGeneration Dynamic. The AthenaNavMesh class default gets its own vtable copy whose NeedsLoadForClient (+0xd0, the shared `return false` stub +0x6879c0) returns true, otherwise SpawnActor refuses it ("due to client/server context"), plus bNetLoadOnClient.
- READY + 5 s: bAutoCreateNavigationData, bAllowAutoRebuild, bRebuildOnInitialUnlock; OnNavigationBoundsUpdated spawns `AthenaNavMesh-Phoebe` in Apollo_Nav_Gameplay.
- Once registered: island box added to `NavOctTreeInclusionBounds`; octree lock (+0x2f8 bit 0) cleared; ConditionalPopulateNavOctree (+0x48894d0); RemoveNavigationBuildLock (+0x48a5160, initial lock 0x4); blocking UNavigationSystemV1::Build (+0x4886b60).
- Result (headless host, repeated): whole island built in 3.7-7.6 s; open-ground samples navigable. Building interiors are not (Fortnite feeds building pieces through its building-grid system; `bUseBuildingGridAsNavigableSpace=false` did not help, reverted).
- Invoker-only generation was tried first and abandoned: Athena accepts only AthenaNavInvokerBox invokers and still produced no active tiles. The whole-island build is cheaper and simpler.
- Respawn-visuals hook (+0x19c15d0) skips the fade when the controller has no camera manager.

Expected host log: `[BattleLabNav] Navmesh AthenaNavMesh-Phoebe built in N ms (locks 0x0); x/8 sample loot spots navigable`, and no `condition failed: NavData`. Gameplay validation pending.

## Test025: "Has Landed From Bus" blackboard key; random bot skins (2026-09-27)

Test024 result (user): bots still only turn around; every bot wears the same skin. Host log: the navmesh built normally (`Navmesh AthenaNavMesh-Phoebe built in 7610 ms`), and every bot runs a BehaviorTreeComponent.
- Tree: `BT_FrenchYedoc` = MoveTo Beacon (if the ReachBeacon evaluator has one), otherwise RunBehavior `BT_Phoebe_Gameplay`. There, every on-ground branch (attack, loot, patrol, POI) sits under `Has Landed From Bus` (`AIEvaluator_Global_HasEverJumpedFromBusAndLandedKey`). Only focus/scan services run without it, which matches "turns around".
- Writers of that key: its cached key ID (byte +0x78208bf, filled by +0x1678530) is read only by the bus-jump landing path (+0x166d2a0, from +0x1879dc0) and the debug circle spawner (+0x16844e0). Nothing sets it for bots spawned into the world, and no FrenchYedoc blueprint does either.
- Skin: `BD_FrenchYedoc_JW` has `OverrideCosmeticMode = CosmeticLibrary` -> `FrenchYedoc_CosmeticLibraryDefault`, whose character/backpack DataTables are not in the client paks (server-only). The loadout CID_416 is only the fallback.

Changes (`FortServerBotManagerAthena.cpp`):
- `MarkBotLanded`: after the native SpawnBot, Blackboard.SetValueAsBool(HasEverJumpedFromBusKey=false, HasEverJumpedFromBusAndLandedKey=true), as the native landing path does, then read it back.
- `PickBotCharacter`: for CosmeticLibrary bot data, a random CID from `BattleLabBotSkins.h` (706 `CID_###_Athena_Commando_M/F_*` generated from the pak index), with up to 8 retries when parts are missing. It is used for the player state's HeroType, CharacterData, the pawn's CustomizationLoadout and the controller's CosmeticLoadoutBC.

Expected host log: `Bot ... marked as landed: true` and a different `CharacterData for ...` parts line for each bot. Host-only build OK (`Logs/bot-landed-build.log`). Gameplay validation pending.

## Test026: bot perception, attitude and diagnostics (2026-09-27)

The detailed log and trace are in `BOT_AI_PROGRESS.md` and `BOT_INITIALIZATION_TRACE.md`. Summary:
- Cleared the server bot manager's `bBotHostileToHumanPlayersOnly`. It made every bot Friendly towards every other bot.
- Added a perception listener refresh after spawn, because the listener caches its team before the bot has one.
- Added a `SwitchTeam` hook: net update and log, for the friendly arrow.
- Set jumped-from-bus to true together with landed.
- Added `BattleLabBotDebug.h`: `[BotAI]` state dumps and a dev self-test behind `Native\bin\bot-selftest.flag`.
- Headless: enemy bots now detect each other and become Threatened. Ranged attack is still Denied there, pending a live test.
