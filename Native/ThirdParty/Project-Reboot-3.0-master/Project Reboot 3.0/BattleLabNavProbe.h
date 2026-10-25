#pragma once

// Local 12.50 host, development diagnostics: headless navigation probe (nav-probe.flag next to the host DLL).
// Live matches left bots pressing into walls for hours ("moving", walking/falling in turn) with their patrol
// destination on the far side. For those spots this follows the navmesh path the bot would take, traces each
// path segment for the first blocking hit, and reports what was hit: the actor's and component's navigation
// settings and whether the navigation octree holds an element with the actor's bounds. It also dumps the
// reflection of Fortnite's building navigation types. Read-only apart from the path queries.

#include "reboot.h"
#include "BattleLabNav.h"
#include "EngineTypes.h"
#include "FortGameModeAthena.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace BattleLabNavProbe
{
	// Live stuck spots (bot location -> its patrol destination), host log of 2026-09-27.
	inline const FVector StuckSpots[][2] = {
		{ { 91958, 99101, -1373 }, { 90000, 99000, -1528 } },
		{ { 86919, 103734, -1438 }, { 87000, 99000, -1528 } },
		{ { 77809, 104242, -1459 }, { 78000, 102000, -1592 } },
		{ { 74717, 95585, -1404 }, { 72072, 93000, -2920 } },
		{ { 106493, -85577, -3755 }, { 108000, -90000, -3064 } },
		{ { 6482, 111922, -3777 }, { 6048, 105000, -3000 } },
		{ { 15500, 112500, -2995 }, { 9000, 105000, -2861 } },
		{ { 22833, 105014, -2973 }, { 15000, 105000, -3003 } },
		{ { -36139, 96053, 4439 }, { -36000, 102000, 4200 } },
		{ { -85786, 35794, 2136 }, { -84079, 39031, 2495 } },
		{ { -104610, 31076, 1079 }, { -111062, 29891, 4672 } },
		{ { 17366, -29181, -1097 }, { 18000, -21000, -1528 } },
	};

	inline std::string V(const FVector& Vec) { return std::format("({:.0f},{:.0f},{:.0f})", Vec.X, Vec.Y, Vec.Z); }

	// FField: FFieldClass* at +8, whose first member is its FName.
	inline std::string FieldType(void* Property)
	{
		auto FieldClass = *(FName**)((uint8_t*)Property + 8);
		return FieldClass ? FieldClass->ToString() : "?";
	}

	inline std::string ValueOf(uint8_t* Base, void* Property)
	{
		auto Type = FieldType(Property);
		auto Raw = Base + *(int*)((uint8_t*)Property + Offsets::Offset_Internal);
		if (Type == "BoolProperty")
			return (*Raw & GetFieldMask(Property)) ? "true" : "false";
		if (Type == "ObjectProperty" || Type == "ClassProperty" || Type == "WeakObjectProperty")
			return Type == "WeakObjectProperty" ? std::format("weak {:x}", *(uint64*)Raw) : (*(UObject**)Raw ? (*(UObject**)Raw)->GetName() : "null");
		if (Type == "ByteProperty" || Type == "EnumProperty")
			return std::to_string(*Raw);
		if (Type == "IntProperty")
			return std::to_string(*(int*)Raw);
		if (Type == "FloatProperty")
			return std::format("{:.2f}", *(float*)Raw);
		if (Type == "NameProperty")
			return ((FName*)Raw)->ToString();
		if (Type == "ArrayProperty")
			return std::format("n={}", *(int*)(Raw + 8));
		if (Type == "StructProperty")
		{
			// FStructProperty::Struct (+0x78): show the struct's type and its members, one level deep.
			auto Struct = *(UObject**)((uint8_t*)Property + 0x78);
			std::string Out = Struct ? Struct->GetName() + "{" : "{";
			for (void* Inner = Struct ? *(void**)(__int64(Struct) + Offsets::Children) : nullptr; Inner; Inner = GetNext(Inner))
			{
				auto InnerType = FieldType(Inner);
				auto InnerRaw = Raw + *(int*)((uint8_t*)Inner + Offsets::Offset_Internal);
				Out += " " + GetFNameOfProp(Inner)->ToString() + "=";
				if (InnerType == "ArrayProperty")
					Out += std::format("n={}", *(int*)(InnerRaw + 8));
				else if (InnerType == "ObjectProperty")
					Out += *(UObject**)InnerRaw ? (*(UObject**)InnerRaw)->GetName() : "null";
				else if (InnerType == "ByteProperty" || InnerType == "BoolProperty" || InnerType == "EnumProperty")
					Out += std::to_string(*InnerRaw);
				else if (InnerType == "IntProperty")
					Out += std::to_string(*(int*)InnerRaw);
				else
					Out += InnerType;
			}
			return Out + " }";
		}
		std::string Hex;
		for (int i = 0; i < 16; ++i)
			Hex += std::format("{:02x}", Raw[i]);
		return Hex;
	}

	// Every reflected property of a struct/class chain (optionally only names containing one of Needles), with the
	// values read from Instance when given.
	inline std::string Describe(UObject* Struct, uint8_t* Instance, std::initializer_list<const char*> Needles = {})
	{
		std::string Out;
		for (auto S = Struct; S; S = *(UObject**)(__int64(S) + Offsets::SuperStruct))
			for (void* Property = *(void**)(__int64(S) + Offsets::Children); Property; Property = GetNext(Property))
			{
				auto Name = GetFNameOfProp(Property)->ToString();
				bool bWanted = Needles.size() == 0;
				for (auto Needle : Needles)
					bWanted |= Name.find(Needle) != std::string::npos;
				if (!bWanted)
					continue;
				Out += std::format(" {}:{}@{:x}", Name, FieldType(Property), *(int*)((uint8_t*)Property + Offsets::Offset_Internal));
				if (Instance)
					Out += "=" + ValueOf(Instance, Property);
			}
		return Out;
	}

	inline void DumpType(const wchar_t* Path)
	{
		auto Type = FindObject<UObject>(Path);
		if (!Type)
		{
			LOG_INFO(LogBots, "[NavProbe] {} not found", std::string(Path, Path + wcslen(Path)));
			return;
		}
		if (Type->ClassPrivate->GetName() == "Enum")
		{
			auto& Names = *(TArray<TPair<FName, int64>>*)((uint8_t*)Type + 0x40);
			std::string Out;
			for (int i = 0; i < Names.Num(); ++i)
				Out += std::format(" {}={}", Names.At(i).Key().ToString(), Names.At(i).Value());
			LOG_INFO(LogBots, "[NavProbe] {}:{}", Type->GetName(), Out);
			return;
		}
		LOG_INFO(LogBots, "[NavProbe] {}:{}", Type->GetName(), Describe(Type, nullptr));
	}

	inline void DumpDefault(const wchar_t* Path, std::initializer_list<const char*> Needles = {})
	{
		auto Default = FindObject<UObject>(Path);
		if (Default)
			LOG_INFO(LogBots, "[NavProbe] {}:{}", Default->GetName(), Describe(Default->ClassPrivate, (uint8_t*)Default, Needles));
	}

	struct FOctreeScan { int Num = -1; int Stride = 0; int Containing = 0; int Matching = 0; float Closest = -1; };

	// Root node of the (flat) navigation octree: element TArray heap data +0x390, count +0x398 (see BattleLabNav.h).
	// Element = FBoxCenterAndExtent (FVector4 center, FVector4 extent) + data; the stride is inferred from the data.
	// Raw reads only, guarded (no C++ objects needing unwinding in this function).
	inline bool ScanOctree(uint8_t* Octree, const float* Point, const float* Center, const float* Extent, FOctreeScan* Out)
	{
		__try
		{
			int Num = *(int*)(Octree + 0x398);
			auto Data = *(uint8_t**)(Octree + 0x390);
			Out->Num = Num;
			if (Num <= 0 || !Data)
				return false;

			static const int Strides[] = { 0x30, 0x38, 0x40, 0x48, 0x50, 0x60 };
			for (int s = 0; s < 6 && !Out->Stride; ++s)
			{
				int Sample = Num < 256 ? Num : 256, Good = 0;
				for (int i = 0; i < Sample; ++i)
				{
					auto F = (float*)(Data + (size_t)i * Strides[s]);
					bool bOk = true;
					for (int a = 0; a < 3; ++a)
						bOk &= F[a] > -1e6f && F[a] < 1e6f && F[4 + a] >= 0.5f && F[4 + a] < 1e6f;
					Good += bOk;
				}
				if (Good * 10 >= Sample * 9)
					Out->Stride = Strides[s];
			}
			if (!Out->Stride)
				return false;

			float Best = 1e30f;
			for (int i = 0; i < Num; ++i)
			{
				auto F = (float*)(Data + (size_t)i * Out->Stride);
				bool bContains = true, bMatches = true;
				float Diff = 0;
				for (int a = 0; a < 3; ++a)
				{
					bContains &= fabsf(Point[a] - F[a]) <= F[4 + a] + 1.f;
					float D = fabsf(Center[a] - F[a]) + fabsf(Extent[a] - F[4 + a]);
					bMatches &= D < 50.f;
					Diff += D;
				}
				Out->Containing += bContains;
				Out->Matching += bMatches;
				if (bContains && Diff < Best)
					Best = Diff;
			}
			Out->Closest = Best < 1e30f ? Best : -1;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	inline std::vector<FVector> FindPath(const FVector& Start, const FVector& Goal, std::string& Status)
	{
		static auto NavDefault = FindObject(L"/Script/NavigationSystem.Default__NavigationSystemV1");
		static auto FindPathFn = FindObject<UFunction>(L"/Script/NavigationSystem.NavigationSystemV1.FindPathToLocationSynchronously");
		static auto IsValidFn = FindObject<UFunction>(L"/Script/NavigationSystem.NavigationPath.IsValid");
		static auto IsPartialFn = FindObject<UFunction>(L"/Script/NavigationSystem.NavigationPath.IsPartial");
		std::vector<FVector> Points;

		struct { UObject* WorldContextObject; FVector PathStart; FVector PathEnd; UObject* PathfindingContext; UClass* FilterClass; UObject* ReturnValue; }
			P{ GetWorld(), Start, Goal, nullptr, nullptr, nullptr };
		NavDefault->ProcessEvent(FindPathFn, &P);
		if (!P.ReturnValue)
		{
			Status = "null";
			return Points;
		}

		bool Valid = false, Partial = false;
		P.ReturnValue->ProcessEvent(IsValidFn, &Valid);
		P.ReturnValue->ProcessEvent(IsPartialFn, &Partial);
		static auto PathPointsOffset = P.ReturnValue->GetOffset("PathPoints", false);
		if (PathPointsOffset != -1)
		{
			auto& Array = P.ReturnValue->Get<TArray<FVector>>(PathPointsOffset);
			for (int i = 0; i < Array.Num(); ++i)
				Points.push_back(Array.at(i));
		}
		Status = std::format("{}{} {}pts", Valid ? "valid" : "INVALID", Partial ? "(partial)" : "", Points.size());
		return Points;
	}

	struct FProbeHit { bool bHit = false; AActor* Actor = nullptr; UObject* Component = nullptr; FVector Point{}; FVector Normal{}; };

	// Visibility line trace (TraceTypeQuery1), broken into actor / component with GameplayStatics.BreakHitResult.
	inline FProbeHit Trace(const FVector& Start, const FVector& End)
	{
		static auto TraceFn = FindObject<UFunction>(L"/Script/Engine.KismetSystemLibrary.LineTraceSingle");
		static auto TraceDefault = FindObject(L"/Script/Engine.Default__KismetSystemLibrary");
		static auto BreakFn = FindObject<UFunction>(L"/Script/Engine.GameplayStatics.BreakHitResult");
		static auto StaticsDefault = FindObject(L"/Script/Engine.Default__GameplayStatics");
		FProbeHit Hit;
		if (!TraceFn || !TraceDefault || !BreakFn || !StaticsDefault)
			return Hit;

		alignas(16) uint8_t Params[0x400]{};
		*(UObject**)(Params + TraceFn->GetOffsetFunc("WorldContextObject")) = GetWorld();
		*(FVector*)(Params + TraceFn->GetOffsetFunc("Start")) = Start;
		*(FVector*)(Params + TraceFn->GetOffsetFunc("End")) = End;
		*(Params + TraceFn->GetOffsetFunc("bIgnoreSelf")) = 1;
		TraceDefault->ProcessEvent(TraceFn, Params);
		if (!*(bool*)(Params + TraceFn->GetOffsetFunc("ReturnValue")))
			return Hit;

		alignas(16) uint8_t Break[0x400]{};
		memcpy(Break + BreakFn->GetOffsetFunc("Hit"), Params + TraceFn->GetOffsetFunc("OutHit"), FHitResult::GetStructSize());
		StaticsDefault->ProcessEvent(BreakFn, Break);
		Hit.bHit = true;
		Hit.Actor = *(AActor**)(Break + BreakFn->GetOffsetFunc("HitActor"));
		Hit.Component = *(UObject**)(Break + BreakFn->GetOffsetFunc("HitComponent"));
		Hit.Point = *(FVector*)(Break + BreakFn->GetOffsetFunc("ImpactPoint"));
		Hit.Normal = *(FVector*)(Break + BreakFn->GetOffsetFunc("ImpactNormal"));
		return Hit;
	}

	inline uint8_t& StaticRuntimeNavigation() { return *((uint8_t*)GetModuleHandleW(nullptr) + 0x82bfb24); }
	inline uint8_t& UpdateOctreeOnComponentChange() { return *((uint8_t*)GetModuleHandleW(nullptr) + 0x7d55931); }

	// INavRelevantInterface: +0x18 GetNavigationBounds (FBox out), +0x48 IsNavigationRelevant (what RegisterNavOctreeElement asks).
	inline std::string NavRelevance(AActor* Actor, UObject* Component)
	{
		static auto InterfaceClass = FindObject<UClass>(L"/Script/Engine.NavRelevantInterface");
		auto Interface = Component && InterfaceClass ? (void**)Component->GetInterfaceAddress(InterfaceClass) : nullptr;
		if (!Interface)
			return "component has no INavRelevantInterface";
		auto Vt = *(uint64**)Interface;
		BattleLabNav::FNavBox Box{};
		((void (*)(void*, BattleLabNav::FNavBox*))Vt[0x18 / 8])(Interface, &Box);
		bool bRelevant = ((bool (*)(void*))Vt[0x48 / 8])(Interface);
		bool bActorSays = ((bool (*)(AActor*, UObject*))(*(uint64**)Actor)[0x618 / 8])(Actor, Component);
		auto Base = (uint64)GetModuleHandleW(nullptr);

		// Once: the component's vtables and its mesh's navigation data, for static analysis of the stripped functions.
		static bool bVtablesLogged = false;
		if (!bVtablesLogged)
		{
			bVtablesLogged = true;
			std::string Slots;
			for (int i = 0; i < 8; ++i)
				Slots += std::format(" {:x}", Vt[i] - Base);
			static auto StaticMeshOffset = Component->GetOffset("StaticMesh", false);
			auto Mesh = StaticMeshOffset != -1 ? Component->Get(StaticMeshOffset) : nullptr;
			LOG_INFO(LogBots, "[NavProbe] {} vtable {:x}, INavRelevantInterface at +0x{:x} vtable {:x}:{}; mesh {}:{}", Component->ClassPrivate->GetName(),
				*(uint64*)Component - Base, (uint8_t*)Interface - (uint8_t*)Component, (uint64)Vt - Base, Slots, Mesh ? Mesh->GetName() : "none",
				Mesh ? Describe(Mesh->ClassPrivate, (uint8_t*)Mesh, { "Nav", "BodySetup" }) : "");
			LOG_INFO(LogBots, "[NavProbe] {} collision:{}", Component->GetName(), Describe(Component->ClassPrivate, (uint8_t*)Component, { "BodyInstance" }));
			static auto BodyInstanceStruct = FindObject<UObject>(L"/Script/Engine.BodyInstance");
			static auto ResponseStruct = FindObject<UObject>(L"/Script/Engine.CollisionResponse");
			if (BodyInstanceStruct && ResponseStruct)
				LOG_INFO(LogBots, "[NavProbe] BodyInstance:{} | CollisionResponse:{}", Describe(BodyInstanceStruct, nullptr, { "Collision", "Response", "ObjectType" }),
					Describe(ResponseStruct, nullptr));
		}
		return std::format("IsNavigationRelevant={} (vt {:x}) actor.IsComponentRelevantForNavigation={} (vt+0x618 {:x}) navbounds {}..{} valid={}",
			bRelevant, Vt[0x48 / 8] - Base, bActorSays, (*(uint64**)Actor)[0x618 / 8] - Base, V(Box.Min), V(Box.Max), Box.IsValid);
	}

	inline void DescribeHit(const FProbeHit& Hit)
	{
		static auto GetActorBoundsFn = FindObject<UFunction>(L"/Script/Engine.Actor.GetActorBounds");
		std::string Octree = "n/a";
		if (Hit.Actor && GetActorBoundsFn)
		{
			struct { bool bOnlyColliding; FVector Origin; FVector Extent; bool bIncludeChildren; } Bounds{ true };
			Hit.Actor->ProcessEvent(GetActorBoundsFn, &Bounds);
			float Point[3]{ Hit.Point.X, Hit.Point.Y, Hit.Point.Z }, Center[3]{ Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z },
				Extent[3]{ Bounds.Extent.X, Bounds.Extent.Y, Bounds.Extent.Z };
			FOctreeScan Scan;
			auto NavSys = BattleLabNav::GetNavSys();
			auto Tree = NavSys ? NavSys->Get<uint8_t*>(0x298) : nullptr;
			bool bScanned = Tree && ScanOctree(Tree, Point, Center, Extent, &Scan);
			Octree = std::format("{} (elements {}, stride 0x{:x}, containing hit {}, matching actor bounds {}, closest diff {:.0f}); actor bounds {} +- {}",
				bScanned ? "scanned" : "UNREADABLE", Scan.Num, Scan.Stride, Scan.Containing, Scan.Matching, Scan.Closest, V(Bounds.Origin), V(Bounds.Extent));
		}

		LOG_INFO(LogBots, "[NavProbe]     hit {} ({}) component {} ({}) at {} normal ({:.2f},{:.2f},{:.2f})",
			Hit.Actor ? Hit.Actor->GetFullName() : "none", Hit.Actor ? Hit.Actor->ClassPrivate->GetName() : "-",
			Hit.Component ? Hit.Component->GetName() : "none", Hit.Component ? Hit.Component->ClassPrivate->GetName() : "-",
			V(Hit.Point), Hit.Normal.X, Hit.Normal.Y, Hit.Normal.Z);
		LOG_INFO(LogBots, "[NavProbe]     octree: {}", Octree);
		if (Hit.Actor)
		{
			LOG_INFO(LogBots, "[NavProbe]     actor nav:{}", Describe(Hit.Actor->ClassPrivate, (uint8_t*)Hit.Actor, { "Nav", "Obstacle", "Smash", "Walk" }));

			// The actor's level: listed in World->Levels (what ConditionalPopulateNavOctree walks)? Its flags.
			auto Level = Hit.Actor->OuterPrivate;
			static auto LevelsOffset = GetWorld()->GetOffset("Levels", false);
			int Index = -1, Count = -1;
			if (LevelsOffset != -1)
			{
				auto& Levels = GetWorld()->Get<TArray<UObject*>>(LevelsOffset);
				Count = Levels.Num();
				for (int i = 0; i < Levels.Num(); ++i)
					if (Levels.at(i) == Level)
						Index = i;
			}
			LOG_INFO(LogBots, "[NavProbe]     level {} (index {} of {} in World.Levels):{}", Level ? Level->GetFullName() : "none", Index, Count,
				Level ? Describe(Level->ClassPrivate, (uint8_t*)Level, { "Visible", "bIs", "bAlready", "bClient", "Owning" }) : "");
		}
		if (Hit.Component)
			LOG_INFO(LogBots, "[NavProbe]     component nav:{}", Describe(Hit.Component->ClassPrivate, (uint8_t*)Hit.Component,
				{ "Nav", "CanEverAffect", "Obstacle", "Collision", "bFillCollision", "StaticMesh", "bHasCustom" }));
		if (Hit.Actor && Hit.Component)
			LOG_INFO(LogBots, "[NavProbe]     relevance: {}", NavRelevance(Hit.Actor, Hit.Component));
	}

	inline void ProbeSpot(int Index, const FVector& From, const FVector& To)
	{
		std::string Status;
		auto Points = FindPath(From, To, Status);
		std::string List;
		for (auto& Point : Points)
			List += " " + V(Point);
		LOG_INFO(LogBots, "[NavProbe] spot {} {} -> {}: {} navmesh at start {} goal {}:{}", Index, V(From), V(To), Status,
			BattleLabNav::IsOnNavmesh(From, 100.f), BattleLabNav::IsOnNavmesh(To, 100.f), List);

		// Waist height (the capsule's half height above the navmesh), segment by segment.
		for (size_t i = 0; i + 1 < Points.size(); ++i)
		{
			FVector A = Points[i], B = Points[i + 1];
			A.Z += 90.f;
			B.Z += 90.f;
			auto Hit = Trace(A, B);
			if (!Hit.bHit)
				continue;
			LOG_INFO(LogBots, "[NavProbe]   segment {} {} -> {} blocked:", i, V(A), V(B));
			DescribeHit(Hit);
			break;
		}
	}

	inline bool FlagFile(const wchar_t* Name)
	{
		wchar_t Path[MAX_PATH]{};
		GetModuleFileNameW((HMODULE)&__ImageBase, Path, MAX_PATH);
		std::wstring Flag = Path;
		Flag = Flag.substr(0, Flag.find_last_of(L'\\') + 1) + Name;
		return GetFileAttributesW(Flag.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	// Dev soak test (bot-soak.flag): one Bot Grenade bot (BD_FrenchYedoc_JW, the grenade's SpawnBot UFunction) at each live stuck
	// spot, all on their spawn team so they patrol instead of fighting; the [BotAI] dumps then show whether they get stuck.
	inline void SoakTick()
	{
		static bool bDone = false;
		static ULONGLONG StartAt = 0;
		if (bDone || !BattleLabNav::bNavmeshBuilt)
			return;
		static bool bEnabled = FlagFile(L"bot-soak.flag");
		if (!bEnabled)
		{
			bDone = true;
			return;
		}
		// Wait for the bot mutator's patrol locations (BasePlacementQuery), a few seconds after the build.
		if (!StartAt)
			StartAt = GetTickCount64() + 8000;
		if (GetTickCount64() < StartAt)
			return;
		bDone = true;

		auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
		auto BotManager = GameMode ? GameMode->GetServerBotManager() : nullptr;
		static auto SpawnBotFn = FindObject<UFunction>(L"/Script/FortniteGame.FortServerBotManagerAthena.SpawnBot");
		auto BotData = LoadObject<UObject>(L"/Game/Athena/Items/Gameplay/FrenchYedoc/BD_FrenchYedoc_JW.BD_FrenchYedoc_JW", FindObject<UClass>(L"/Script/FortniteGame.FortAthenaAIBotCustomizationData"));
		if (!BotManager || !SpawnBotFn || !BotData)
			return;

		static const uint8_t PhasePrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0xB6, 0xFA };
		static auto OnGamePhaseChanged = BattleLabNav::CheckedNative<void(*)(UObject*, uint8_t)>(0x167c5f0, PhasePrologue, "BotController OnGamePhaseChanged");
		int Spawned = 0;
		for (auto& Spot : StuckSpots)
		{
			alignas(16) uint8_t Params[0x400]{};
			FVector Location = Spot[0];
			Location.Z += 150.f;
			*(FVector*)(Params + SpawnBotFn->GetOffsetFunc("InSpawnLocation")) = Location;
			*(UObject**)(Params + SpawnBotFn->GetOffsetFunc("InBotData")) = BotData;
			BotManager->ProcessEvent(SpawnBotFn, Params);
			auto Pawn = *(APawn**)(Params + SpawnBotFn->GetOffsetFunc("ReturnValue"));
			auto Controller = Pawn ? Pawn->GetController() : nullptr;
			// A headless host never leaves Setup; BT_Phoebe_Gameplay runs from Aircraft on (as in the self-test).
			if (Controller && OnGamePhaseChanged)
				OnGamePhaseChanged(Controller, 4);
			Spawned += Pawn != nullptr;
		}
		LOG_INFO(LogBots, "[NavProbe] soak test: {} bots spawned at the live stuck spots", Spawned);
	}

	// Dev (indicator.flag): reflection of the team indicator system, to find what shows the friendly marker.
	inline void IndicatorDump()
	{
		static bool bDone = false;
		if (bDone || !BattleLabNav::bHostReady)
			return;
		bDone = true;
		if (!FlagFile(L"indicator.flag"))
			return;

		for (auto Type : { L"/Script/FortniteGame.FortIndicatorManager", L"/Script/FortniteGame.FortIndicator", L"/Script/FortniteGame.FortIndicatorComponent",
			L"/Script/FortniteGame.EFortIndicatorState", L"/Script/FortniteUI.AthenaPlayerIndicatorBase", L"/Script/FortniteGame.FortMiniMapTeamIndicators" })
			DumpType(Type);

		// Every class property whose name mentions indicators or team markers.
		static auto ClassClass = FindObject<UClass>(L"/Script/CoreUObject.Class");
		std::string Out;
		for (auto Class : GetAllObjectsOfClass(ClassClass))
			for (void* Property = *(void**)(__int64(Class) + Offsets::Children); Property; Property = GetNext(Property))
			{
				auto Name = GetFNameOfProp(Property)->ToString();
				if (Name.find("Indicator") != std::string::npos || Name.find("NonSquad") != std::string::npos)
					Out += std::format(" {}.{}:{}@{:x}", Class->GetName(), Name, FieldType(Property), *(int*)((uint8_t*)Property + Offsets::Offset_Internal));
			}
		LOG_INFO(LogBots, "[NavProbe] indicator properties:{}", Out);

		// Native entry points (UFunction::Func) of the team-change handlers, for static analysis.
		std::string Funcs;
		auto Base = (uint64)GetModuleHandleW(nullptr);
		for (auto Path : { L"/Script/FortniteGame.FortPlayerStateAthena.OnRep_TeamIndex", L"/Script/FortniteGame.FortPlayerStateAthena.OnRep_SquadId",
			L"/Script/FortniteGame.FortPlayerState.OnRep_PlayerTeam", L"/Script/FortniteGame.FortPlayerState.OnRep_PlayerTeamPrivate",
			L"/Script/FortniteGame.FortPlayerStateAthena.OnRep_ChangeTeamInfo", L"/Script/FortniteGame.FortPlayerStateAthena.OnRep_ReplicatedTeamMemberState",
			L"/Script/FortniteUI.AthenaLocalPlayerViewModel.HandleLocalPlayerChangedTeams", L"/Script/FortniteUI.AthenaLocalPlayerViewModel.HandleViewTargetPlayerStateReplicated",
			L"/Script/FortniteUI.AthenaPlayerIndicatorBase.RefreshCurrentPawn", L"/Script/FortniteGame.FortPlayerPawn.OnRep_PlayerState",
			L"/Script/Engine.Pawn.OnRep_PlayerState" })
			if (auto Fn = FindObject<UFunction>(Path))
				Funcs += std::format(" {}=+0x{:x}", Fn->GetName(), (uint64)Fn->GetFunc() - Base);
		LOG_INFO(LogBots, "[NavProbe] team handler entry points:{}", Funcs);
		for (auto Type : { L"/Script/FortniteUI.AthenaLocalPlayerViewModel", L"/Script/FortniteUI.AthenaPlayerViewModel", L"/Script/FortniteUI.FortPlayerViewModel",
			L"/Script/FortniteUI.AthenaIndicatorLayerBase" })
			DumpType(Type);

		DumpType(L"/Script/FortniteGame.EFortCustomMovement");
		DumpType(L"/Script/FortniteGame.FortKismetLibrary.SpawnProjectile");
		DumpDefault(L"/Script/FortniteGame.Default__FortRuntimeOptions", { "Indicator" });
		for (auto Path : { L"/Script/FortniteGame.Default__FortMovementComp_CharacterAthena", L"/Script/FortniteGame.Default__FortMovementComp_Character" })
			DumpDefault(Path, { "Speed", "Accel", "Braking", "Ignore", "Authoritative", "Correction", "Fly", "Cheat", "Friction", "Sprint" });
		for (auto Path : { L"/Script/Engine.Default__Controller", L"/Script/Engine.Default__Character" })
			DumpDefault(Path, { "Pawn", "CharacterMovement", "PlayerState" });
	}

	// Dev (indicator.flag), once the POIs are loaded: what is at the Steamy Stacks vents, and the replicated team state of a bot
	// switched onto team 3 the way the Friendly Bot Grenade does it (SpawnBot, then SwitchTeam).
	inline void TeamStateDump()
	{
		static int Stage = 0;
		static ULONGLONG At = 0;
		static APawn* Bot = nullptr;
		static bool bEnabled = FlagFile(L"indicator.flag") && FlagFile(L"teamstate.flag");
		if (Stage >= 3 || !bEnabled || !BattleLabNav::bNavmeshBuilt)
			return;
		if (!At)
			At = GetTickCount64() + 8000;
		if (GetTickCount64() < At)
			return;

		if (Stage == 0)
		{
			Stage = 1;
			At = GetTickCount64() + 3000;
			auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
			auto BotManager = GameMode ? GameMode->GetServerBotManager() : nullptr;
			static auto SpawnBotFn = FindObject<UFunction>(L"/Script/FortniteGame.FortServerBotManagerAthena.SpawnBot");
			auto BotData = LoadObject<UObject>(L"/Game/Athena/Items/Gameplay/FrenchYedoc/BD_FrenchYedoc_JW.BD_FrenchYedoc_JW", FindObject<UClass>(L"/Script/FortniteGame.FortAthenaAIBotCustomizationData"));
			static auto SwitchTeamFn = FindObject<UFunction>(L"/Script/FortniteGame.FortAthenaAIBotController.SwitchTeam");
			if (!BotManager || !SpawnBotFn || !BotData || !SwitchTeamFn)
				return;
			alignas(16) uint8_t Params[0x400]{};
			*(FVector*)(Params + SpawnBotFn->GetOffsetFunc("InSpawnLocation")) = BattleLabNav::NavigableSpot;
			*(UObject**)(Params + SpawnBotFn->GetOffsetFunc("InBotData")) = BotData;
			BotManager->ProcessEvent(SpawnBotFn, Params);
			Bot = *(APawn**)(Params + SpawnBotFn->GetOffsetFunc("ReturnValue"));
			if (auto Controller = Bot ? Bot->GetController() : nullptr)
			{
				uint8_t NewTeam[0x10]{ 3 };
				Controller->ProcessEvent(SwitchTeamFn, NewTeam);
			}
			return;
		}

		if (Stage == 1)
		{
			Stage = 2;
			auto PlayerState = Bot ? Bot->GetPlayerState() : nullptr;
			if (!PlayerState)
				return;
			LOG_INFO(LogBots, "[NavProbe] bot player state {}:{}", PlayerState->GetName(), Describe(PlayerState->ClassPrivate, (uint8_t*)PlayerState,
				{ "Team", "Squad", "Party", "Member", "bIsABot", "bIsSpectator", "bOnlySpectator" }));
			static auto PlayerTeamOffset = PlayerState->GetOffset("PlayerTeam", false);
			static auto PlayerTeamPrivateOffset = PlayerState->GetOffset("PlayerTeamPrivate", false);
			for (auto Offset : { PlayerTeamOffset, PlayerTeamPrivateOffset })
				if (auto Info = Offset != -1 ? PlayerState->Get(Offset) : nullptr)
				{
					LOG_INFO(LogBots, "[NavProbe] {} ({}):{}", Info->GetName(), Info->ClassPrivate->GetName(), Describe(Info->ClassPrivate, (uint8_t*)Info,
						{ "Team", "Squad", "Member", "Player", "Rep", "Private", "Info" }));
				}
			if (auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode()))
				if (auto GameState = GameMode->GetGameState())
					LOG_INFO(LogBots, "[NavProbe] game state team data:{}", Describe(GameState->ClassPrivate, (uint8_t*)GameState,
						{ "Member", "Teams", "Squad", "PlayerArray", "Alive" }));
			return;
		}

		Stage = 3;

		// What is at the Steamy Stacks spots where soak-test bots hovered in custom movement 4 (actors whose bounds are
		// within 600 uu), with their navigation-related components.
		static auto GetActorBoundsFn = FindObject<UFunction>(L"/Script/Engine.Actor.GetActorBounds");
		static auto GetComponentsFn = FindObject<UFunction>(L"/Script/Engine.Actor.K2_GetComponentsByClass");
		static auto ComponentClass = FindObject<UClass>(L"/Script/Engine.ActorComponent");
		static auto ActorClass = FindObject<UClass>(L"/Script/Engine.Actor");
		for (FVector Spot : { FVector{ 89949, 98626, -1000 }, FVector{ 90683, 97803, -600 } })
		{
			std::string Near;
			for (auto Object : GetAllObjectsOfClass(ActorClass))
			{
				auto Actor = (AActor*)Object;
				if (Actor->GetName().starts_with("Default__") || !GetActorBoundsFn)
					continue;
				struct { bool bOnlyColliding; FVector Origin; FVector Extent; bool bIncludeChildren; } B{ false };
				Actor->ProcessEvent(GetActorBoundsFn, &B);
				if (fabsf(Spot.X - B.Origin.X) > B.Extent.X + 600 || fabsf(Spot.Y - B.Origin.Y) > B.Extent.Y + 600 || fabsf(Spot.Z - B.Origin.Z) > B.Extent.Z + 1500 ||
					B.Extent.X > 20000)
					continue;
				Near += std::format("\n    {} ({}) at {} +- {}", Actor->GetName(), Actor->ClassPrivate->GetName(), V(B.Origin), V(B.Extent));
				if (Actor->ClassPrivate->GetName().starts_with("BGA_"))
					Near += Describe(Actor->ClassPrivate, (uint8_t*)Actor, { "Nav", "Obstacle", "Bot", "AI", "Affect" });
				struct { UClass* C; TArray<UObject*> R; } P{ ComponentClass, {} };
				Actor->ProcessEvent(GetComponentsFn, &P);
				for (int i = 0; i < P.R.Num(); ++i)
				{
					auto Name = P.R.at(i)->ClassPrivate->GetName();
					if (Name.find("Nav") != std::string::npos || Name.find("Volume") != std::string::npos || Name.find("Box") != std::string::npos ||
						Name.find("Capsule") != std::string::npos || Name.find("Sphere") != std::string::npos)
						Near += std::format(" [{}:{}]", Name, Describe(P.R.at(i)->ClassPrivate, (uint8_t*)P.R.at(i), { "Area", "bCanEverAffect", "bDynamicObstacle" }));
				}
				P.R.Free();
			}
			LOG_INFO(LogBots, "[NavProbe] actors near {}:{}", V(Spot), Near);
		}
	}

	inline void Tick()
	{
		IndicatorDump();
		TeamStateDump();
		SoakTick();

		static int Runs = 0;
		static ULONGLONG NextRun = 0;
		if (Runs >= 2 || !BattleLabNav::bNavmeshBuilt)
			return;

		static bool bEnabled = []
		{
			wchar_t Path[MAX_PATH]{};
			GetModuleFileNameW((HMODULE)&__ImageBase, Path, MAX_PATH);
			std::wstring Flag = Path;
			Flag = Flag.substr(0, Flag.find_last_of(L'\\') + 1) + L"nav-probe.flag";
			return GetFileAttributesW(Flag.c_str()) != INVALID_FILE_ATTRIBUTES;
		}();
		if (!bEnabled)
			return;

		// Once right after the build, once 90 s later (after any streaming and dirty-area rebuilds).
		auto Now = GetTickCount64();
		if (!NextRun)
			NextRun = Now + 2000;
		if (Now < NextRun)
			return;
		NextRun = Now + 90000;
		++Runs;

		LOG_INFO(LogBots, "[NavProbe] run {}", Runs);
		if (Runs == 1)
		{
			for (auto Type : { L"/Script/FortniteGame.BuildingNavObstacle", L"/Script/FortniteGame.BuildingActorNavArea", L"/Script/FortniteGame.EBuildingNavObstacleType",
				L"/Script/FortniteGame.FortNavObstacleComponent" })
				DumpType(Type);
			for (auto Default : { L"/Script/FortniteGame.Default__FortMetaNavArea_Wall", L"/Script/FortniteGame.Default__FortMetaNavArea_Obstacles",
				L"/Script/FortniteGame.Default__FortNavArea_WoodenWall", L"/Script/FortniteGame.Default__FortNavArea_StoneWall",
				L"/Script/FortniteGame.Default__FortNavArea_Obstacle", L"/Script/FortniteGame.Default__FortNavArea_DefaultSmashable" })
				DumpDefault(Default);
			for (auto Default : { L"/Script/FortniteGame.Default__BuildingSMActor", L"/Script/FortniteGame.Default__BuildingWall" })
				DumpDefault(Default, { "Nav", "Obstacle", "Smash" });
			if (auto NavSys = BattleLabNav::GetNavSys())
			{
				auto Base = (uint64)GetModuleHandleW(nullptr);
				auto VTable = *(uint64**)NavSys;
				LOG_INFO(LogBots, "[NavProbe] nav system {} ({}) vtable +0x328 {:x} +0x388 {:x} +0x150 {:x}:{}", NavSys->GetName(), NavSys->ClassPrivate->GetName(),
					VTable[0x328 / 8] - Base, VTable[0x388 / 8] - Base, VTable[0x150 / 8] - Base,
					Describe(NavSys->ClassPrivate, (uint8_t*)NavSys, { "Inclusion", "Octree", "Gathering", "bSkip", "Static", "Lazy", "Agent" }));

				// AthenaNavSystem::AddLevelToOctree filters by the inclusion list only when +0x1908 bit 0 is set; entries are 8 bytes.
				std::string Entries;
				auto& Inclusion = NavSys->Get<TArray<uint64>>(0x18f8);
				for (int i = 0; i < Inclusion.Num(); ++i)
					Entries += std::format(" {:x}", Inclusion.at(i));
				LOG_INFO(LogBots, "[NavProbe] static navigation flag {} update-on-component-change {} inclusion filter bit {} entries:{}",
					StaticRuntimeNavigation(), UpdateOctreeOnComponentChange(), NavSys->Get<uint8_t>(0x1908) & 1, Entries);
			}
			if (auto NavData = BattleLabNav::GetMainNavData(BattleLabNav::GetNavSys()))
				LOG_INFO(LogBots, "[NavProbe] nav data {}:{}", NavData->GetFullName(), Describe(NavData->ClassPrivate, (uint8_t*)NavData,
					{ "Agent", "Area", "Obstacle", "Filter", "CellSize", "CellHeight", "Walkable", "Tile" }));
		}

		int Index = 0;
		for (auto& Spot : StuckSpots)
			ProbeSpot(Index++, Spot[0], Spot[1]);

	}
}
