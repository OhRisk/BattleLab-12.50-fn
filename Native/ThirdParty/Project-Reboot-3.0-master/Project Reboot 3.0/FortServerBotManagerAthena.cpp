#include "FortServerBotManagerAthena.h"
#include "bots.h"
#include "ai.h"
#include "finder.h"
#include "BattleLabLoadout.h"
#include "BattleLabBotSkins.h"
#include "BattleLabNav.h"

#include <random>

// Bot data with OverrideCosmeticMode = CosmeticLibrary (e.g. BD_FrenchYedoc_JW) dresses each bot from its
// library's character table; those tables are server-only and absent from the client paks, so the game
// falls back to the loadout's single CID for every bot. Pick from every player character instead.
static UObject* PickBotCharacter(UFortAthenaAIBotCustomizationData* BotData)
{
	auto Fallback = BotData->GetCharacterCustomization()->GetCustomizationLoadout()->GetCharacter();

	static auto ModeOffset = BotData->GetOffset("OverrideCosmeticMode", false);
	if (ModeOffset == -1 || BotData->Get<uint8_t>(ModeOffset) == 0) // SpecificLoadout
		return Fallback;

	static auto CharacterClass = FindObject<UClass>(L"/Script/FortniteGame.AthenaCharacterItemDefinition");
	static std::mt19937 Rng{ (unsigned)GetTickCount64() };
	std::uniform_int_distribution<size_t> Pick(0, std::size(BattleLab::BotCharacterPool) - 1);

	for (int Attempt = 0; Attempt < 8; ++Attempt)
	{
		std::string Name = BattleLab::BotCharacterPool[Pick(Rng)];
		std::wstring Path = L"/Game/Athena/Items/Cosmetics/Characters/" + std::wstring(Name.begin(), Name.end()) + L"." + std::wstring(Name.begin(), Name.end());
		auto Character = LoadObject<UFortItemDefinition>(Path.c_str(), CharacterClass);

		if (Character && !BattleLab::CollectCosmeticParts(Character, nullptr).empty())
			return Character;
	}

	return Fallback;
}

// BT_Phoebe_Gameplay runs every on-ground behavior (attack, loot, patrol, move to POI) under "Has Landed
// From Bus" (AIEvaluator_Global_HasEverJumpedFromBusAndLandedKey). The game sets that key only when a bot
// lands from the Battle Bus (0x166d2a0) or is teleported by the debug spawner (0x16844e0); bots spawned
// straight into the world (Bot Grenades) never get it, so the tree finds nothing to run and the bot only
// scans around. Set the keys the way the landing path does: jumped-from-bus true, landed true.
static void MarkBotLanded(AController* Controller)
{
	static auto BlackboardOffset = Controller->GetOffset("Blackboard", false);
	auto Blackboard = BlackboardOffset != -1 ? Controller->Get(BlackboardOffset) : nullptr;
	static auto SetValueAsBoolFn = FindObject<UFunction>(L"/Script/AIModule.BlackboardComponent.SetValueAsBool");
	static auto GetValueAsBoolFn = FindObject<UFunction>(L"/Script/AIModule.BlackboardComponent.GetValueAsBool");

	if (!Blackboard || !SetValueAsBoolFn || !GetValueAsBoolFn)
	{
		LOG_WARN(LogBots, "[BattleLabServer] Cannot mark {} landed (blackboard {})", Controller->GetName(), __int64(Blackboard));
		return;
	}

	static auto JumpedKey = UKismetStringLibrary::Conv_StringToName(L"AIEvaluator_Global_HasEverJumpedFromBusKey");
	static auto LandedKey = UKismetStringLibrary::Conv_StringToName(L"AIEvaluator_Global_HasEverJumpedFromBusAndLandedKey");

	struct { FName KeyName; bool BoolValue; } SetJumped{ JumpedKey, true }, SetLanded{ LandedKey, true };
	Blackboard->ProcessEvent(SetValueAsBoolFn, &SetJumped);
	Blackboard->ProcessEvent(SetValueAsBoolFn, &SetLanded);

	struct { FName KeyName; bool ReturnValue; } Check{ LandedKey, false };
	Blackboard->ProcessEvent(GetValueAsBoolFn, &Check);
	LOG_INFO(LogBots, "[BattleLabServer] Bot {} marked as landed: {}", Controller->GetName(), Check.ReturnValue);
}

// The bot AI decides about weapons from AFortAthenaAIBotController::SlotItems (+0x360), six FFortBotInventoryInfo
// { ItemDefinition +0, FortItem +8, native bytes +0x10 (initialised to 0x70) and +0x11 (weapon category) }.
// The range attack evaluator (+0x186a680) needs a slot with a ranged weapon that has ammo (+0x1678050), and weapon
// selection (+0x1670110) picks the bot's desired weapon from the slots. Nothing in this client executable ever
// writes them (the server's inventory handling is not compiled in), so bots never shoot. Fill them from the bot's
// inventory the way the game assigns slots: by the inventory skill set's slot preference (+0x1685010, lower is
// better, -1 = no match; as the pickup slot choice +0x1690480 compares), with the native weapon category
// (+0x26565a0). Idempotent; rebuilt whenever the inventory's weapons change.
void UFortServerBotManagerAthena::SyncBotSlotItems(AController* Controller)
{
	static constexpr int NumSlots = 6, SlotSize = 0x18;
	static auto SlotItemsOffset = Controller->GetOffset("SlotItems", false);
	static auto SkillSetOffset = Controller->GetOffset("CacheInventoryDigestedSkillSet", false);
	static auto InventoryOffset = Controller->GetOffset("Inventory", false);
	auto Inventory = InventoryOffset != -1 ? Controller->Get<AFortInventory*>(InventoryOffset) : nullptr;
	if (SlotItemsOffset == -1 || SkillSetOffset == -1 || !Inventory)
		return;

	static const uint8_t PreferencePrologue[] = { 0x40, 0x53, 0x55, 0x48, 0x83, 0xEC, 0x58, 0x49, 0x8B, 0xD8, 0x4D, 0x85, 0xC0 };
	static const uint8_t CategoryPrologue[] = { 0x48, 0x83, 0xEC, 0x48, 0x48, 0x85, 0xC9 };
	static auto SlotPreference = BattleLabNav::CheckedNative<uint32 (*)(UObject*, int, UObject*)>(0x1685010, PreferencePrologue, "InventorySlotPreference");
	static auto WeaponCategory = BattleLabNav::CheckedNative<uint8_t (*)(UObject*)>(0x26565a0, CategoryPrologue, "WeaponCategory");
	if (!WeaponCategory)
		return;

	auto Slots = (uint8_t*)Controller + SlotItemsOffset;
	auto SkillSet = Controller->Get(SkillSetOffset);

	// Weapons in the inventory, in inventory order.
	std::vector<std::pair<UFortItemDefinition*, UFortItem*>> Weapons;
	auto& Items = Inventory->GetItemList().GetItemInstances();
	for (int i = 0; i < Items.Num(); ++i)
		if (auto Definition = Cast<UFortWeaponItemDefinition>(Items.at(i)->GetItemEntry()->GetItemDefinition()))
			Weapons.push_back({ Definition, Items.at(i) });

	// Unchanged? (the same items in the same slots)
	int Filled = 0;
	for (int s = 0; s < NumSlots; ++s)
		Filled += *(UFortItem**)(Slots + s * SlotSize + 8) != nullptr;
	bool bSame = Filled == (std::min)((int)Weapons.size(), NumSlots);
	for (auto& [Definition, Item] : Weapons)
	{
		bool bFound = false;
		for (int s = 0; s < NumSlots && !bFound; ++s)
			bFound = *(UFortItem**)(Slots + s * SlotSize + 8) == Item;
		bSame &= bFound;
	}
	if (bSame)
		return;

	for (int s = 0; s < NumSlots; ++s)
	{
		auto Slot = Slots + s * SlotSize;
		*(UObject**)Slot = nullptr;
		*(UObject**)(Slot + 8) = nullptr;
		*(Slot + 0x11) = WeaponCategory(nullptr);
	}

	std::string Summary;
	for (auto& [Definition, Item] : Weapons)
	{
		int Best = -1;
		uint32 BestRank = 0xFFFFFFFF;
		for (int s = 0; s < NumSlots; ++s)
		{
			if (*(UFortItem**)(Slots + s * SlotSize + 8))
				continue;
			uint32 Rank = SlotPreference && SkillSet ? SlotPreference(SkillSet, s, Definition) : 0xFFFFFFFF;
			if (Best == -1 || Rank < BestRank)
				Best = s, BestRank = Rank;
		}
		if (Best == -1)
			break;

		auto Slot = Slots + Best * SlotSize;
		*(UObject**)Slot = Definition;
		*(UFortItem**)(Slot + 8) = Item;
		*(Slot + 0x11) = WeaponCategory(Definition);
		Summary += std::format(" {}:{}(category {})", Best, Definition->GetName(), *(Slot + 0x11));
	}

	LOG_INFO(LogBots, "[BattleLabServer] Bot {} weapon slots:{}", Controller->GetName(), Summary);
}

// Server tick: keep every bot's SlotItems in step with its inventory (pickups, spent items).
// Patrol destinations come from AFortAthenaAIBotController::GetRandomLocationSafeToReach (+0x1673940), which picks
// among the bot mutator's BasePlacementQuery results (native array at mutator +0x3f0; the mutator retries the query
// from BeginPlay until the navmesh is ready, +0x1ae9fa0). Log once when they arrive.
static void ReportBotPatrolLocations()
{
	static auto MutatorClass = FindObject<UClass>(L"/Script/FortniteGame.FortAthenaMutator_Bots");
	static bool bReported = false;
	if (bReported || !MutatorClass)
		return;

	for (auto Mutator : GetAllObjectsOfClass(MutatorClass))
	{
		int Results = Mutator->GetName().starts_with("Default__") ? 0 : *(int*)((uint8_t*)Mutator + 0x3f8);
		if (Results > 0)
		{
			bReported = true;
			LOG_INFO(LogBots, "[BattleLabServer] Bot mutator {} has {} patrol locations", Mutator->GetName(), Results);
		}
	}
}

void UFortServerBotManagerAthena::TickBots()
{
	static ULONGLONG NextSync = 0;
	if (GetTickCount64() < NextSync)
		return;
	NextSync = GetTickCount64() + 2000;

	ReportBotPatrolLocations();

	static auto BotControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortAthenaAIBotController");
	if (!BotControllerClass)
		return;
	for (auto Object : GetAllObjectsOfClass(BotControllerClass))
	{
		auto Controller = (AController*)Object;
		if (!Controller->GetName().starts_with("Default__") && Controller->GetPawn())
			SyncBotSlotItems(Controller);
	}
}

// Local 12.50 host: after the game's own SpawnBot, complete what this host's missing setup steps left out,
// using only the caller's bot data (e.g. BD_FrenchYedoc_JW): startup inventory (+ammo, equipped),
// the behavior tree if no brain is running, and the player state's HeroType.
static void CompleteNativeBot(AFortPlayerPawnAthena* Pawn, UFortAthenaAIBotCustomizationData* BotData)
{
	auto Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller || !BotData)
		return;

	static auto InventoryOffset = Controller->GetOffset("Inventory", false);
	auto Inventory = InventoryOffset != -1 ? Controller->Get<AFortInventory*>(InventoryOffset) : nullptr;

	static auto StartupInventoryOffset = BotData->GetOffset("StartupInventory", false);
	auto StartupInventory = StartupInventoryOffset != -1 ? BotData->Get(StartupInventoryOffset) : nullptr;

	if (Inventory && StartupInventory && Inventory->GetItemList().GetItemInstances().Num() == 0)
	{
		static auto ItemsOffset = StartupInventory->GetOffset("Items");
		auto& StartupItems = StartupInventory->Get<TArray<UFortItemDefinition*>>(ItemsOffset);
		UFortWeaponItemDefinition* WeaponToEquip = nullptr;
		FGuid WeaponGuid{};

		for (int i = 0; i < StartupItems.Num(); ++i)
		{
			auto Definition = StartupItems.at(i);
			if (!Definition)
				continue;

			auto Added = Inventory->AddItem(Definition, nullptr, 1);
			auto WeaponDefinition = Cast<UFortWeaponItemDefinition>(Definition);

			if (WeaponDefinition && Added.first.size())
			{
				if (auto AmmoData = WeaponDefinition->GetAmmoData())
					Inventory->AddItem(AmmoData, nullptr, 999);

				// Prefer a ranged weapon (anything with ammo) over the pickaxe.
				if (!WeaponToEquip || WeaponDefinition->GetAmmoData())
				{
					WeaponToEquip = WeaponDefinition;
					WeaponGuid = Added.first[0]->GetItemEntry()->GetItemGuid();
				}
			}
		}

		Inventory->Update();

		if (WeaponToEquip)
			Pawn->EquipWeaponDefinition(WeaponToEquip, WeaponGuid);
	}

	static auto BrainOffset = Controller->GetOffset("BrainComponent", false);
	static auto BehaviorTreeOffset = BotData->GetOffset("BehaviorTree", false);
	if (BrainOffset != -1 && !Controller->Get(BrainOffset) && BehaviorTreeOffset != -1)
	{
		static auto RunBehaviorTreeFn = FindObject<UFunction>(L"/Script/AIModule.AIController.RunBehaviorTree");
		struct { UObject* BTAsset; bool ReturnValue; } Params{ BotData->Get(BehaviorTreeOffset), false };

		if (RunBehaviorTreeFn && Params.BTAsset)
		{
			Controller->ProcessEvent(RunBehaviorTreeFn, &Params);
			LOG_INFO(LogBots, "[BattleLabServer] RunBehaviorTree {} -> {}", Params.BTAsset->GetName(), Params.ReturnValue);
		}
	}

}

// The game's bot controller setup (FortAthenaAIBotController.cpp, "condition failed: HeroType") aborts before
// the bot's weapons and behavior tree when the player state has no HeroType; without MCP nothing sets it.
// Fill it from the spawning bot data's character (e.g. CID_416 -> its HeroDefinition), as MCP would.
void UFortServerBotManagerAthena::EnsureBotHeroType(UObject* ControllerObject)
{
	auto Controller = (AController*)ControllerObject;
	auto BotData = PendingBotData;
	auto PlayerState = Controller ? Controller->GetPlayerState() : nullptr;

	if (!BotData || !PlayerState)
	{
		LOG_WARN(LogBots, "[BattleLabServer] HeroType: botdata={} playerstate={}", __int64(BotData), __int64(PlayerState));
		return;
	}

	static auto HeroTypeOffset = PlayerState->GetOffset("HeroType", false);
	if (HeroTypeOffset == -1 || PlayerState->Get(HeroTypeOffset))
		return;

	auto Character = PendingCharacter ? PendingCharacter : BotData->GetCharacterCustomization()->GetCustomizationLoadout()->GetCharacter();
	static auto HeroDefinitionOffset = Character ? Character->GetOffset("HeroDefinition", false) : -1;
	auto HeroDefinition = Character && HeroDefinitionOffset != -1 ? Character->Get(HeroDefinitionOffset) : nullptr;

	PlayerState->Get(HeroTypeOffset) = HeroDefinition;
	LOG_INFO(LogBots, "[BattleLabServer] Bot {} HeroType = {}", Controller->GetName(), HeroDefinition ? HeroDefinition->GetName() : "null");
}

// The bot's AIPerceptionComponent registers as a sight listener while the controller spawns, before the
// bot has its team, and AISense_Sight keeps that team for its affiliation filter (DetectEnemies only).
// Without a refresh the bot never sees anyone as an enemy. Native SwitchTeam refreshes it (+0x168a110 ->
// RequestStimuliListenerUpdate); bots that keep their spawn team (Enemy Bot Grenades) need it once here.
static void RefreshBotPerception(AController* Controller)
{
	static auto PerceptionOffset = Controller->GetOffset("PerceptionComponent", false);
	static auto RequestUpdateFn = FindObject<UFunction>(L"/Script/AIModule.AIPerceptionComponent.RequestStimuliListenerUpdate");
	if (auto Perception = PerceptionOffset != -1 ? Controller->Get(PerceptionOffset) : nullptr; Perception && RequestUpdateFn)
		Perception->ProcessEvent(RequestUpdateFn);
}

void UFortServerBotManagerAthena::SwitchTeamHook(UObject* Context, FFrame& Stack, void* Ret)
{
	auto Controller = (AController*)Context;
	auto PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());
	int OldTeam = PlayerState ? PlayerState->GetTeamIndex() : -1;
	int OldSquad = PlayerState ? PlayerState->GetSquadId() : -1;

	SwitchTeamOriginal(Context, Stack, Ret);

	if (!PlayerState)
		return;

	// Push the new team to clients now rather than on the next periodic update.
	((AActor*)PlayerState)->ForceNetUpdate();
	static auto PlayerTeamOffset = PlayerState->GetOffset("PlayerTeam", false);
	if (auto TeamInfo = PlayerTeamOffset != -1 ? (AActor*)PlayerState->Get(PlayerTeamOffset) : nullptr)
		TeamInfo->ForceNetUpdate();
	if (auto Pawn = Controller->GetPawn())
		Pawn->ForceNetUpdate();

	LOG_INFO(LogBots, "[BattleLabServer] SwitchTeam {}: team {} -> {}, squad {} -> {} (t={})", Controller->GetName(),
		OldTeam, (int)PlayerState->GetTeamIndex(), OldSquad, (int)PlayerState->GetSquadId(), GetTickCount64());
}

AFortPlayerPawnAthena* UFortServerBotManagerAthena::SpawnBotHook(UFortServerBotManagerAthena* BotManager, FVector& InSpawnLocation, FRotator& InSpawnRotation,
	UFortAthenaAIBotCustomizationData* InBotData, FFortAthenaAIBotRunTimeCustomizationData& InRuntimeBotData)
{
	LOG_INFO(LogBots, "SpawnBotHook!");

	if (__int64(_ReturnAddress()) == SpawnBotRet()) {
		return SpawnBotOriginal(BotManager, InSpawnLocation, InSpawnRotation, InBotData, InRuntimeBotData);
	}

	// Local Battle Lab host: use the game's own spawn with the caller's full bot data (behavior tree,
	// weapon loadout, team from the runtime data, e.g. Friendly/Enemy Bot Grenades). Reboot's rebuild
	// below keeps only the pawn class and skin, producing an unarmed, idle bot.
	if (Fortnite_Version == 12.50)
	{
		// This build's FortAthenaAIBotController::RunBehaviorTree checks
		// CachedBotManager+0x4d0 before calling the engine implementation.
		// Reboot constructs the manager with SpawnObject and never enables this
		// non-reflected flag. Preserve the native controller/tree implementation.
		static const unsigned char Gate[] = {0x48,0x8b,0x89,0x08,0x04,0,0,
			0x48,0x85,0xc9,0x74,0x09,0x80,0xb9,0xd0,0x04,0,0,0};
		auto NativeGate = (const unsigned char*)GetModuleHandleW(nullptr) + 0x1684726;
		if (BotManager && memcmp(NativeGate, Gate, sizeof(Gate)) == 0) {
			auto& Enabled = BotManager->Get<uint8_t>(0x4d0);
			if (!Enabled) {
				Enabled = 1;
				LOG_INFO(LogBots, "[BattleLabServer] Enabled native bot-manager behavior-tree gate (12.50)");
			}
		}
		else LOG_WARN(LogBots, "[BattleLabServer] Bot-manager gate validation failed; left unchanged");

		// bBotHostileToHumanPlayersOnly (native default true, as in Battle Royale) makes the bot controller's
		// GetTeamAttitudeTowards (+0x16761b0) report every other bot as Friendly regardless of team, so Friendly
		// and Enemy Bot Grenade bots never fight each other. Fortnite's own team modes clear it
		// (BP_Phoebe_Mutator_DM); do the same so friend or foe follows the bots' teams.
		static auto HostileToHumansOnlyOffset = BotManager ? BotManager->GetOffset("bBotHostileToHumanPlayersOnly", false) : -1;
		if (HostileToHumansOnlyOffset != -1 && BotManager->Get<bool>(HostileToHumansOnlyOffset))
		{
			BotManager->Get<bool>(HostileToHumansOnlyOffset) = false;
			LOG_INFO(LogBots, "[BattleLabServer] Bots may now fight other bots (bBotHostileToHumanPlayersOnly cleared)");
		}
		auto Character = InBotData ? PickBotCharacter(InBotData) : nullptr;
		PendingBotData = InBotData;
		PendingCharacter = Character;
		auto NativePawn = SpawnBotOriginal(BotManager, InSpawnLocation, InSpawnRotation, InBotData, InRuntimeBotData);
		PendingBotData = nullptr;
		PendingCharacter = nullptr;
		LOG_INFO(LogBots, "[BattleLabServer] Native SpawnBot {} -> {}", InBotData ? InBotData->GetName() : "null", NativePawn ? NativePawn->GetFullName() : "null (falling back)");

		if (NativePawn)
		{
			CompleteNativeBot(NativePawn, InBotData);

			if (auto Controller = NativePawn->GetController())
			{
				MarkBotLanded(Controller);
				RefreshBotPerception(Controller);
				SyncBotSlotItems(Controller);
			}

			// Without this, clients wait forever for the bot's parts ("Hero:None ... DoCharacterPartsNeedReplication").
			auto BotLoadout = InBotData->GetCharacterCustomization()->GetCustomizationLoadout();
			bool bLibraryPick = Character && Character != BotLoadout->GetCharacter();

			if (auto Controller = NativePawn->GetController(); Controller && Character)
			{
				static auto CosmeticLoadoutBCOffset = Controller->GetOffset("CosmeticLoadoutBC", false);
				if (CosmeticLoadoutBCOffset != -1)
					Controller->GetPtr<FFortAthenaLoadout>(CosmeticLoadoutBCOffset)->GetCharacter() = Character;
			}

			if (auto PawnLoadout = NativePawn->GetCosmeticLoadout(); PawnLoadout && Character)
			{
				PawnLoadout->GetCharacter() = Character;

				static auto OnRepLoadoutFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerPawn.OnRep_CustomizationLoadout");
				if (OnRepLoadoutFn)
					NativePawn->ProcessEvent(OnRepLoadoutFn);
			}

			BattleLab::WriteCharacterData(NativePawn->GetPlayerState(),
				BattleLab::CollectCosmeticParts(Character ? Character : BotLoadout->GetCharacter(), bLibraryPick ? nullptr : BotLoadout->GetBackpack()));

			// Diagnostics: the bot's resulting inventory, weapon and brain.
			auto Controller = NativePawn->GetController();
			std::string Items;
			int ItemCount = -1;

			if (Controller)
			{
				static auto InventoryOffset = Controller->GetOffset("Inventory", false);
				auto Inventory = InventoryOffset != -1 ? Controller->Get<AFortInventory*>(InventoryOffset) : nullptr;

				if (Inventory)
				{
					auto& Instances = Inventory->GetItemList().GetItemInstances();
					ItemCount = Instances.Num();
					for (int i = 0; i < Instances.Num(); ++i)
						if (auto Instance = Instances.at(i))
							Items += Instance->GetItemEntry()->GetItemDefinition()->GetName() + " ";
				}
			}

			static auto BrainOffset = Controller ? Controller->GetOffset("BrainComponent", false) : -1;
			auto Brain = Controller && BrainOffset != -1 ? Controller->Get(BrainOffset) : nullptr;
			auto Weapon = NativePawn->GetCurrentWeapon();

			LOG_INFO(LogBots, "[BattleLabServer] Bot state: controller={} items={} [{}] weapon={} brain={}",
				Controller ? Controller->GetName() : "null", ItemCount, Items, Weapon ? Weapon->GetName() : "none", Brain ? Brain->GetFullName() : "none");

			return NativePawn;
		}
	}

	AActor* SpawnLocator = GetWorld()->SpawnActor<APawn>(APawn::StaticClass(), InSpawnLocation, InSpawnRotation.Quaternion());
	auto PawnClass = InBotData->Get<UClass*>(InBotData->GetOffset("PawnClass"));
	auto Pawn = BotMutator->SpawnBot(PawnClass , SpawnLocator, InSpawnLocation, InSpawnRotation, true);
	auto PC = Pawn->GetController();
	auto PlayerState = Pawn->GetPlayerState();
	auto BotNameSettings = InBotData->Get<UFortBotNameSettings*>(InBotData->GetOffset("BotNameSettings"));
	Pawn->TeleportTo(InSpawnLocation, InSpawnRotation);//idfk????

	auto SkinOverRide = InBotData->GetCharacterCustomization()->GetCustomizationLoadout()->GetCharacter();

	if (SkinOverRide->GetName() == "CID_556_Athena_Commando_F_RebirthDefaultA")
	{
		if (InBotData->GetFullName().contains("Alter"))
		{
			SkinOverRide = LoadObject<UFortItemDefinition>("/Game/Athena/Items/Cosmetics/Characters/CID_NPC_Athena_Commando_M_HenchmanBad.CID_NPC_Athena_Commando_M_HenchmanBad");
		}
		else if (InBotData->GetFullName().contains("Ego"))
		{
			SkinOverRide = LoadObject<UFortItemDefinition>("/Game/Athena/Items/Cosmetics/Characters/CID_NPC_Athena_Commando_M_HenchmanGood.CID_NPC_Athena_Commando_M_HenchmanGood");
		}
		else if (InBotData->GetFullName().contains("Box")) //s13
		{
			SkinOverRide = LoadObject<UFortItemDefinition>("/Game/Athena/Items/Cosmetics/Characters/CID_NPC_Athena_Commando_M_Scrapyard.CID_NPC_Athena_Commando_M_Scrapyard");
		}
	}
	else
	{
		if (InBotData->GetFullName().contains("Yatch") && Fortnite_Version >= 12.30 && Fortnite_Version <= 12.61)
		{
			SkinOverRide = LoadObject<UFortItemDefinition>("/Game/Athena/AI/MANG/BotData/BotData_MANG_POI_HDP.BotData_MANG_POI_HDP");
		}
		else
		{
			SkinOverRide = InBotData->GetCharacterCustomization()->GetCustomizationLoadout()->GetCharacter();
		}
	}

	auto HeroDefinition = SkinOverRide->Get(SkinOverRide->GetOffset("HeroDefinition"));
	ApplyHID(Pawn, HeroDefinition, true);

	FString OverrideName = UKismetTextLibrary::Conv_TextToString(BotNameSettings->GetOverrideName());
	*reinterpret_cast<FString*>(__int64(PlayerState) + PlayerState->GetOffset("PlayerNamePrivate")) = OverrideName;
	PC->GetPlayerState()->OnRep_PlayerName();

	return Pawn;
}
