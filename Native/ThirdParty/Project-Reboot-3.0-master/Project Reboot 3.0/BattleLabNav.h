#pragma once

// Local 12.50 host: navigation for the game's own Battle Lab bot AI.
//
// The client paks do not contain the Battle Royale navmesh (a server-only export), so every movement evaluator
// of the bot AI reported "condition failed: NavData" and bots only turned in place. The level
// Apollo_Nav_Gameplay still provides the island's NavMeshBoundsVolume, and Fortnite's own AthenaNavSystem /
// AthenaNavMesh can generate the navmesh at runtime; this host only removes what the baked-navmesh (client)
// configuration blocks:
//   1. AthenaNavMesh may not be spawned in client context (NeedsLoadForClient is a `return false` stub).
//   2. The map's NullNavSysConfig leaves the system without nav data, octree geometry or auto rebuilds.
//   3. The octree lock and the initial build lock stay held.
//   4. The navigation octree's fixed root bounds do not cover the island (see CreateNavOctreeHook).
// Then the whole island is built once (about 12 s, blocking) shortly after the host is READY. The nav data is
// Dynamic, so later changes (player builds, destroyed walls) rebuild locally.

#include "reboot.h"
#include "GameplayStatics.h"

namespace BattleLabNav
{
	inline bool bHostReady = false;

	inline UClass* FindClass(const wchar_t* Path)
	{
		return FindObject<UClass>(Path);
	}

	inline UObject* GetNavSys()
	{
		auto World = GetWorld();
		static auto NavSysOffset = World->GetOffset("NavigationSystem", false);
		return NavSysOffset != -1 ? World->Get<UObject*>(NavSysOffset) : nullptr;
	}

	inline UObject* GetMainNavData(UObject* NavSys)
	{
		static auto MainNavDataOffset = NavSys->GetOffset("MainNavData", false);
		return MainNavDataOffset != -1 ? NavSys->Get<UObject*>(MainNavDataOffset) : nullptr;
	}

	inline int GetNavDataCount(UObject* NavSys)
	{
		static auto NavDataSetOffset = NavSys->GetOffset("NavDataSet", false);
		return NavDataSetOffset != -1 ? NavSys->Get<TArray<UObject*>>(NavDataSetOffset).Num() : 0;
	}

	inline void SetBool(UObject* Object, const char* Name, bool Value)
	{
		if (auto Property = Object->GetProperty(Name, false))
			Object->SetBitfieldValue(Object->GetOffset(Name), GetFieldMask(Property), Value);
		else
			LOG_WARN(LogBots, "[BattleLabNav] {} missing on {}", Name, Object->GetName());
	}

	inline UObject* FindClassDefault(const char* ClassName)
	{
		static auto ClassClass = FindClass(L"/Script/CoreUObject.Class");
		for (auto Class : GetAllObjectsOfClass(ClassClass))
			if (Class->GetName() == ClassName)
				return ((UClass*)Class)->CreateDefaultObject();
		return nullptr;
	}

	// Calls a native engine function by RVA after checking its first bytes, so a different build fails safely.
	template <typename Fn, size_t N>
	inline Fn CheckedNative(uint64 Rva, const uint8_t (&Prologue)[N], const char* Name)
	{
		auto Address = (uint8_t*)GetModuleHandleW(0) + Rva;
		if (memcmp(Address, Prologue, N) == 0)
			return (Fn)Address;
		LOG_ERROR(LogBots, "[BattleLabNav] {} prologue mismatch at +0x{:x}; skipped", Name, Rva);
		return nullptr;
	}

	// In this client build SpawnActor's context check reduces to the template's NeedsLoadForClient (vtable +0xd0).
	// For server-only actors that is the shared `return false` stub at +0x6879c0, so SpawnActor refuses them
	// ("Unable to spawn class 'AthenaNavMesh' due to client/server context"). The template is the class default
	// object: give only that object a vtable copy whose NeedsLoadForClient returns true.
	inline void AllowSpawnInClientContext(UObject* Default)
	{
		static constexpr size_t VTableEntries = 0x400;
		static bool (*NeedsLoadForClientTrue)(UObject*) = [](UObject*) { return true; };

		auto& VTable = *(uint64**)Default;
		auto Original = VTable[0xd0 / 8] - __int64(GetModuleHandleW(0));
		if (Original != 0x6879c0)
		{
			LOG_INFO(LogBots, "[BattleLabNav] {} NeedsLoadForClient +0x{:x} left unchanged", Default->GetName(), Original);
			return;
		}

		auto Copy = new uint64[VTableEntries];
		memcpy(Copy, VTable, VTableEntries * sizeof(uint64));
		Copy[0xd0 / 8] = uint64(NeedsLoadForClientTrue);
		VTable = Copy;
		LOG_INFO(LogBots, "[BattleLabNav] {} may now be spawned on this host", Default->GetName());
	}

	// Static meshes and navigation. UStaticMeshComponent::IsNavigationRelevant (primary vtable +0x3e0, +0x40fc6e0) is
	//   StaticMesh && StaticMesh->NavCollision (+0x130) && UPrimitiveComponent::IsNavigationRelevant (+0x40a7120),
	// and its answer is cached in UActorComponent::bNavigationRelevant (+0x86 mask 4) when the component registers. The cooker
	// builds a mesh's NavCollision exactly when bHasNavigationData is set and it has a BodySetup (UStaticMesh::CreateNavCollision),
	// but it is a server-only subobject and absent from the client paks (e.g. S_Athena_WaterWall_1x2 cooks only its BodySetup and
	// FortStaticMeshUserData). On this host every static mesh therefore reports "not navigation relevant": POI walls, cliffs,
	// rocks and props never reach the navigation octree and the runtime navmesh runs straight through them, so bots path into
	// walls and jump against them for good. The rest of the component's navigation code is intact (without NavCollision,
	// geometry export falls back to the BodySetup's collision, as the engine does for meshes that have none).
	// Before the map loads, give the classes that use this implementation one that treats "would have NavCollision on the
	// server" (bHasNavigationData and a BodySetup) like the NavCollision itself, and otherwise runs the game's own code.
	namespace NavRelevance
	{
		constexpr uint64 StaticMeshComponentRva = 0x40fc6e0;
		constexpr uint64 PrimitiveComponentRva = 0x40a7120;
		constexpr int PrimarySlot = 0x3e0 / 8;

		inline int StaticMeshOffset = -1;
		inline int NavCollisionOffset = -1, BodySetupOffset = -1;
		inline int HasNavigationDataOffset = -1; inline uint8_t HasNavigationDataMask = 0;
		inline bool (*PrimitiveIsNavigationRelevant)(UObject*) = nullptr;

		inline bool StaticMeshIsNavigationRelevant(UObject* Component)
		{
			auto Mesh = *(uint8_t**)((uint8_t*)Component + StaticMeshOffset);
			if (!Mesh)
				return false;
			bool bHasNavCollision = *(UObject**)(Mesh + NavCollisionOffset) ||
				((Mesh[HasNavigationDataOffset] & HasNavigationDataMask) && *(UObject**)(Mesh + BodySetupOffset));
			return bHasNavCollision && PrimitiveIsNavigationRelevant(Component);
		}

		// The same function reached through INavRelevantInterface (vtable +0x48, `this` = the interface inside the component):
		// the engine's entry is an adjustor thunk `sub rcx, InterfaceOffset; jmp +0x40fc6e0`. RegisterNavOctreeElement asks here.
		constexpr int InterfaceSlot = 0x48 / 8;
		constexpr int InterfaceOffset = 0x1f0;
		inline bool StaticMeshIsNavigationRelevantInterface(uint8_t* Interface)
		{
			return StaticMeshIsNavigationRelevant((UObject*)(Interface - InterfaceOffset));
		}

		inline bool IsThunkTo(uint64 Entry, uint64 Target)
		{
			auto Code = (const uint8_t*)Entry;
			static const uint8_t SubRcx[] = { 0x48, 0x81, 0xE9, InterfaceOffset & 0xff, InterfaceOffset >> 8, 0x00, 0x00, 0xE9 };
			return memcmp(Code, SubRcx, sizeof(SubRcx)) == 0 && Entry + 12 + *(int32_t*)(Code + 8) == Target;
		}

		inline bool PatchSlot(uint64& Slot, uint64 Replacement)
		{
			DWORD Old = 0;
			if (!VirtualProtect(&Slot, sizeof(Slot), PAGE_READWRITE, &Old))
				return false;
			Slot = Replacement;
			VirtualProtect(&Slot, sizeof(Slot), Old, &Old);
			return true;
		}

		inline bool IsChildOf(UObject* Class, UObject* Parent)
		{
			for (auto S = Class; S; S = *(UObject**)(__int64(S) + Offsets::SuperStruct))
				if (S == Parent)
					return true;
			return false;
		}

		inline int PropertyOffset(UObject* Struct, const char* Name, uint8_t* Mask = nullptr)
		{
			for (auto S = Struct; S; S = *(UObject**)(__int64(S) + Offsets::SuperStruct))
				for (void* Property = *(void**)(__int64(S) + Offsets::Children); Property; Property = GetNext(Property))
					if (GetFNameOfProp(Property)->ToString() == Name)
					{
						if (Mask)
							*Mask = GetFieldMask(Property);
						return *(int*)(__int64(Property) + Offsets::Offset_Internal);
					}
			return -1;
		}

		// Every native class derived from UStaticMeshComponent whose slot still holds the engine's implementation (blueprint
		// component classes share their native parent's vtable).
		inline void Restore()
		{
			static const uint8_t StaticMeshPrologue[] = { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x81, 0x20, 0x04, 0x00, 0x00, 0x48, 0x85, 0xC0 };
			static const uint8_t PrimitivePrologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xF6, 0x81, 0x86, 0x00, 0x00, 0x00, 0x08 };
			auto StaticMeshComponent = FindObject<UClass>(L"/Script/Engine.StaticMeshComponent");
			auto StaticMesh = FindObject<UClass>(L"/Script/Engine.StaticMesh");
			PrimitiveIsNavigationRelevant = CheckedNative<bool (*)(UObject*)>(PrimitiveComponentRva, PrimitivePrologue, "UPrimitiveComponent::IsNavigationRelevant");
			bool bEngineCode = CheckedNative<void*>(StaticMeshComponentRva, StaticMeshPrologue, "UStaticMeshComponent::IsNavigationRelevant") != nullptr;
			StaticMeshOffset = StaticMeshComponent ? PropertyOffset(StaticMeshComponent, "StaticMesh") : -1;
			NavCollisionOffset = StaticMesh ? PropertyOffset(StaticMesh, "NavCollision") : -1;
			BodySetupOffset = StaticMesh ? PropertyOffset(StaticMesh, "BodySetup") : -1;
			HasNavigationDataOffset = StaticMesh ? PropertyOffset(StaticMesh, "bHasNavigationData", &HasNavigationDataMask) : -1;
			// The engine code reads the mesh at +0x420 and its NavCollision at +0x130.
			if (!PrimitiveIsNavigationRelevant || !bEngineCode || StaticMeshOffset != 0x420 || NavCollisionOffset != 0x130 || BodySetupOffset == -1 ||
				HasNavigationDataOffset == -1 || !HasNavigationDataMask)
			{
				LOG_ERROR(LogBots, "[BattleLabNav] Static mesh navigation relevance not restored (layout mesh +0x{:x} navcollision +0x{:x} bodysetup +0x{:x} navdata +0x{:x}); "
					"static meshes stay out of the navmesh", StaticMeshOffset, NavCollisionOffset, BodySetupOffset, HasNavigationDataOffset);
				return;
			}

			auto Base = (uint64)GetModuleHandleW(nullptr);
			static auto ClassClass = FindClass(L"/Script/CoreUObject.Class");
			static auto InterfaceClass = FindObject<UClass>(L"/Script/Engine.NavRelevantInterface");
			std::string Patched, Own;
			for (auto Class : GetAllObjectsOfClass(ClassClass))
			{
				if (!IsChildOf(Class, StaticMeshComponent))
					continue;
				auto Default = ((UClass*)Class)->CreateDefaultObject();
				auto Interface = Default && InterfaceClass ? (uint8_t*)Default->GetInterfaceAddress(InterfaceClass) : nullptr;
				if (!Interface || Interface - (uint8_t*)Default != InterfaceOffset)
					continue;

				auto& Primary = (*(uint64**)Default)[PrimarySlot];
				auto& ViaInterface = (*(uint64**)Interface)[InterfaceSlot];
				bool bPrimary = Primary - Base == StaticMeshComponentRva, bInterface = IsThunkTo(ViaInterface, Base + StaticMeshComponentRva);
				if (!bPrimary && !bInterface)
				{
					if (Primary != uint64(&StaticMeshIsNavigationRelevant))
						Own += std::format(" {}(+0x{:x}/+0x{:x})", Class->GetName(), Primary - Base, ViaInterface - Base);
					continue;
				}
				bool bOk = (!bPrimary || PatchSlot(Primary, uint64(&StaticMeshIsNavigationRelevant))) &&
					(!bInterface || PatchSlot(ViaInterface, uint64(&StaticMeshIsNavigationRelevantInterface)));
				Patched += std::format(" {}({}{}{})", Class->GetName(), bPrimary ? "component" : "", bPrimary && bInterface ? "+" : "", bInterface ? "interface" : "") +
					(bOk ? "" : "(FAILED)");
			}

			LOG_INFO(LogBots, "[BattleLabNav] Static mesh navigation relevance restored for:{}", Patched);
			if (!Own.empty())
				LOG_INFO(LogBots, "[BattleLabNav] Static mesh classes with their own navigation relevance (unchanged):{}", Own);
		}
	}

	// At module load, before the map: new nav data copies these class defaults.
	inline void PrepareNavigationClasses()
	{
		NavRelevance::Restore();

		for (auto NavDataClass : { "AthenaNavMesh", "FortNavMesh", "RecastNavMesh" })
		{
			auto Default = FindClassDefault(NavDataClass);
			if (!Default)
				continue;

			// ERuntimeGenerationType::Dynamic: only runtime-generating nav data gets a tile generator.
			auto GenerationOffset = Default->GetOffset("RuntimeGeneration", false);
			if (GenerationOffset != -1)
				Default->Get<uint8_t>(GenerationOffset) = 2;

			if (strcmp(NavDataClass, "AthenaNavMesh") == 0)
			{
				AllowSpawnInClientContext(Default);
				// ANavigationData::PostInitializeComponents discards client-side nav data without it.
				SetBool(Default, "bNetLoadOnClient", true);
			}
		}
	}

	struct FNavBox { FVector Min; FVector Max; uint8_t IsValid; }; // FBox
	static_assert(sizeof(FNavBox) == 0x1C);

	// The island's NavMeshBoundsVolume(s): sent through UNavigationSystemV1::OnNavigationBoundsUpdated. With no nav
	// data this spawns it (synchronously); afterwards it refreshes the generator's bounds. Returns the largest box.
	inline FNavBox UpdateNavigationBounds(UObject* NavSys)
	{
		static auto OnBoundsUpdatedFn = FindObject<UFunction>(L"/Script/NavigationSystem.NavigationSystemV1.OnNavigationBoundsUpdated");
		static auto GetActorBoundsFn = FindObject<UFunction>(L"/Script/Engine.Actor.GetActorBounds");
		static auto VolumeClass = FindClass(L"/Script/NavigationSystem.NavMeshBoundsVolume");
		FNavBox Largest{};

		if (!OnBoundsUpdatedFn || !GetActorBoundsFn || !VolumeClass)
		{
			LOG_ERROR(LogBots, "[BattleLabNav] Navigation bounds functions missing");
			return Largest;
		}

		for (auto Volume : GetAllObjectsOfClass(VolumeClass))
		{
			if (Volume->GetName().starts_with("Default__"))
				continue;

			struct { bool bOnlyColliding; FVector Origin; FVector Extent; bool bIncludeChildren; } Bounds{ false };
			Volume->ProcessEvent(GetActorBoundsFn, &Bounds);
			LOG_INFO(LogBots, "[BattleLabNav] Bounds volume {} origin ({:.0f},{:.0f},{:.0f}) extent ({:.0f},{:.0f},{:.0f})", Volume->GetName(),
				Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z, Bounds.Extent.X, Bounds.Extent.Y, Bounds.Extent.Z);
			if (Bounds.Extent.X * Bounds.Extent.Y > (Largest.Max.X - Largest.Min.X) * (Largest.Max.Y - Largest.Min.Y) / 4)
				Largest = { { Bounds.Origin.X - Bounds.Extent.X, Bounds.Origin.Y - Bounds.Extent.Y, Bounds.Origin.Z - Bounds.Extent.Z },
					{ Bounds.Origin.X + Bounds.Extent.X, Bounds.Origin.Y + Bounds.Extent.Y, Bounds.Origin.Z + Bounds.Extent.Z }, 1 };

			UObject* Params = Volume;
			NavSys->ProcessEvent(OnBoundsUpdatedFn, &Params);
		}

		return Largest;
	}

	// Step 1 (READY + 5 s): the navigation system has no nav data; make it spawn AthenaNavMesh.
	inline bool CreateNavData()
	{
		auto NavSys = GetNavSys();
		if (!NavSys)
		{
			LOG_ERROR(LogBots, "[BattleLabNav] No navigation system; bots cannot move");
			return false;
		}

		SetBool(NavSys, "bAutoCreateNavigationData", true);
		SetBool(NavSys, "bGenerateNavigationOnlyAroundNavigationInvokers", false);
		// FortNavSystem ships configured for a baked navmesh: no automatic rebuilds.
		SetBool(NavSys, "bAllowAutoRebuild", true);
		SetBool(NavSys, "bRebuildOnInitialUnlock", true);

		if (!GetMainNavData(NavSys))
			UpdateNavigationBounds(NavSys);
		return true;
	}

	// K2_ProjectPointToNavigation: is there navmesh at Location?
	inline bool IsOnNavmesh(const FVector& Location, float ExtentXY = 500.f)
	{
		static auto ProjectFn = FindObject<UFunction>(L"/Script/NavigationSystem.NavigationSystemV1.K2_ProjectPointToNavigation");
		static auto NavSysDefault = FindObject(L"/Script/NavigationSystem.Default__NavigationSystemV1");
		if (!ProjectFn || !NavSysDefault)
			return false;

		struct { UObject* WorldContext; FVector Point; FVector Projected; UObject* NavData; UObject* FilterClass; FVector QueryExtent; bool ReturnValue; } Params{
			GetWorld(), Location, {}, nullptr, nullptr, { ExtentXY, ExtentXY, 2000.f }, false };
		NavSysDefault->ProcessEvent(ProjectFn, &Params);
		return Params.ReturnValue;
	}

	// First sample spot found on the navmesh (diagnostics / bot self-test); valid once bNavmeshBuilt.
	inline FVector NavigableSpot{};
	inline bool bNavmeshBuilt = false;

	// Share of a 32x32 grid over the island's navigation bounds that has navmesh (build health check).
	inline int CoveragePercent(const FNavBox& Bounds)
	{
		constexpr int N = 32;
		int Covered = 0;
		for (int Row = 0; Row < N; ++Row)
			for (int Col = 0; Col < N; ++Col)
			{
				FVector Point{ Bounds.Min.X + (Row + 0.5f) * (Bounds.Max.X - Bounds.Min.X) / N, Bounds.Min.Y + (Col + 0.5f) * (Bounds.Max.Y - Bounds.Min.Y) / N, 0.f };
				for (float Z : { -3000.f, 0.f, 3000.f, 6000.f })
				{
					Point.Z = Z;
					if (IsOnNavmesh(Point)) { ++Covered; break; }
				}
			}
		return Covered * 100 / (N * N);
	}

	// The navigation octree (FNavigationOctree at NavSys+0x298) is created by the virtual at vtable +0x328 with the
	// engine's fixed root bounds (center 0, extent 64000), but the Apollo island spans about +-130000 (the spawn
	// island +-200000). Populated from the whole island, nearly every element ends up in a node that does not
	// contain it (measured: 36602 of 36946), so a tile's geometry query, which descends by position, misses most
	// of the geometry under it: the runtime navmesh only covered a lattice of strips along the octree's node
	// boundaries (a quarter of the island; none at Dirty Docks). Epic's servers load a baked navmesh and never
	// generate it from this octree.
	// Keep the octree flat instead: a node only splits while its extent exceeds MinLeafExtent, so raising
	// MinLeafExtent to the root extent keeps every element in the root, where each query tests every element's
	// bounds directly (about 37k box tests per tile).
	inline void (*CreateNavOctreeOriginal)(UObject*, void*, void*, void*) = nullptr;

	inline void CreateNavOctreeHook(UObject* NavSys, void* A2, void* A3, void* A4)
	{
		CreateNavOctreeOriginal(NavSys, A2, A3, A4);

		auto Octree = NavSys->Get<uint8_t*>(0x298);
		if (!Octree)
			return;
		// Root context: Extent (FVector4) +0x400, ChildExtent +0x410, MinLeafExtent +0x420 (64000 * 0.53125^12);
		// root node element count +0x398 (must still be empty).
		auto RootExtent = (float*)(Octree + 0x400);
		auto& MinLeafExtent = *(float*)(Octree + 0x420);
		if (RootExtent[0] != 64000.f || *(float*)(Octree + 0x410) != 34000.f || fabsf(MinLeafExtent - 32.34f) > 0.1f || *(int*)(Octree + 0x398) != 0)
		{
			LOG_WARN(LogBots, "[BattleLabNav] Unexpected navigation octree layout (extent {} min leaf {}); left unchanged", RootExtent[0], MinLeafExtent);
			return;
		}
		MinLeafExtent = RootExtent[0];
		LOG_INFO(LogBots, "[BattleLabNav] Navigation octree kept flat (min leaf extent {:.0f})", MinLeafExtent);
	}

	// Gives only this navigation system object a vtable copy whose CreateNavOctree is the hook above.
	inline void HookCreateNavOctree(UObject* NavSys)
	{
		static constexpr size_t VTableEntries = 0x400;
		static const uint8_t CreatePrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x55 };
		auto& VTable = *(uint64**)NavSys;
		if (VTable[0x328 / 8] == uint64(&CreateNavOctreeHook))
			return;
		if (!CheckedNative<void*>(VTable[0x328 / 8] - uint64(GetModuleHandleW(0)), CreatePrologue, "CreateNavOctree"))
			return;

		CreateNavOctreeOriginal = decltype(CreateNavOctreeOriginal)(VTable[0x328 / 8]);
		auto Copy = new uint64[VTableEntries];
		memcpy(Copy, VTable, VTableEntries * sizeof(uint64));
		Copy[0x328 / 8] = uint64(&CreateNavOctreeHook);
		VTable = Copy;
	}

	// Step 2 (once the nav data is registered, a tick later): geometry, unlock, build, verify.
	inline void BuildNavmesh()
	{
		auto NavSys = GetNavSys();
		auto NavData = GetMainNavData(NavSys);

		// AthenaNavSystem::AddLevelToOctree (+0x16fd9c0) filters actors by NavOctTreeInclusionBounds (actor pointers, not
		// boxes) only while +0x1908 bit 0 is set; it is clear, so every level takes the engine's own path.
		auto Island = UpdateNavigationBounds(NavSys);

		// UNavigationSystemV1::ConditionalPopulateNavOctree rebuilds the octree from every loaded level, but skips
		// all levels while the octree lock (bit 0 of +0x2f8) is held; the static configuration keeps it set.
		static const uint8_t PopulatePrologue[] = { 0x48, 0x8B, 0xC4, 0x53, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x89, 0x68, 0x08, 0x33, 0xDB };
		if (auto Populate = CheckedNative<void (*)(UObject*)>(0x48894d0, PopulatePrologue, "ConditionalPopulateNavOctree"))
		{
			NavSys->Get<uint8_t>(0x2f8) &= ~1;
			HookCreateNavOctree(NavSys);
			Populate(NavSys);
		}

		// Build locks (NavBuildingLockFlags, +0x398; the initial lock 0x4 is still held): release them with
		// UNavigationSystemV1::RemoveNavigationBuildLock(Flags, ELockRemovalRebuildAction::Rebuild).
		static const uint8_t UnlockPrologue[] = { 0x48, 0x8B, 0xC4, 0x53, 0x55, 0x56, 0x48, 0x83, 0xEC, 0x60, 0x4C, 0x89, 0x60, 0x10, 0x0F, 0xB6 };
		auto& LockFlags = NavSys->Get<uint8_t>(0x398);
		if (LockFlags)
			if (auto RemoveLock = CheckedNative<void (*)(UObject*, uint8_t, int)>(0x48a5160, UnlockPrologue, "RemoveNavigationBuildLock"))
				RemoveLock(NavSys, LockFlags, 0);

		// UNavigationSystemV1::Build: builds every tile of the island and waits for completion.
		static const uint8_t BuildPrologue[] = { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x80, 0x3D };
		auto Start = GetTickCount64();
		if (auto Build = CheckedNative<void (*)(UObject*)>(0x4886b60, BuildPrologue, "UNavigationSystemV1::Build"))
			Build(NavSys);
		auto BuildMs = GetTickCount64() - Start;

		// Verify on a few loot containers spread over the island.
		int Checked = 0, Navigable = 0;
		static auto ContainerClass = FindClass(L"/Script/FortniteGame.BuildingContainer");
		if (ContainerClass)
		{
			auto Containers = UGameplayStatics::GetAllActorsOfClass(GetWorld(), ContainerClass);
			for (int i = 0; i < Containers.Num() && Checked < 8; i += (std::max)(1, Containers.Num() / 8), ++Checked)
			{
				auto Location = Containers.at(i)->GetActorLocation();
				if (IsOnNavmesh(Location))
				{
					if (!Navigable++)
						NavigableSpot = Location;
				}
				else
					LOG_INFO(LogBots, "[BattleLabNav]   no navmesh at {} ({:.0f}, {:.0f}, {:.0f})", Containers.at(i)->GetFullName(), Location.X, Location.Y, Location.Z);
			}
			Containers.Free();
		}

		bNavmeshBuilt = Navigable > 0;
		LOG_INFO(LogBots, "[BattleLabNav] Navmesh {} built in {} ms (locks 0x{:x}); {}/{} sample loot spots navigable, {}% of the navigation bounds covered",
			NavData ? NavData->GetName() : "null", BuildMs, LockFlags, Navigable, Checked, Island.IsValid ? CoveragePercent(Island) : 0);
	}

	// Called from the server tick.
	inline void Tick()
	{
		static ULONGLONG StartAt = 0;
		static int Step = 0;

		if (!bHostReady || Step == 2)
			return;

		auto Now = GetTickCount64();
		if (!StartAt)
			StartAt = Now + 5000;
		if (Now < StartAt)
			return;

		if (Step == 0)
		{
			Step = CreateNavData() ? 1 : 2;
			return;
		}

		// The nav data spawned by the bounds update is registered on a later navigation tick.
		auto NavSys = GetNavSys();
		if (GetNavDataCount(NavSys) == 0 || !GetMainNavData(NavSys))
		{
			if (Now > StartAt + 30000)
			{
				LOG_ERROR(LogBots, "[BattleLabNav] Nav data was not created; bots cannot move");
				Step = 2;
			}
			return;
		}

		Step = 2;
		BuildNavmesh();
	}
}
