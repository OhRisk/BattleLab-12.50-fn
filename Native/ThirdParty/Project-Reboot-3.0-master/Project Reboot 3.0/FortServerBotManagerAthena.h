#pragma once

#include "Object.h"
#include "FortPlayerPawnAthena.h"
#include "FortAthenaAIBotCustomizationData.h"
#include "FortAthenaMutator_Bots.h"

struct FFortAthenaAIBotRunTimeCustomizationData
{

};

static inline void (*BotManagerSetupStuffIdk)(__int64 BotManaager, __int64 Pawn, __int64 BehaviorTree, __int64 a4, DWORD* SkillLevel, __int64 idk, __int64 StartupInventory, __int64 BotNameSettings, __int64 idk_1, BYTE* CanRespawnOnDeath, unsigned __int8 BitFieldDataThing, BYTE* CustomSquadId, FFortAthenaAIBotRunTimeCustomizationData InRuntimeBotData) = decltype(BotManagerSetupStuffIdk)(__int64(GetModuleHandleW(0)) + 0x19D93F0);

class UFortServerBotManagerAthena : public UObject
{
public:
	static inline AFortPlayerPawnAthena* (*SpawnBotOriginal)(UFortServerBotManagerAthena* BotManager, FVector InSpawnLocation, FRotator InSpawnRotation, UFortAthenaAIBotCustomizationData* InBotData, FFortAthenaAIBotRunTimeCustomizationData& InRuntimeBotData);

	AFortAthenaMutator_Bots*& GetCachedBotMutator()
	{
		static auto CachedBotMutatorOffset = GetOffset("CachedBotMutator");
		return Get<AFortAthenaMutator_Bots*>(CachedBotMutatorOffset);
	}

	// Local 12.50 host: bot data of the SpawnBot call in progress, for the controller setup hooks.
	static inline UFortAthenaAIBotCustomizationData* PendingBotData = nullptr;
	// ...and the character (CID) chosen for it: a random pool pick for CosmeticLibrary bot data.
	static inline UObject* PendingCharacter = nullptr;
	static void EnsureBotHeroType(UObject* Controller);
	// Fills the controller's SlotItems (the AI's weapon view of its inventory), which this client build never writes.
	static void SyncBotSlotItems(class AController* Controller);
	static void TickBots();

	// FortAthenaAIBotController.SwitchTeam (the Friendly Bot Grenade moves its bot onto the thrower's team).
	static inline void (*SwitchTeamOriginal)(UObject* Context, FFrame& Stack, void* Ret) = nullptr;
	static void SwitchTeamHook(UObject* Context, FFrame& Stack, void* Ret);

	static AFortPlayerPawnAthena* SpawnBotHook(UFortServerBotManagerAthena* BotManager, FVector& InSpawnLocation, FRotator& InSpawnRotation, UFortAthenaAIBotCustomizationData* InBotData, FFortAthenaAIBotRunTimeCustomizationData& InRuntimeBotData);
};