#pragma once

// Local 12.50 host, development diagnostics for Bot Grenade AI: every few seconds, one [BotAI] line per bot
// controller (logged only when something other than position changed) with possession, blackboard state,
// path following, a navmesh path test, perception, team and weapon. Read-only apart from the path query.

#include <map>

extern "C" IMAGE_DOS_HEADER __ImageBase;

#include "reboot.h"
#include "BattleLabNav.h"
#include "BattleLabNavProbe.h"
#include "FortGameModeAthena.h"
#include "FortPawn.h"
#include "FortPlayerStateAthena.h"
#include "KismetStringLibrary.h"

namespace BattleLabBotDebug
{
	inline ULONGLONG NextDump = 0;
	inline std::map<std::string, std::string> LastState; // controller name -> last logged state
	inline std::map<std::string, ULONGLONG> LastLogTime;

	// CharacterMovementComponent.CustomMovementMode (movement mode 6 = MOVE_Custom): Fortnite's own sub-mode.
	inline int CustomMovementMode(AActor* Pawn)
	{
		static auto CharacterMovementOffset = Pawn->GetOffset("CharacterMovement", false);
		auto Movement = CharacterMovementOffset != -1 ? Pawn->Get(CharacterMovementOffset) : nullptr;
		static auto CustomOffset = Movement ? Movement->GetOffset("CustomMovementMode", false) : -1;
		return Movement && CustomOffset != -1 ? Movement->Get<uint8_t>(CustomOffset) : -1;
	}

	inline FName Key(const wchar_t* Name) { return UKismetStringLibrary::Conv_StringToName(Name); }

	inline int BBEnum(UObject* BB, const wchar_t* Name)
	{
		static auto Fn = FindObject<UFunction>(L"/Script/AIModule.BlackboardComponent.GetValueAsEnum");
		struct { FName KeyName; uint8_t ReturnValue; } P{ Key(Name), 0xff };
		BB->ProcessEvent(Fn, &P);
		return P.ReturnValue;
	}

	inline UObject* BBObject(UObject* BB, const wchar_t* Name)
	{
		static auto Fn = FindObject<UFunction>(L"/Script/AIModule.BlackboardComponent.GetValueAsObject");
		struct { FName KeyName; UObject* ReturnValue; } P{ Key(Name), nullptr };
		BB->ProcessEvent(Fn, &P);
		return P.ReturnValue;
	}

	inline bool BBBool(UObject* BB, const wchar_t* Name)
	{
		static auto Fn = FindObject<UFunction>(L"/Script/AIModule.BlackboardComponent.GetValueAsBool");
		struct { FName KeyName; bool ReturnValue; } P{ Key(Name), false };
		BB->ProcessEvent(Fn, &P);
		return P.ReturnValue;
	}

	inline FVector BBVector(UObject* BB, const wchar_t* Name)
	{
		static auto Fn = FindObject<UFunction>(L"/Script/AIModule.BlackboardComponent.GetValueAsVector");
		struct { FName KeyName; FVector ReturnValue; } P{ Key(Name), {} };
		BB->ProcessEvent(Fn, &P);
		return P.ReturnValue;
	}

	inline std::string Name(UObject* Object) { return Object ? Object->GetName() : "none"; }

	inline std::string Vec(const FVector& V) { return std::format("({:.0f},{:.0f},{:.0f})", V.X, V.Y, V.Z); }

	// Navmesh path from the bot to Goal, as the bot itself would query it.
	inline std::string PathTest(UObject* Controller, const FVector& Start, const FVector& Goal)
	{
		static auto NavDefault = FindObject(L"/Script/NavigationSystem.Default__NavigationSystemV1");
		static auto FindPathFn = FindObject<UFunction>(L"/Script/NavigationSystem.NavigationSystemV1.FindPathToLocationSynchronously");
		static auto IsValidFn = FindObject<UFunction>(L"/Script/NavigationSystem.NavigationPath.IsValid");
		static auto IsPartialFn = FindObject<UFunction>(L"/Script/NavigationSystem.NavigationPath.IsPartial");
		if (!NavDefault || !FindPathFn || !IsValidFn || !IsPartialFn)
			return "n/a";

		struct { UObject* WorldContextObject; FVector PathStart; FVector PathEnd; UObject* PathfindingContext; UClass* FilterClass; UObject* ReturnValue; }
			P{ Controller, Start, Goal, Controller, nullptr, nullptr };
		NavDefault->ProcessEvent(FindPathFn, &P);
		if (!P.ReturnValue)
			return "null";

		bool Valid = false, Partial = false;
		P.ReturnValue->ProcessEvent(IsValidFn, &Valid);
		P.ReturnValue->ProcessEvent(IsPartialFn, &Partial);
		static auto PathPointsOffset = P.ReturnValue->GetOffset("PathPoints", false);
		int Points = PathPointsOffset != -1 ? P.ReturnValue->Get<TArray<FVector>>(PathPointsOffset).Num() : -1;
		return std::format("{}{} {}pts", Valid ? "valid" : "INVALID", Partial ? "(partial)" : "", Points);
	}

	inline std::string Perceived(UObject* Controller)
	{
		static auto PerceptionOffset = Controller->GetOffset("PerceptionComponent", false);
		auto Perception = PerceptionOffset != -1 ? Controller->Get(PerceptionOffset) : nullptr;
		static auto KnownFn = FindObject<UFunction>(L"/Script/AIModule.AIPerceptionComponent.GetKnownPerceivedActors");
		if (!Perception || !KnownFn)
			return "no perception";

		struct { UClass* SenseToUse; TArray<AActor*> OutActors; } P{};
		Perception->ProcessEvent(KnownFn, &P);
		std::string Out = std::to_string(P.OutActors.Num()) + "[";
		for (int i = 0; i < P.OutActors.Num() && i < 4; ++i)
			Out += Name(P.OutActors.at(i)) + " ";
		P.OutActors.Free();
		return Out + "]";
	}

	// Team index, squad, team info actor and whether that team info lists the player state as a member.
	inline std::string TeamOf(UObject* PlayerState)
	{
		if (!PlayerState)
			return "no playerstate";

		auto Athena = (AFortPlayerStateAthena*)PlayerState;
		static auto PlayerTeamOffset = PlayerState->GetOffset("PlayerTeam", false);
		auto TeamInfo = PlayerTeamOffset != -1 ? PlayerState->Get(PlayerTeamOffset) : nullptr;
		std::string Members = "?";
		if (TeamInfo)
		{
			static auto TeamMembersOffset = TeamInfo->GetOffset("TeamMembers", false);
			if (TeamMembersOffset != -1)
			{
				auto& List = TeamInfo->Get<TArray<UObject*>>(TeamMembersOffset);
				bool Listed = false;
				auto Owner = ((AActor*)PlayerState)->GetOwner();
				for (int i = 0; i < List.Num(); ++i)
					Listed |= List.at(i) == Owner || List.at(i) == PlayerState;
				Members = std::format("{} members, listed={}", List.Num(), Listed);
			}
		}
		// GenericTeamId (AI team attitude) wherever it is reflected: player state, its owner (controller), pawn.
		std::string Generic;
		auto Owner = ((AActor*)PlayerState)->GetOwner();
		for (UObject* Object : { PlayerState, (UObject*)Owner, Owner ? (UObject*)((AController*)Owner)->GetPawn() : nullptr })
			if (Object)
				if (auto Offset = Object->GetOffset("GenericTeamId", false); Offset != -1)
					Generic += std::format(" {}.GenericTeamId={}", Object->ClassPrivate->GetName(), Object->Get<uint8_t>(Offset));
		return std::format("team={} squad={} teaminfo={} ({}){}", Athena->GetTeamIndex(), Athena->GetSquadId(), Name(TeamInfo), Members, Generic);
	}

	// Perception-related components on an actor: class, vtable RVA and the +0x1c0 pointer that
	// Fortnite's perception component (vtable containing +0x164d160) requires to be its PerceptionManager.
	inline std::string PerceptionComponents(AActor* Actor)
	{
		static auto GetComponentsFn = FindObject<UFunction>(L"/Script/Engine.Actor.K2_GetComponentsByClass");
		static auto ActorComponentClass = FindObject<UClass>(L"/Script/Engine.ActorComponent");
		if (!Actor || !GetComponentsFn || !ActorComponentClass)
			return "";

		struct { UClass* ComponentClass; TArray<UObject*> ReturnValue; } P{ ActorComponentClass, {} };
		Actor->ProcessEvent(GetComponentsFn, &P);
		auto Base = (uint64)GetModuleHandleW(nullptr);
		std::string Out;
		for (int i = 0; i < P.ReturnValue.Num(); ++i)
		{
			auto Component = P.ReturnValue.at(i);
			auto ClassName = Component ? Component->ClassPrivate->GetName() : "";
			if (ClassName.find("Perception") == std::string::npos)
				continue;
			Out += std::format(" {}(vt+{:x} @1c0={:x})", ClassName, *(uint64*)Component - Base, *(uint64*)((uint8_t*)Component + 0x1c0));
		}
		P.ReturnValue.Free();
		return Out;
	}

	// IGenericTeamAgentInterface vtable: [0] destructor, [1] _getUObject, [2] SetGenericTeamId,
	// [3] GetGenericTeamId (FGenericTeamId has constructors: returned through a hidden pointer),
	// [4] GetTeamAttitudeTowards.
	inline std::string TeamAgent(UObject* Object, AActor* Other)
	{
		static auto InterfaceClass = FindObject<UClass>(L"/Script/AIModule.GenericTeamAgentInterface");
		if (!Object || !InterfaceClass)
			return "n/a";
		auto Interface = (void**)Object->GetInterfaceAddress(InterfaceClass);
		if (!Interface)
			return "no interface";

		auto Base = (uint64)GetModuleHandleW(nullptr);
		auto Vt = *(uint64**)Interface;
		auto GetTeam = (uint8_t* (*)(void*, uint8_t*))Vt[3];
		auto Attitude = (uint8_t(*)(void*, AActor*))Vt[4];
		uint8_t TeamId = 0xee;
		GetTeam(Interface, &TeamId);
		std::string Out = std::format("id={} (get {:x} attitude {:x})", TeamId, Vt[3] - Base, Vt[4] - Base);
		if (Other)
			Out += std::format(" attitude->{}={}", Other->GetName(), Attitude(Interface, Other));
		return Out;
	}

	// Reflected properties of Object's class chain with offsets in [Low, High), with their first byte.
	inline std::string PropertiesAround(UObject* Object, int Low, int High)
	{
		std::string Out;
		for (auto Class = Object ? Object->ClassPrivate : nullptr; Class; Class = *(UClass**)(__int64(Class) + Offsets::SuperStruct))
			for (void* Property = *(void**)(__int64(Class) + Offsets::Children); Property; Property = GetNext(Property))
			{
				int Offset = *(int*)(__int64(Property) + Offsets::Offset_Internal);
				if (Offset >= Low && Offset < High)
					Out += std::format(" {}@{:x}={:x}", GetFNameOfProp(Property)->ToString(), Offset, *((uint8_t*)Object + Offset));
			}
		return Out;
	}

	// Reflected properties whose name contains Needle, shown as pointer and (if an array) element count.
	inline std::string PropertiesNamed(UObject* Object, const std::string& Needle)
	{
		std::string Out;
		for (auto Class = Object ? Object->ClassPrivate : nullptr; Class; Class = *(UClass**)(__int64(Class) + Offsets::SuperStruct))
			for (void* Property = *(void**)(__int64(Class) + Offsets::Children); Property; Property = GetNext(Property))
			{
				auto PropName = GetFNameOfProp(Property)->ToString();
				if (PropName.find(Needle) == std::string::npos)
					continue;
				int Offset = *(int*)(__int64(Property) + Offsets::Offset_Internal);
				auto Raw = (uint8_t*)Object + Offset;
				Out += std::format(" {}@{:x}=[{:x} n={}]", PropName, Offset, *(uint64*)Raw, *(int*)(Raw + 8));
			}
		return Out;
	}

	// Owned gameplay tags (IGameplayTagAssetInterface) as text.
	inline std::string OwnedTags(AActor* Actor)
	{
		// As the native faction check (+0x1eb7b90) does: IGameplayTagAssetInterface vtable +0x10 fills an
		// FGameplayTagContainer { TArray<FGameplayTag> GameplayTags; TArray<FGameplayTag> ParentTags; }.
		static auto InterfaceClass = FindObject<UClass>(L"/Script/GameplayTags.GameplayTagAssetInterface");
		auto Interface = Actor && InterfaceClass ? (void**)Actor->GetInterfaceAddress(InterfaceClass) : nullptr;
		if (!Interface)
			return "n/a";

		struct { TArray<FName> GameplayTags; TArray<FName> ParentTags; } Container{};
		(*(void(**)(void*, void*))(*(uint8_t**)Interface + 0x10))(Interface, &Container);
		std::string Out;
		for (int i = 0; i < Container.GameplayTags.Num(); ++i)
			Out += Container.GameplayTags.at(i).ToString() + " ";
		Container.GameplayTags.Free();
		Container.ParentTags.Free();
		return Out;
	}

	inline void DumpBot(UObject* Controller)
	{
		static auto PawnOffset = Controller->GetOffset("Pawn", false);
		auto Pawn = PawnOffset != -1 ? (AFortPawn*)Controller->Get(PawnOffset) : nullptr;
		if (!Pawn)
			return;

		static auto PawnControllerOffset = Pawn->GetOffset("Controller", false);
		bool Possessed = PawnControllerOffset != -1 && Pawn->Get(PawnControllerOffset) == Controller;

		static auto BlackboardOffset = Controller->GetOffset("Blackboard", false);
		static auto BrainOffset = Controller->GetOffset("BrainComponent", false);
		auto BB = BlackboardOffset != -1 ? Controller->Get(BlackboardOffset) : nullptr;
		auto Brain = BrainOffset != -1 ? Controller->Get(BrainOffset) : nullptr;

		static auto GetMoveStatusFn = FindObject<UFunction>(L"/Script/AIModule.AIController.GetMoveStatus");
		static auto GetFocusActorFn = FindObject<UFunction>(L"/Script/AIModule.AIController.GetFocusActor");
		uint8_t MoveStatus = 0xff;
		UObject* Focus = nullptr;
		if (GetMoveStatusFn) Controller->ProcessEvent(GetMoveStatusFn, &MoveStatus);
		if (GetFocusActorFn) Controller->ProcessEvent(GetFocusActorFn, &Focus);

		static auto CharacterMovementOffset = Pawn->GetOffset("CharacterMovement", false);
		auto Movement = CharacterMovementOffset != -1 ? Pawn->Get(CharacterMovementOffset) : nullptr;
		static auto MovementModeOffset = Movement ? Movement->GetOffset("MovementMode", false) : -1;
		int MovementMode = Movement && MovementModeOffset != -1 ? Movement->Get<uint8_t>(MovementModeOffset) : -1;

		auto PlayerState = Cast<AFortPlayerStateAthena>(((AController*)Controller)->GetPlayerState());
		auto Weapon = Pawn->GetCurrentWeapon();
		static auto AmmoCountOffset = Weapon ? Weapon->GetOffset("AmmoCount", false) : -1;

		auto Location = Pawn->GetActorLocation();

		std::string State = std::format("possessed={} brain={} bb={} move={} mode={} focus={} team={} squad={} weapon={} ammo={} perceived={}",
			Possessed, Brain ? "yes" : "none", BB ? "yes" : "none", MoveStatus, MovementMode, Name(Focus),
			PlayerState ? PlayerState->GetTeamIndex() : -1, PlayerState ? PlayerState->GetSquadId() : -1,
			Name(Weapon), Weapon && AmmoCountOffset != -1 ? Weapon->Get<int>(AmmoCountOffset) : -1, Perceived(Controller));

		if (BB)
		{
			State += std::format(" | LoS={} desiredWeapon={}", BBBool(BB, L"AIEvaluator_Global_HasLoSOnThreat"), Name(BBObject(BB, L"AIEvaluator_Global_DesiredWeapon")));
			// SlotItems (controller +0x360, 6 x FFortBotInventoryInfo: item +8, category +0x11) and threat list (+0x470/+0x478, current +0x458).
			{
				auto Raw = (uint8_t*)Controller;
				std::string Cache;
				for (int i = 0; i < 6; ++i)
				{
					auto Slot = Raw + 0x360 + i * 0x18;
					auto SlotWeapon = *(UObject**)(Slot + 8);
					if (SlotWeapon)
						Cache += std::format("{}:{} ", SlotWeapon->GetName(), *(Slot + 0x11));
				}
				int ThreatIndex = *(int*)(Raw + 0x458), ThreatNum = *(int*)(Raw + 0x478);
				int ThreatType = -1;
				if (ThreatIndex >= 0 && ThreatIndex < ThreatNum)
					ThreatType = *(*(uint8_t**)(Raw + 0x470) + ThreatIndex * 0xc8 + 0x50);
				State += std::format(" | weaponCache=[{}] threats={} current={} type={}", Cache, ThreatNum, ThreatIndex, ThreatType);
			}
			State += std::format(" | landed={} alert={} target={} phase={} beacon={}/{} patrol={} poi={} loot={} range={} melee={} patrolDest={}",
				BBBool(BB, L"AIEvaluator_Global_HasEverJumpedFromBusAndLandedKey"), BBEnum(BB, L"AIEvaluator_Global_AlertLevel"),
				Name(BBObject(BB, L"AIEvaluator_Global_TargetActor")), BBEnum(BB, L"AIEvaluator_Global_GamePhase"),
				BBEnum(BB, L"AIEvaluator_ReachBeacon_ExecutionStatus"), Name(BBObject(BB, L"AIEvaluator_ReachBeacon_Target")),
				BBEnum(BB, L"AIEvaluator_Patrol_ExecutionStatus"), BBEnum(BB, L"AIEvaluator_POINavigation_ExecutionStatus"),
				BBEnum(BB, L"AIEvaluator_Loot_ExecutionStatus"), BBEnum(BB, L"AIEvaluator_RangeAttack_ExecutionStatus"),
				BBEnum(BB, L"AIEvaluator_MeleeAttack_ExecutionStatus"), Vec(BBVector(BB, L"AIEvaluator_Patrol_Destination")));
		}

		// Also every 30 s while unchanged, so a bot that stops moving still shows up with its position.
		auto& Last = LastState[Controller->GetName()];
		auto& LastLogged = LastLogTime[Controller->GetName()];
		if (State == Last && GetTickCount64() - LastLogged < 30000)
			return;

		bool First = Last.empty();
		Last = State;
		LastLogged = GetTickCount64();
		if (!First)
			State += std::format(" | custom={}", CustomMovementMode(Pawn));

		std::string Paths;
		if (First)
		{
			static auto GetPlayerPawnFn = FindObject<UFunction>(L"/Script/Engine.GameplayStatics.GetPlayerPawn");
			static auto StaticsDefault = FindObject(L"/Script/Engine.Default__GameplayStatics");
			struct { UObject* WorldContextObject; int PlayerIndex; UObject* ReturnValue; } Player{ Controller, 0, nullptr };
			if (GetPlayerPawnFn && StaticsDefault)
				StaticsDefault->ProcessEvent(GetPlayerPawnFn, &Player);

			Paths = " | path+8m=" + PathTest(Controller, Location, Location + FVector{ 800, 0, 0 });
			Paths += " | bot " + TeamOf(PlayerState);

			// Team agent ids and attitude towards another bot pawn (the first other one found).
			AActor* OtherPawn = nullptr;
			for (auto Other : GetAllObjectsOfClass(FindObject<UClass>(L"/Script/FortniteGame.FortAthenaAIBotController")))
				if (Other != Controller && !Other->GetName().starts_with("Default__") && ((AController*)Other)->GetPawn())
				{
					OtherPawn = ((AController*)Other)->GetPawn();
					break;
				}
			Paths += " | teamagent controller " + TeamAgent(Controller, OtherPawn) + " pawn " + TeamAgent(Pawn, OtherPawn);
			if (auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode()))
				if (auto GameState = Cast<AFortGameStateAthena>(GameMode->GetGameState()))
					Paths += std::format(" gamestatePhase={}", (int)GameState->GetGamePhase());
			if (auto PlayerPawn = (APawn*)Player.ReturnValue)
			{
				Paths += " path->player=" + PathTest(Controller, Location, PlayerPawn->GetActorLocation());
				Paths += " | player " + TeamOf(PlayerPawn->GetPlayerState());
				Paths += " | attitude to player: controller " + TeamAgent(Controller, PlayerPawn);
			}
		}

		LOG_INFO(LogBots, "[BotAI] {} at {} {}{}", Controller->GetName(), Vec(Location), State, Paths);
	}

	// Dev-only headless self-test: when bot-selftest.flag sits next to the host DLL, spawn two Bot Grenade bots
	// (BD_FrenchYedoc_JW) on the navmesh through the same SpawnBot UFunction the grenade blueprint calls.
	inline void SelfTest()
	{
		static bool bDone = false;
		if (bDone || !BattleLabNav::bNavmeshBuilt)
			return;
		bDone = true;

		wchar_t Path[MAX_PATH]{};
		GetModuleFileNameW((HMODULE)&__ImageBase, Path, MAX_PATH);
		std::wstring Flag = Path;
		Flag = Flag.substr(0, Flag.find_last_of(L'\\') + 1) + L"bot-selftest.flag";
		if (GetFileAttributesW(Flag.c_str()) == INVALID_FILE_ATTRIBUTES)
			return;



		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		auto BotManager = GameMode ? GameMode->GetServerBotManager() : nullptr;
		static auto SpawnBotFn = FindObject<UFunction>(L"/Script/FortniteGame.FortServerBotManagerAthena.SpawnBot");
		auto BotData = LoadObject<UObject>(L"/Game/Athena/Items/Gameplay/FrenchYedoc/BD_FrenchYedoc_JW.BD_FrenchYedoc_JW", FindObject<UClass>(L"/Script/FortniteGame.FortAthenaAIBotCustomizationData"));
		if (!BotManager || !SpawnBotFn || !BotData)
		{
			LOG_WARN(LogBots, "[BotAI] Self-test: manager {} fn {} botdata {}", __int64(BotManager), __int64(SpawnBotFn), __int64(BotData));
			return;
		}

		static auto LocationOffset = SpawnBotFn->GetOffsetFunc("InSpawnLocation");
		static auto BotDataOffset = SpawnBotFn->GetOffsetFunc("InBotData");
		static auto ReturnOffset = SpawnBotFn->GetOffsetFunc("ReturnValue");
		static auto SetValueAsEnumFn = FindObject<UFunction>(L"/Script/AIModule.BlackboardComponent.SetValueAsEnum");
		for (int i = 0; i < 2; ++i)
		{
			alignas(16) uint8_t Params[0x400]{};
			*(FVector*)(Params + LocationOffset) = BattleLabNav::NavigableSpot + FVector{ 0, 600.f * i, 150.f };
			*(UObject**)(Params + BotDataOffset) = BotData;
			BotManager->ProcessEvent(SpawnBotFn, Params);

			// Without a player the match never starts and the game phase stays Setup, which keeps
			// BT_Phoebe_Gameplay ("In Gameplay?": GamePhase >= Aircraft) off. Self-test bots only: SafeZones.
			auto Pawn = *(APawn**)(Params + ReturnOffset);
			auto Controller = Pawn ? Pawn->GetController() : nullptr;
			static auto BlackboardOffset = Controller ? Controller->GetOffset("Blackboard", false) : -1;
			auto BB = Controller && BlackboardOffset != -1 ? Controller->Get(BlackboardOffset) : nullptr;
			// The bot controller's own game-phase-changed handler (+0x167c5f0: blackboard key and cached +0x8f8).
			static const uint8_t PhasePrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0xB6, 0xFA };
			static auto OnGamePhaseChanged = BattleLabNav::CheckedNative<void(*)(UObject*, uint8_t)>(0x167c5f0, PhasePrologue, "BotController OnGamePhaseChanged");
			if (Controller && OnGamePhaseChanged)
				OnGamePhaseChanged(Controller, 4);
			else if (BB && SetValueAsEnumFn)
			{
				struct { FName KeyName; uint8_t EnumValue; } P{ Key(L"AIEvaluator_Global_GamePhase"), 4 };
				BB->ProcessEvent(SetValueAsEnumFn, &P);
			}

			// Second bot onto another team, through the same SwitchTeam the Friendly grenade blueprint calls
			// (the first keeps its spawn team, like an Enemy grenade bot), so the pair should fight.
			static auto SwitchTeamFn = FindObject<UFunction>(L"/Script/FortniteGame.FortAthenaAIBotController.SwitchTeam");
			if (i == 1 && Controller && SwitchTeamFn)
			{
				uint8_t NewTeam[0x10]{ 3 };
				Controller->ProcessEvent(SwitchTeamFn, NewTeam);
			}
		}

		LOG_INFO(LogBots, "[BotAI] Self-test: spawned 2 bots at {} (bot GamePhase set to SafeZones, second bot switched to team 3)", Vec(BattleLabNav::NavigableSpot));
	}

	inline void Tick()
	{
		SelfTest();
		BattleLabNavProbe::Tick();

		if (GetTickCount64() < NextDump)
			return;
		NextDump = GetTickCount64() + 3000;

		static auto BotControllerClass = FindObject<UClass>(L"/Script/FortniteGame.FortAthenaAIBotController");
		if (!BotControllerClass)
			return;

		for (auto Controller : GetAllObjectsOfClass(BotControllerClass))
			if (Controller && !Controller->GetName().starts_with("Default__"))
				DumpBot(Controller);
	}
}
