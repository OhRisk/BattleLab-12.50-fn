// Player client: Battle Lab commands without the "cheat" prefix.
// The console sends each line to APlayerController::ConsoleCommand, which passes it to UPlayer::ConsoleCommand (located from its
// "Command not recognized: %s" message); the controller's entry is hooked in its vtable slots. A line whose first word is a
// local-server command (fly, speed, friendly, ...) is passed on as
// "cheat <line>", which the engine sends to the server like any typed "cheat" command. Everything else is untouched.
// Fly and noclip are predicted on this client, so fly speed and braking are also set on the local character here
// (the server sets the same values and accepts the client's position while flying).
#pragma once
#include <windows.h>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>
#include "Log.h"
#include "Mem.h"

namespace ConsoleCommands {

struct FStringView { wchar_t* Data; int32_t Num; int32_t Max; };  // UE4 FString layout

// Server commands (commands.cpp), short names included.
inline const wchar_t* kCommands[] = {
    L"friendly", L"enemy", L"friendlybot", L"enemybot", L"fly", L"noclip", L"ghost", L"walk", L"speed", L"flyspeed", L"god", L"heal", L"tp", L"teleport",
    L"goto", L"bugitgo", L"save", L"savewaypoint", L"wp", L"waypoint", L"give", L"giveitem", L"drop", L"spawnpickup", L"kill",
    L"suicide", L"destroy", L"destroytarget", L"storm", L"pausesafezone", L"players", L"listplayers", L"skin", L"applycid",
    L"pickaxe", L"setpickaxe", L"clear", L"wipequickbars", L"wipequickbar", L"launch", L"health", L"sethealth", L"shield",
    L"setshield", L"summon", L"help", L"cheats",
};

// 12.50 layout: AController::Pawn +0x248, ACharacter::CharacterMovement +0x280;
// movement component MaxFlySpeed +0x198, BrakingDecelerationFlying +0x1c0, MaxAccelerationFlying +0x1e64.
constexpr int kPawn = 0x248, kCharacterMovement = 0x280;
constexpr int kMaxFlySpeed = 0x198, kBrakingDecelerationFlying = 0x1c0, kMaxAccelerationFlying = 0x1e64;

inline float g_flySpeed = 2.f;  // multiples of running speed, as on the server

inline void* ReadPtr(void* base, int offset) {
    auto p = reinterpret_cast<uint8_t*>(base) + offset;
    return Mem::Readable(p, 8) ? *reinterpret_cast<void**>(p) : nullptr;
}

inline void TuneLocalFlight(void* controller) {
    void* movement = ReadPtr(ReadPtr(controller, kPawn), kCharacterMovement);
    if (!movement || !Mem::Readable(reinterpret_cast<uint8_t*>(movement) + kMaxAccelerationFlying, 4)) return;
    float speed = 600.f * g_flySpeed;
    auto m = reinterpret_cast<uint8_t*>(movement);
    *reinterpret_cast<float*>(m + kMaxFlySpeed) = speed;
    *reinterpret_cast<float*>(m + kBrakingDecelerationFlying) = speed * 4.f;
    *reinterpret_cast<float*>(m + kMaxAccelerationFlying) = speed * 4.f;
}

// The server switches the character into cheat flying and sends ClientCheatFly/Ghost/Walk, but on this client the character
// stays in walking mode, so the server kept correcting it (the jerky flight). Start the same engine functions locally:
// a Client RPC called on the owning client runs its implementation here.
constexpr uintptr_t kProcessEvent = 0x2e88310, kStaticFindObject = 0x2e909a0;
enum class Move { Walk, Fly, Noclip };
inline Move g_move = Move::Walk;
inline void* g_movePawn = nullptr;

inline void* FindObjectByPath(const wchar_t* path) {
    auto find = reinterpret_cast<void* (*)(void*, void*, const wchar_t*, bool)>(Mem::Main().base + kStaticFindObject);
    return find(nullptr, nullptr, path, false);
}

inline void ApplyLocalMove(void* controller, const std::wstring& cmd) {
    void* pawn = ReadPtr(controller, kPawn);
    if (!pawn) return;
    if (pawn != g_movePawn) { g_movePawn = pawn; g_move = Move::Walk; }  // new character: walking, as on the server
    Move wanted = cmd == L"walk" ? Move::Walk : cmd == L"fly" ? Move::Fly : Move::Noclip;
    g_move = g_move == wanted ? Move::Walk : wanted;

    static void* fly = FindObjectByPath(L"/Script/Engine.Character.ClientCheatFly");
    static void* ghost = FindObjectByPath(L"/Script/Engine.Character.ClientCheatGhost");
    static void* walk = FindObjectByPath(L"/Script/Engine.Character.ClientCheatWalk");
    void* fn = g_move == Move::Walk ? walk : g_move == Move::Fly ? fly : ghost;
    if (!fn) return;
    auto processEvent = reinterpret_cast<void (*)(void*, void*, void*)>(Mem::Main().base + kProcessEvent);
    alignas(16) uint8_t params[0x20] = {};
    processEvent(pawn, fn, params);
    if (g_move != Move::Walk) TuneLocalFlight(controller);
}

using ConsoleCommandFn = void* (*)(void* self, void* result, const FStringView* command, bool writeToLog);
inline ConsoleCommandFn g_original = nullptr;

inline std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

inline void* Hooked(void* self, void* result, const FStringView* command, bool writeToLog) {
    if (!command || !command->Data || command->Num <= 1)
        return g_original(self, result, command, writeToLog);

    std::wstring line(command->Data);
    size_t start = line.find_first_not_of(L' ');
    if (start == std::wstring::npos) return g_original(self, result, command, writeToLog);
    line = line.substr(start);

    // Words of the line; "cheat fly 3" and "fly 3" both give command "fly", argument "3".
    std::vector<std::wstring> words;
    std::wstring lowered = Lower(line);
    for (size_t i = 0; i < lowered.size();) {
        size_t end = lowered.find(L' ', i);
        if (end == std::wstring::npos) end = lowered.size();
        if (end > i) words.push_back(lowered.substr(i, end - i));
        i = end + 1;
    }
    bool bPrefixed = !words.empty() && words[0] == L"cheat";
    size_t at = bPrefixed ? 1 : 0;
    std::wstring cmd = words.size() > at ? words[at] : L"";
    std::wstring arg = words.size() > at + 1 ? words[at + 1] : L"";

    bool bKnown = false;
    for (auto c : kCommands) bKnown |= cmd == c;
    if (!bKnown) return g_original(self, result, command, writeToLog);

    if (cmd == L"flyspeed") {
        g_flySpeed = arg.empty() ? 2.f : (std::max)(0.1f, (std::min)(50.f, static_cast<float>(_wtof(arg.c_str()))));
        if (g_move != Move::Walk) TuneLocalFlight(self);
    }

    void* out;
    if (bPrefixed) out = g_original(self, result, command, writeToLog);
    else {
        std::wstring forwarded = L"cheat " + line;
        FStringView copy{ forwarded.data(), static_cast<int32_t>(forwarded.size() + 1), static_cast<int32_t>(forwarded.size() + 1) };
        BL_LOG("[Console] %ls -> %ls", line.c_str(), forwarded.c_str());
        out = g_original(self, result, &copy, writeToLog);
    }
    if (cmd == L"fly" || cmd == L"noclip" || cmd == L"ghost" || cmd == L"walk") ApplyLocalMove(self, cmd == L"ghost" ? L"noclip" : cmd);
    return out;
}

inline bool Install() {
    auto text = Mem::FindWide(L"Command not recognized: %s");
    if (!text) { BL_LOG("[Console] message not found; short commands off"); return false; }
    auto refs = Mem::FindXrefs(text);
    uint8_t* fn = refs.empty() ? nullptr : Mem::FunctionStart(refs[0]);
    // UPlayer::ConsoleCommand(this, FString* Result, const FString& Cmd, bool bWriteToLog) starts by spilling r9b and rdx.
    static const uint8_t prologue[] = { 0x44, 0x88, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10 };
    if (!fn || memcmp(fn, prologue, sizeof(prologue)) != 0) {
        BL_LOG("[Console] UPlayer::ConsoleCommand not recognized at +0x%llx; short commands off", (unsigned long long)(fn ? Mem::Rva(fn) : 0));
        return false;
    }
    // It is not virtual; the virtual APlayerController::ConsoleCommand is a small caller that passes this->Player (+0x290).
    auto textSection = Mem::Find(".text");
    static const uint8_t loadPlayer[] = { 0x48, 0x8B, 0x89, 0x90, 0x02, 0x00, 0x00 };  // mov rcx, [rcx+0x290]
    for (uint8_t* p = textSection->start; p < textSection->start + textSection->size - 5; ++p) {
        if (p[0] != 0xE8) continue;
        int32_t rel;
        memcpy(&rel, p + 1, 4);
        if (p + 5 + rel != fn) continue;
        uint8_t* caller = Mem::FunctionStart(p);
        if (!caller || p - caller > 0x30) continue;
        bool bLoadsPlayer = false;
        for (uint8_t* q = caller; q < p && !bLoadsPlayer; ++q) bLoadsPlayer = memcmp(q, loadPlayer, sizeof(loadPlayer)) == 0;
        auto slots = bLoadsPlayer ? Mem::FindPointerSlots(caller, 64) : std::vector<void**>{};
        if (slots.empty()) continue;
        g_original = reinterpret_cast<ConsoleCommandFn>(caller);
        for (auto slot : slots) Mem::WritePtr(slot, reinterpret_cast<void*>(&Hooked));
        BL_LOG("[Console] short commands active (APlayerController::ConsoleCommand +0x%llx, %zu vtable slots)", (unsigned long long)Mem::Rva(caller), slots.size());
        return true;
    }
    BL_LOG("[Console] APlayerController::ConsoleCommand not found; short commands off");
    return false;
}

}  // namespace ConsoleCommands
