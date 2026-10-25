#pragma once

// Local preservation host: applies each player's saved Locker loadout. The host runs without MCP
// profiles (Reboot NoMCP), so the loadout is read from the local backend's /battlelab/loadout
// (127.0.0.1 only) and applied through the game's own character-part/cosmetic functions.
// Friends who join over LAN/VPN register their Locker with this backend by player name when they queue.

#include <map>
#include <sstream>

#include "BattleLabNotify.h"
#include "FortPlayerControllerAthena.h"
#include "reboot.h"

namespace BattleLab
{
	inline std::wstring UrlEncode(const std::string& Text)
	{
		static const char* Hex = "0123456789ABCDEF";
		std::wstring Out;
		for (unsigned char Ch : Text)
		{
			if (isalnum(Ch) || Ch == '-' || Ch == '_' || Ch == '.')
				Out += (wchar_t)Ch;
			else
			{
				Out += L'%';
				Out += (wchar_t)Hex[Ch >> 4];
				Out += (wchar_t)Hex[Ch & 15];
			}
		}
		return Out;
	}

	// Slot name (FortAthenaLoadout member) -> loaded item definition, for the named player.
	inline std::map<std::string, UObject*> FetchLoadout(const std::string& PlayerName)
	{
		std::map<std::string, UObject*> Slots;
		std::istringstream Lines(GetFromBackend(L"/battlelab/loadout?player=" + UrlEncode(PlayerName)));

		for (std::string Line; std::getline(Lines, Line);)
		{
			auto Eq = Line.find('=');
			if (Eq == std::string::npos)
				continue;

			std::string Slot = Line.substr(0, Eq);
			std::string AssetPath = Line.substr(Eq + 1);
			std::wstring WideAssetPath(AssetPath.begin(), AssetPath.end());

			static auto ObjectClass = FindObject<UClass>(L"/Script/CoreUObject.Object");
			if (auto Definition = LoadObject<UObject>(WideAssetPath.c_str(), ObjectClass))
				Slots[Slot] = Definition;
			else
				LOG_WARN(LogDev, "[BattleLabServer] Loadout {}: could not load {}", Slot, AssetPath);
		}

		return Slots;
	}

	inline void CopyIntoLoadout(FFortAthenaLoadout* Loadout, const std::map<std::string, UObject*>& Slots)
	{
		if (!Loadout)
			return;

		for (auto& [Slot, Definition] : Slots)
		{
			int Offset = FindOffsetStruct("/Script/FortniteGame.FortAthenaLoadout", Slot, false);
			if (Offset != -1)
				*(UObject**)(__int64(Loadout) + Offset) = Definition;
		}
	}

	// Hero definition -> its specializations' character parts (head, body, hat, face...).
	inline void CollectHeroParts(UObject* HeroDefinition, std::vector<UObject*>& OutParts)
	{
		if (!HeroDefinition)
			return;

		static auto SpecializationsOffset = HeroDefinition->GetOffset("Specializations");
		static auto FortHeroSpecializationClass = FindObject<UClass>(L"/Script/FortniteGame.FortHeroSpecialization");
		static auto CustomCharacterPartClass = FindObject<UClass>(L"/Script/FortniteGame.CustomCharacterPart");
		auto& Specializations = HeroDefinition->Get<TArray<TSoftObjectPtr<UObject>>>(SpecializationsOffset);

		for (int i = 0; i < Specializations.Num(); ++i)
		{
			auto Specialization = Specializations.at(i).Get(FortHeroSpecializationClass, true);
			if (!Specialization)
				continue;

			static auto CharacterPartsOffset = Specialization->GetOffset("CharacterParts");
			auto& Parts = Specialization->Get<TArray<TSoftObjectPtr<UObject>>>(CharacterPartsOffset);

			for (int z = 0; z < Parts.Num(); ++z)
				if (auto Part = Parts.at(z, GetSoftObjectSize()).Get(CustomCharacterPartClass, true))
					OutParts.push_back(Part);
		}
	}

	// CID (+ optional back bling) -> the full part list for that look.
	inline std::vector<UObject*> CollectCosmeticParts(UObject* Character, UObject* Backpack)
	{
		std::vector<UObject*> Parts;

		if (Character)
		{
			static auto HeroDefinitionOffset = Character->GetOffset("HeroDefinition");
			CollectHeroParts(Character->Get(HeroDefinitionOffset), Parts);
		}

		if (Backpack)
		{
			static auto BackpackPartsOffset = Backpack->GetOffset("CharacterParts", false);
			if (BackpackPartsOffset != -1)
			{
				auto& BackpackParts = Backpack->Get<TArray<UObject*>>(BackpackPartsOffset);
				for (int i = 0; i < BackpackParts.Num(); ++i)
					if (auto Part = BackpackParts.at(i))
						Parts.push_back(Part);
			}
		}

		return Parts;
	}

	// 12.50 replicates a character's look only through AFortPlayerState::CharacterData (FCustomCharacterData:
	// Parts[part type] + WasPartReplicatedFlags); the pawn builds itself from it ("Using Character Parts In
	// Player State: YES") and remote clients wait for it ("DoCharacterPartsNeedReplication"). This build has
	// no PlayerState CharacterParts, and the native ApplyCharacterCosmetics routes through the MCP hero, so
	// Reboot's cosmetic helpers never reach the client. Write CharacterData the way the server's own
	// customization does, then replicate it.
	inline bool WriteCharacterData(UObject* PlayerState, const std::vector<UObject*>& Parts)
	{
		static constexpr int NumPartTypes = 6; // EFortCustomPartType: Head, Body, Hat, Backpack, Charm, Face

		static int CharacterDataOffset = -1;
		if (CharacterDataOffset == -1 && PlayerState)
			CharacterDataOffset = PlayerState->GetOffset("CharacterData", false);
		static auto PartsOffset = FindOffsetStruct("/Script/FortniteGame.CustomCharacterData", "Parts", false);
		static auto FlagsOffset = FindOffsetStruct("/Script/FortniteGame.CustomCharacterData", "WasPartReplicatedFlags", false);

		if (!PlayerState || Parts.empty() || CharacterDataOffset == -1 || PartsOffset == -1)
		{
			LOG_WARN(LogDev, "[BattleLabServer] CharacterData not written (parts {}, CharacterData {:x}, Parts {:x})", Parts.size(), CharacterDataOffset, PartsOffset);
			return false;
		}

		auto Data = (uint8_t*)PlayerState + CharacterDataOffset;
		auto Slots = (UObject**)(Data + PartsOffset);
		uint8_t Flags = 0;
		std::string Names;

		for (int i = 0; i < NumPartTypes; ++i)
			Slots[i] = nullptr;

		for (auto Part : Parts)
		{
			static auto PartTypeOffset = Part->GetOffset("CharacterPartType");
			auto Type = Part->Get<uint8_t>(PartTypeOffset);
			if (Type >= NumPartTypes)
				continue;

			Slots[Type] = Part;
			Flags |= 1 << Type;
			Names += Part->GetName() + " ";
		}

		if (FlagsOffset != -1)
			*(Data + FlagsOffset) = Flags;

		static auto OnRepCharacterDataFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerState.OnRep_CharacterData");
		if (OnRepCharacterDataFn)
			PlayerState->ProcessEvent(OnRepCharacterDataFn);

		((AActor*)PlayerState)->ForceNetUpdate();
		LOG_INFO(LogDev, "[BattleLabServer] CharacterData for {}: {}(flags {:x})", PlayerState->GetName(), Names, Flags);
		return true;
	}

	// Before the pawn exists: sets the controller's loadout (pickaxe, glider, contrail are read from it).
	inline void ApplyLoadoutToController(AFortPlayerController* Controller)
	{
		if (!Controller || Controller->GetCosmeticLoadoutOffset() == -1)
			return;

		auto PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());
		auto Slots = FetchLoadout(PlayerState ? PlayerState->GetPlayerName().ToString() : std::string());
		CopyIntoLoadout(Controller->GetCosmeticLoadout(), Slots);

		// The game's own AFortPlayerState::ApplyCharacterCustomization rebuilds the body from the player
		// state's HeroType (default recruit without an MCP profile), overwriting any parts applied directly.
		// Point HeroType at the Locker character's hero so that native refresh applies the right skin.
		auto Character = Slots.count("Character") ? Slots["Character"] : nullptr;
		if (Character && PlayerState)
		{
			static auto HeroDefinitionOffset = Character->GetOffset("HeroDefinition");
			static auto HeroTypeOffset = PlayerState->GetOffset("HeroType", false);
			if (HeroDefinitionOffset != -1 && HeroTypeOffset != -1)
			{
				if (auto HeroDefinition = Character->Get(HeroDefinitionOffset))
					PlayerState->Get(HeroTypeOffset) = HeroDefinition;
			}
		}

		LOG_INFO(LogDev, "[BattleLabServer] Applied {} Locker slot(s) to {}", Slots.size(), Controller->GetName());
	}

	// Every spawn (including respawns): skin and back bling on the new pawn.
	inline void ApplyLoadoutToPawn(AFortPlayerController* Controller, AFortPlayerPawn* Pawn)
	{
		if (!Controller || !Pawn || Controller->GetCosmeticLoadoutOffset() == -1)
			return;

		ApplyLoadoutToController(Controller); // re-fetch: the game may have reset it since join

		auto Loadout = Controller->GetCosmeticLoadout();
		if (!Loadout || !Loadout->GetCharacter())
		{
			LOG_WARN(LogDev, "[BattleLabServer] No Locker character to apply to {}", Pawn->GetName());
			return;
		}

		LOG_INFO(LogDev, "[BattleLabServer] Applying skin {} to {}", Loadout->GetCharacter()->GetName(), Pawn->GetName());

		if (auto PawnLoadout = Pawn->GetCosmeticLoadout())
		{
			CopyStruct(PawnLoadout, Loadout, FFortAthenaLoadout::GetStructSize());

			static auto OnRepLoadoutFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPlayerPawn.OnRep_CustomizationLoadout");
			if (OnRepLoadoutFn)
				Pawn->ProcessEvent(OnRepLoadoutFn);
		}

		WriteCharacterData(Pawn->GetPlayerState(), CollectCosmeticParts(Loadout->GetCharacter(), Loadout->GetBackpack()));
		Pawn->ForceNetUpdate();
	}

	// Re-applying from inside the game's own customization refresh crashes the host (re-entrancy), so the
	// refresh hook only queues the controller; the server tick applies it shortly after, outside the refresh.
	inline std::vector<std::pair<AFortPlayerController*, ULONGLONG>> PendingSkinReapply;

	inline void QueueSkinReapply(AFortPlayerController* Controller)
	{
		PendingSkinReapply.push_back({ Controller, GetTickCount64() + 500 });
	}

	inline void TickSkinReapply()
	{
		auto Now = GetTickCount64();

		for (auto It = PendingSkinReapply.begin(); It != PendingSkinReapply.end();)
		{
			if (Now < It->second)
			{
				++It;
				continue;
			}

			auto Controller = It->first;
			It = PendingSkinReapply.erase(It);

			auto Pawn = Controller && Controller->IsValidLowLevel() ? Cast<AFortPlayerPawn>(Controller->GetPawn()) : nullptr;
			if (Pawn)
			{
				LOG_INFO(LogDev, "[BattleLabServer] Re-applying Locker skin after native refresh ({})", Pawn->GetName());
				ApplyLoadoutToPawn(Controller, Pawn);
			}
		}
	}
}
