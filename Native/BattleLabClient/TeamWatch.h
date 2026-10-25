// Development diagnostics (player client only, read-only): friendly marker for Friendly Bot Grenade bots.
// Once a second, logs (only when something changed) every Athena player state's team / squad / bot flag, the local player's
// team and squad, and the HUD's player indicators (AthenaIndicatorLayerBase::PlayerIndicators: which player state each one is
// for, its visibility and opacity). This shows when a bot's player state reaches this client on the local team and whether the
// HUD creates its indicator then. Raw memory reads only, each guarded; no engine calls except StaticFindObject at startup.
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include "Log.h"
#include "Mem.h"

namespace TeamWatch {

// 12.50 CL-13137020 layout.
constexpr uintptr_t kStaticFindObject = 0x2e909a0;  // UObject* (UClass*, UObject* Outer, const TCHAR* Name, bool ExactClass)
constexpr uintptr_t kObjectChunks = 0x81bc0a8;      // FUObjectArray::ObjObjects.Objects (FUObjectItem**, 64k items per chunk)
constexpr uintptr_t kObjectCount = 0x81bc0bc;       // FUObjectArray::ObjObjects.NumElements
constexpr int kItemSize = 0x18, kObjectFlags = 0x8, kInternalIndex = 0xc, kClass = 0x10, kSuperStruct = 0x40;
constexpr int kRF_ClassDefaultObject = 0x10;
constexpr int kControllerPlayerState = 0x220, kTeamIndex = 0xe68, kSquadId = 0xfe4, kPlayerStateBits = 0x222;
constexpr int kPlayerIndicators = 0x2a8, kIndicatorPlayerState = 0x2d0, kIndicatedActor = 0x2a0, kVisibility = 0xc3, kOpacity = 0xc4;
constexpr int kTeamIndicatorMesh = 0x2c8, kInstanceData = 0x478;  // AthenaIndicatorLayerBase.TeamIndicatorMesh, ISM PerInstanceSMData
constexpr int kIndicatorManager = 0x1f98, kAllIndicators = 0x28, kOuter = 0x20;  // FortPlayerController.IndicatorManager

struct Classes { void* PlayerState; void* Controller; void* IndicatorLayer; void* InstancedMesh; };

inline void* Find(const wchar_t* Path) {
    auto StaticFindObject = reinterpret_cast<void* (*)(void*, void*, const wchar_t*, bool)>(Mem::Main().base + kStaticFindObject);
    return StaticFindObject(nullptr, nullptr, Path, false);
}

// Guarded reads (no C++ objects with destructors in these functions).
inline bool Read(const void* p, void* out, size_t n) {
    __try { memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <typename T> inline T Get(const void* base, int offset, T fallback = T{}) {
    T v = fallback;
    Read(reinterpret_cast<const uint8_t*>(base) + offset, &v, sizeof(T));
    return v;
}

inline int IndexOf(void* object) { return object ? Get<int>(object, kInternalIndex, -1) : -1; }

inline bool IsChildOf(void* cls, void* parent) {
    for (int guard = 0; cls && guard < 64; ++guard, cls = Get<void*>(cls, kSuperStruct))
        if (cls == parent) return true;
    return false;
}

// What a class is to us: 0 other, 1 player state, 2 player controller, 3 indicator layer (cached per class pointer).
inline int Kind(void* cls, const Classes& k) {
    static std::unordered_map<void*, int> cache;
    auto it = cache.find(cls);
    if (it != cache.end()) return it->second;
    int kind = 0;
    for (void* c = cls; c && !kind; c = Get<void*>(c, kSuperStruct))
        kind = c == k.PlayerState ? 1 : c == k.Controller ? 2 : (k.IndicatorLayer && c == k.IndicatorLayer) ? 3 : 0;
    cache[cls] = kind;
    return kind;
}

inline std::string Snapshot(const Classes& k) {
    auto base = Mem::Main().base;
    auto chunks = Get<void**>(base, (int)kObjectChunks);
    int count = Get<int>(base, (int)kObjectCount);
    std::string states, local, indicators;
    char buf[256];
    for (int i = 0; i < count; ++i) {
        void* chunk = Get<void*>(chunks, (i >> 16) * 8);
        if (!chunk) continue;
        void* object = Get<void*>(chunk, (i & 0xffff) * kItemSize);
        void* cls = object ? Get<void*>(object, kClass) : nullptr;
        int kind = cls ? Kind(cls, k) : 0;
        if (!kind || (Get<int>(object, kObjectFlags) & kRF_ClassDefaultObject)) continue;
        if (kind == 1) {
            // Bits byte +0x222 holds bIsSpectator / bOnlySpectator / bIsABot (raw, masks not assumed).
            sprintf_s(buf, " #%d:t%u/s%u/%02x", IndexOf(object), Get<uint8_t>(object, kTeamIndex), Get<uint8_t>(object, kSquadId),
                Get<uint8_t>(object, kPlayerStateBits));
            states += buf;
        } else if (kind == 2) {
            void* ps = Get<void*>(object, kControllerPlayerState);
            if (ps) {
                sprintf_s(buf, " local #%d t%u/s%u", IndexOf(ps), Get<uint8_t>(ps, kTeamIndex), Get<uint8_t>(ps, kSquadId));
                local += buf;
                // FortIndicatorManager.AllIndicators: which object each indicator belongs to (its outer).
                void* manager = Get<void*>(object, kIndicatorManager);
                auto all = manager ? Get<void**>(manager, kAllIndicators) : nullptr;
                int num = manager ? Get<int>(manager, kAllIndicators + 8) : -1;
                sprintf_s(buf, " manager[%d]:", num);
                local += buf;
                for (int j = 0; j < num && j < 32; ++j) {
                    sprintf_s(buf, " #%d<#%d", IndexOf(Get<void*>(all, j * 8)), IndexOf(Get<void*>(Get<void*>(all, j * 8), kOuter)));
                    local += buf;
                }
            }
        } else if (kind == 3) {
            auto data = Get<void**>(object, kPlayerIndicators);
            int num = Get<int>(object, kPlayerIndicators + 8);
            // Indicated players cache (+0x418; its list at +0x50: 12-byte entries, FWeakObjectPtr index + flags).
            void* cache = Get<void*>(object, 0x418);
            auto entries = cache ? Get<uint8_t*>(cache, 0x50) : nullptr;
            int entryCount = cache ? Get<int>(cache, 0x58) : -1;
            sprintf_s(buf, " cache[%d]:", entryCount);
            indicators += buf;
            for (int j = 0; j < entryCount && j < 32; ++j) {
                sprintf_s(buf, " #%d/%x", Get<int>(entries, j * 12), Get<uint32_t>(entries, j * 12 + 8));
                indicators += buf;
            }
            void* mesh = Get<void*>(object, kTeamIndicatorMesh);
            int meshInstances = mesh && k.InstancedMesh && IsChildOf(Get<void*>(mesh, kClass), k.InstancedMesh) ? Get<int>(mesh, kInstanceData + 8) : -1;
            sprintf_s(buf, " layer#%d[%d] mesh#%d(%d):", IndexOf(object), num, IndexOf(mesh), meshInstances);
            indicators += buf;
            for (int j = 0; j < num && j < 64; ++j) {
                void* w = Get<void*>(data, j * 8);
                if (!w) continue;
                sprintf_s(buf, " ps#%d/actor#%d vis%u op%.2f", IndexOf(Get<void*>(w, kIndicatorPlayerState)), IndexOf(Get<void*>(w, kIndicatedActor)),
                    Get<uint8_t>(w, kVisibility), Get<float>(w, kOpacity));
                indicators += buf;
            }
        }
    }
    return "players" + local + " |" + states + " | indicators" + indicators;
}

inline DWORD WINAPI Thread(LPVOID) {
    Sleep(20000);  // past startup; class objects exist from the start
    Classes k{ Find(L"/Script/FortniteGame.FortPlayerStateAthena"), Find(L"/Script/FortniteGame.FortPlayerControllerAthena"),
        Find(L"/Script/FortniteUI.AthenaIndicatorLayerBase"), Find(L"/Script/Engine.InstancedStaticMeshComponent") };
    BL_LOG("[TeamWatch] classes: player state %p, controller %p, indicator layer %p", k.PlayerState, k.Controller, k.IndicatorLayer);
    if (!k.PlayerState || !k.Controller) return 0;
    std::string last;
    for (;;) {
        Sleep(1000);
        std::string now = Snapshot(k);
        if (now != last) {
            BL_LOG("[TeamWatch] %s", now.c_str());
            last = now;
        }
    }
}

inline void Start() {
    CloseHandle(CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr));
}

}  // namespace TeamWatch
