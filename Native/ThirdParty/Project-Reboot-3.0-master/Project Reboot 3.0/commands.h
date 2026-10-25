#pragma once

#include "reboot.h"
#include "FortPlayerControllerAthena.h"
#include "KismetSystemLibrary.h"
#include "AthenaBarrierObjective.h"
#include "FortAthenaMutator_Barrier.h"
#include "FortWeaponMeleeItemDefinition.h"
#include "builder.h"
#include "FortLootPackage.h"
#include "bots.h"
#include "FortAthenaMutator_Bots.h"
#include "ai.h"
#include "moderation.h"
#include "BattleLabNotify.h"

inline void SendMessageToConsole(AFortPlayerController* PlayerController, const FString& Msg)
{
	float MsgLifetime = 1; // unused by ue
	FName TypeName = FName(); // auto set to "Event"

	static auto ClientMessageFn = FindObject<UFunction>(L"/Script/Engine.PlayerController.ClientMessage");
	struct 
	{
		FString                                     S;                                                        // (Parm, ZeroConstructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
		FName                                       Type;                                                     // (Parm, ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
		float                                              MsgLifeTime;                                              // (Parm, ZeroConstructor, IsPlainOldData, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPublic)
	} APlayerController_ClientMessage_Params{Msg, TypeName, MsgLifetime};

	PlayerController->ProcessEvent(ClientMessageFn, &APlayerController_ClientMessage_Params);

	// auto brah = Msg.ToString();
	// LOG_INFO(LogDev, "{}", brah);
}

inline bool IsPrivateIP(std::string IPStr) {
	if (IPStr.find("192.168") == 0 || IPStr.find("10.0") == 0 || IPStr.find("172.16") == 0) {
		return true;
	}

	return false;
}

inline bool IsOperator(APlayerState* PlayerState, AFortPlayerController* PlayerController)
{
	auto& IP = PlayerState->GetSavedNetworkAddress();
	auto IPStr = IP.ToString();

	// std::cout << "IPStr: " << IPStr << '\n';

	// Local Battle Lab host: any 127.x address (with or without port) is the player on this PC. Friends joining over
	// LAN/VPN may use cheats when the host allows it (LocalServerConfig.json "cheats": "everyone").
	if (IPStr.starts_with("127.") || BattleLab::GetHostConfig().bCheatsForEveryone || IsOp(PlayerController))
	{
		return true;
	}
	// Hosting for friends with "cheats": "host": only the player on this PC, whatever the friends' addresses are.
	if (Globals::bPrivateIPsAreOperator && !BattleLab::GetHostConfig().bLan) {
		return IsPrivateIP(IPStr);
	}

	SendMessageToConsole(PlayerController, L"You are not an Operator!");
	return false;
}


void ServerCheatHook(AFortPlayerControllerAthena* PlayerController, FString Msg);
