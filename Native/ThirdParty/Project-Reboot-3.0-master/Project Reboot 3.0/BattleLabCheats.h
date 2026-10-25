#pragma once

// Local Battle Lab host: movement cheats (speed, fly speed, fly / noclip) with per-player settings that survive respawns.
//
// Speed uses Fortnite's own movement speed multiplier (FortPawn.SetMovementSpeedMultiplier: the MovementSpeedMultiplier
// gameplay attribute, which replicates to the owning client, so client prediction and server agree).
// Fly and noclip are the engine's cheat flying (MOVE_Flying + bCheatFlying, collision off for noclip, ClientCheatFly/Ghost on
// the owning client). The client's movement is predicted locally, so while flying the server accepts the client's position
// (bIgnoreClientMovementErrorChecksAndCorrection, bServerAcceptClientAuthoritativePosition) instead of correcting it back:
// that is what made flying jerky. The client module sets the same fly speed and braking on its side.

#include "reboot.h"
#include "FortPlayerControllerAthena.h"
#include "FortPawn.h"
#include "NetDriver.h"

#include <map>

namespace BattleLabCheats
{
	enum class EMove : uint8_t { Walk, Fly, Noclip };

	struct FPlayerCheats
	{
		float Speed = 1.f;     // movement speed multiplier
		float FlySpeed = 2.f;  // fly speed in multiples of normal running speed (600 uu/s)
		EMove Move = EMove::Walk;
		UObject* AppliedPawn = nullptr;
	};

	inline std::map<UObject*, FPlayerCheats> Players; // by player controller

	inline void SetBit(UObject* Object, const char* Name, bool Value)
	{
		if (auto Property = Object->GetProperty(Name, false))
			Object->SetBitfieldValue(Object->GetOffset(Name), GetFieldMask(Property), Value);
	}

	inline void SetFloat(UObject* Object, const char* Name, float Value)
	{
		if (auto Offset = Object->GetOffset(Name, false); Offset != -1)
			Object->Get<float>(Offset) = Value;
	}

	inline UObject* GetMovement(AFortPawn* Pawn)
	{
		static auto CharacterMovementOffset = Pawn->GetOffset("CharacterMovement");
		return Pawn->Get(CharacterMovementOffset);
	}

	inline void ApplySpeed(AFortPawn* Pawn, float Speed)
	{
		static auto SetMultiplierFn = FindObject<UFunction>(L"/Script/FortniteGame.FortPawn.SetMovementSpeedMultiplier");
		if (!SetMultiplierFn)
			return;
		alignas(16) uint8_t Params[0x20]{};
		*(float*)Params = Speed;
		Pawn->ProcessEvent(SetMultiplierFn, Params);
	}

	// Fly speed, braking and acceleration for flying (the client module applies the same values locally).
	inline void ApplyFlyTuning(UObject* Movement, float FlySpeed)
	{
		float Speed = 600.f * FlySpeed;
		SetFloat(Movement, "MaxFlySpeed", Speed);
		SetFloat(Movement, "BrakingDecelerationFlying", Speed * 4.f);
		SetFloat(Movement, "MaxAccelerationFlying", Speed * 4.f);
	}

	inline void ApplyMove(AFortPawn* Pawn, EMove Move, float FlySpeed)
	{
		auto Movement = GetMovement(Pawn);
		if (!Movement)
			return;
		bool bWalk = Move == EMove::Walk, bNoclip = Move == EMove::Noclip;

		ApplyFlyTuning(Movement, FlySpeed);
		SetBit(Movement, "bCheatFlying", !bWalk);
		SetBit(Movement, "bIgnoreClientMovementErrorChecksAndCorrection", !bWalk);
		SetBit(Movement, "bServerAcceptClientAuthoritativePosition", !bWalk);

		static auto SetMovementModeFn = FindObject<UFunction>(L"/Script/Engine.CharacterMovementComponent.SetMovementMode");
		struct { uint8_t NewMovementMode; uint8_t NewCustomMode; } ModeParams{ (uint8_t)(bWalk ? 3 : 5), 0 }; // MOVE_Falling / MOVE_Flying
		Movement->ProcessEvent(SetMovementModeFn, &ModeParams);

		static auto SetActorEnableCollisionFn = FindObject<UFunction>(L"/Script/Engine.Actor.SetActorEnableCollision");
		bool bEnableCollision = !bNoclip;
		Pawn->ProcessEvent(SetActorEnableCollisionFn, &bEnableCollision);

		static auto ClientCheatFlyFn = FindObject<UFunction>(L"/Script/Engine.Character.ClientCheatFly");
		static auto ClientCheatGhostFn = FindObject<UFunction>(L"/Script/Engine.Character.ClientCheatGhost");
		static auto ClientCheatWalkFn = FindObject<UFunction>(L"/Script/Engine.Character.ClientCheatWalk");
		if (auto ClientFn = bWalk ? ClientCheatWalkFn : bNoclip ? ClientCheatGhostFn : ClientCheatFlyFn)
			Pawn->ProcessEvent(ClientFn);
	}

	inline FPlayerCheats& Get(AFortPlayerControllerAthena* Controller)
	{
		auto& State = Players[Controller];
		State.AppliedPawn = Controller->GetMyFortPawn();
		return State;
	}

	// Server tick (once a second): a new pawn (respawn) gets the player's speed again; fly / noclip end with the old pawn.
	inline void Tick()
	{
		static ULONGLONG Next = 0;
		if (Players.empty() || GetTickCount64() < Next)
			return;
		Next = GetTickCount64() + 1000;

		static auto NetDriverOffset = GetWorld()->GetOffset("NetDriver");
		auto NetDriver = GetWorld()->Get<UNetDriver*>(NetDriverOffset);
		if (!NetDriver)
			return;
		auto& Connections = NetDriver->GetClientConnections();
		std::map<UObject*, FPlayerCheats> Live;
		for (int i = 0; i < Connections.Num(); ++i)
		{
			static auto PlayerControllerOffset = Connections.at(i)->GetOffset("PlayerController");
			auto Controller = Cast<AFortPlayerControllerAthena>(Connections.at(i)->Get(PlayerControllerOffset));
			auto It = Controller ? Players.find(Controller) : Players.end();
			if (It == Players.end())
				continue;
			auto State = It->second;
			auto Pawn = Controller->GetMyFortPawn();
			if (Pawn && Pawn != State.AppliedPawn)
			{
				State.AppliedPawn = Pawn;
				State.Move = EMove::Walk;
				if (State.Speed != 1.f)
					ApplySpeed(Pawn, State.Speed);
			}
			Live[Controller] = State;
		}
		Players = std::move(Live); // players who left are dropped
	}
}
