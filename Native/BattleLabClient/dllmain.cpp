// BattleLabClient.dll: loaded into the working-copy Fortnite 12.50 client by BattleLabLauncher.exe.
//
// Stage 1 responsibilities:
//   * wait until the game code has been decrypted by normal startup
//   * redirect every HTTP request to the local backend (http://127.0.0.1:3551) by hooking
//     FCurlHttpRequest::ProcessRequest through its vtable and rewriting the URL FString in place
//   * log crashes inside game code (module offsets) for diagnosis
#include <windows.h>
#include <cstdint>
#include <cwchar>
#include "Log.h"
#include "Mem.h"
#include "TeamWatch.h"
#include "ConsoleCommands.h"

namespace {

// Local backend; -battlelabport=<n> (from the launcher, LocalProfile.json httpPort) moves it, e.g. for a second player on one PC.
wchar_t kLocalBase[64] = L"http://127.0.0.1:3551";
bool g_teamWatchOnHost = false;

void ReadBackendPort() {
    const wchar_t* arg = wcsstr(GetCommandLineW(), L"-battlelabport=");
    unsigned long port = arg ? wcstoul(arg + 15, nullptr, 10) : 0;
    if (port > 0 && port < 65536) swprintf_s(kLocalBase, L"http://127.0.0.1:%lu", port);
}

// UE4 FString
struct FString {
    wchar_t* Data;
    int32_t Num;  // includes terminator
    int32_t Max;
};

bool LooksLikeUrl(const FString* s) {
    if (!Mem::Readable(s, sizeof(FString))) return false;
    if (s->Num < 9 || s->Num > 4096 || s->Max < s->Num) return false;
    if (!Mem::Readable(s->Data, s->Num * sizeof(wchar_t))) return false;
    if (s->Data[s->Num - 1] != 0) return false;
    return wcsncmp(s->Data, L"http://", 7) == 0 || wcsncmp(s->Data, L"https://", 8) == 0;
}

// Rewrites scheme+host to the local backend, keeping path+query. In place (the new URL is shorter than
// any Epic URL); returns false if it does not fit.
bool RewriteUrl(FString* s) {
    const wchar_t* url = s->Data;
    const wchar_t* afterScheme = wcsstr(url, L"://") + 3;
    if (wcsncmp(afterScheme, L"127.0.0.1", 9) == 0 || wcsncmp(afterScheme, L"localhost", 9) == 0) return true;
    const wchar_t* path = wcschr(afterScheme, L'/');
    if (!path) path = L"/";
    size_t need = wcslen(kLocalBase) + wcslen(path) + 1;
    if (need > static_cast<size_t>(s->Max)) return false;
    wchar_t tmp[4096];
    swprintf_s(tmp, L"%s%s", kLocalBase, path);
    wcscpy_s(s->Data, s->Max, tmp);
    s->Num = static_cast<int32_t>(need);
    return true;
}

using ProcessRequestFn = bool (*)(void* self);
ProcessRequestFn g_origProcessRequest = nullptr;
volatile LONG g_requestCount = 0;

bool HookedProcessRequest(void* self) {
    // Locate the URL member by shape (no hard-coded offsets): first FString in the object that is an http(s) URL.
    auto base = reinterpret_cast<uint8_t*>(self);
    for (size_t off = 8; off < 0x200; off += 8) {
        auto s = reinterpret_cast<FString*>(base + off);
        if (!LooksLikeUrl(s)) continue;
        char before[16384] = {};
        WideCharToMultiByte(CP_UTF8, 0, s->Data, -1, before, sizeof(before), nullptr, nullptr);
        bool ok = RewriteUrl(s);
        LONG n = InterlockedIncrement(&g_requestCount);
        if (n <= 400) {
            char after[16384] = {};
            WideCharToMultiByte(CP_UTF8, 0, s->Data, -1, after, sizeof(after), nullptr, nullptr);
            BL_LOG("[HTTP] #%ld %s -> %s%s", n, before, ok ? after : "(NOT REWRITTEN: too long)", "");
        }
        break;
    }
    return g_origProcessRequest(self);
}

bool InstallHttpRedirect() {
    const wchar_t* anchors[] = {
        L"Cannot process HTTP request: URL is empty",
        L"ProcessRequest failed. Still processing last request.",
        L"ProcessRequest failed. URL '%s' is not using a whitelisted domain. %p",
    };
    uint8_t* fn = nullptr;
    for (auto a : anchors) {
        auto str = Mem::FindWide(a);
        if (!str) { BL_LOG("[HTTP] anchor not found in .rdata: %ls", a); continue; }
        auto refs = Mem::FindXrefs(str);
        BL_LOG("[HTTP] anchor '%ls' at +0x%llx, %zu code refs", a, (unsigned long long)Mem::Rva(str), refs.size());
        for (auto r : refs) {
            auto f = Mem::FunctionStart(r);
            BL_LOG("[HTTP]   ref +0x%llx in function +0x%llx", (unsigned long long)Mem::Rva(r),
                   (unsigned long long)(f ? Mem::Rva(f) : 0));
            if (f && !fn) fn = f;
            else if (f && f != fn) BL_LOG("[HTTP]   (differs from first candidate +0x%llx)", (unsigned long long)Mem::Rva(fn));
        }
        if (fn) break;
    }
    if (!fn) { BL_LOG("[HTTP] ProcessRequest not located"); return false; }

    // The anchor sits in SetupRequest (non-virtual, called from ProcessRequest). Walk up direct callers
    // until we reach a function that is referenced from a vtable.
    for (int depth = 0; depth < 3 && Mem::FindPointerSlots(fn).empty(); ++depth) {
        auto text = Mem::Find(".text");
        uint8_t* caller = nullptr;
        int callers = 0;
        for (uint8_t* p = text->start; p < text->start + text->size - 5; ++p) {
            if (p[0] != 0xE8) continue;
            int32_t rel;
            memcpy(&rel, p + 1, 4);
            if (p + 5 + rel == fn) {
                ++callers;
                if (!caller) caller = Mem::FunctionStart(p);
            }
        }
        BL_LOG("[HTTP] +0x%llx is not virtual; %d caller(s), moving up to +0x%llx", (unsigned long long)Mem::Rva(fn), callers,
               (unsigned long long)(caller ? Mem::Rva(caller) : 0));
        if (callers != 1 || !caller) break;  // ambiguous: stop
        fn = caller;
    }

    auto slots = Mem::FindPointerSlots(fn);
    BL_LOG("[HTTP] ProcessRequest candidate +0x%llx referenced by %zu vtable slot(s)", (unsigned long long)Mem::Rva(fn), slots.size());
    if (slots.empty()) {
        // Diagnostics: where else is it referenced, and what does its entry look like?
        for (auto& s : Mem::Main().sections) {
            if (strcmp(s.name, ".data") && strcmp(s.name, "_RDATA") && strcmp(s.name, ".rdata")) continue;
            auto p = reinterpret_cast<void**>(s.start);
            auto end = reinterpret_cast<void**>(s.start + (s.size & ~7ull));
            for (; p < end; ++p)
                if (*p == fn) BL_LOG("[HTTP]   pointer in %s at +0x%llx", s.name, (unsigned long long)Mem::Rva(p));
        }
        char hex[3 * 64 + 1] = {};
        for (int i = 0; i < 64; ++i) sprintf_s(hex + i * 3, 4, "%02x ", fn[i]);
        BL_LOG("[HTTP]   entry bytes: %s", hex);
        auto text = Mem::Find(".text");
        int calls = 0;
        for (uint8_t* p = text->start; p < text->start + text->size - 5 && calls < 12; ++p) {
            if (p[0] != 0xE8 && p[0] != 0xE9) continue;
            int32_t rel;
            memcpy(&rel, p + 1, 4);
            if (p + 5 + rel == fn) {
                auto caller = Mem::FunctionStart(p);
                BL_LOG("[HTTP]   %s from +0x%llx (function +0x%llx)", p[0] == 0xE8 ? "call" : "jmp", (unsigned long long)Mem::Rva(p),
                       (unsigned long long)(caller ? Mem::Rva(caller) : 0));
                ++calls;
            }
        }
        return false;
    }
    g_origProcessRequest = reinterpret_cast<ProcessRequestFn>(fn);
    for (auto slot : slots) {
        Mem::WritePtr(slot, reinterpret_cast<void*>(&HookedProcessRequest));
        BL_LOG("[HTTP] hooked vtable slot at +0x%llx", (unsigned long long)Mem::Rva(slot));
    }
    return true;
}

// ---------------------------------------------------------------- crash diagnostics
volatile LONG g_crashLogs = 0;
uint8_t* g_watchPage = nullptr;
DWORD g_watchProtection = 0;
thread_local bool g_watchStep = false;
volatile LONG g_watchHits = 0;

LONG CALLBACK WriteWatch(EXCEPTION_POINTERS* ep) {
    if (!g_watchPage) return EXCEPTION_CONTINUE_SEARCH;
    auto e = ep->ExceptionRecord;
    auto c = ep->ContextRecord;
    if (e->ExceptionCode == EXCEPTION_SINGLE_STEP && g_watchStep) {
        g_watchStep = false;
        c->EFlags &= ~0x100;
        DWORD old;
        VirtualProtect(g_watchPage, 4096, PAGE_READONLY, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || e->ExceptionInformation[0] != 1)
        return EXCEPTION_CONTINUE_SEARCH;
    auto target = reinterpret_cast<uint8_t*>(e->ExceptionInformation[1]);
    if (target < g_watchPage || target >= g_watchPage + 4096) return EXCEPTION_CONTINUE_SEARCH;
    LONG n = InterlockedIncrement(&g_watchHits);
    if (n < 100 || (target >= Mem::Main().base + 0x8195718 && target < Mem::Main().base + 0x8195720)) {
        BL_LOG("[WRITE WATCH] #%ld tid=%lu pc=+%llx target=+%llx rax=%llx rcx=%llx rdx=%llx r8=%llx r9=%llx",
            n, GetCurrentThreadId(), (unsigned long long)Mem::Rva(e->ExceptionAddress),
            (unsigned long long)Mem::Rva(target), c->Rax,c->Rcx,c->Rdx,c->R8,c->R9);
        unsigned char* p = reinterpret_cast<unsigned char*>(c->Rip);
        if (Mem::Readable(p,32)) {
            char hex[97] = {};
            for(int i=0;i<32;++i) sprintf_s(hex+i*3,4,"%02x ",p[i]);
            BL_LOG("[WRITE CODE] %s",hex);
        }
        auto stack = reinterpret_cast<uintptr_t*>(c->Rsp);
        if(Mem::Readable(stack,256)) for(int i=0;i<32;++i)
            if(Mem::InModule(reinterpret_cast<void*>(stack[i])))
                BL_LOG("[WRITE STACK] %d +%llx",i,(unsigned long long)(stack[i]-(uintptr_t)Mem::Main().base));
    }
    DWORD old;
    VirtualProtect(g_watchPage,4096,g_watchProtection,&old);
    g_watchStep = true;
    c->EFlags |= 0x100;
    return EXCEPTION_CONTINUE_EXECUTION;
}

LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* ep) {
    auto code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    auto addr = ep->ExceptionRecord->ExceptionAddress;
    if (!Mem::InModule(addr)) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_crashLogs) > 20) return EXCEPTION_CONTINUE_SEARCH;
    auto c = ep->ContextRecord;
    // Actual x64 unwind frames, plus bytes at the fault, distinguish a social
    // callback failure from corruption shared by otherwise unrelated threads.
    if (g_crashLogs == 1) {
        auto bytes = reinterpret_cast<unsigned char*>(addr) - 32;
        if (Mem::Readable(bytes, 96)) {
            char hex[289] = {};
            for (int i = 0; i < 96; ++i) sprintf_s(hex + i * 3, 4, "%02x ", bytes[i]);
            BL_LOG("[FAULT BYTES] from +0x%llx: %s", (unsigned long long)Mem::Rva(bytes), hex);
        }
        CONTEXT walk = *c;
        for (int frame = 0; frame < 40 && walk.Rip; ++frame) {
            DWORD64 imageBase = 0;
            auto entry = RtlLookupFunctionEntry(walk.Rip, &imageBase, nullptr);
            BL_LOG("[UNWIND] #%d pc=%llx module=%llx rva=%llx function=%lx", frame,
                walk.Rip, imageBase, imageBase ? walk.Rip - imageBase : 0, entry ? entry->BeginAddress : 0);
            if (entry) {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, walk.Rip, entry, &walk,
                    &handlerData, &establisher, nullptr);
            } else {
                if (!Mem::Readable(reinterpret_cast<void*>(walk.Rsp), 8)) break;
                walk.Rip = *reinterpret_cast<DWORD64*>(walk.Rsp);
                walk.Rsp += 8;
            }
        }
    }
    BL_LOG("[CRASH?] code=0x%08lx at +0x%llx (first chance) tid=%lu access=%llu target=0x%llx",
           code, (unsigned long long)Mem::Rva(addr), GetCurrentThreadId(),
           (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0],
           (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    BL_LOG("  rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx", c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi);
    BL_LOG("  r8=%llx r9=%llx r10=%llx r11=%llx rsp=%llx rbp=%llx", c->R8, c->R9, c->R10, c->R11, c->Rsp, c->Rbp);
    // Return addresses on the stack that point into the module (cheap backtrace).
    auto sp = reinterpret_cast<uint64_t*>(c->Rsp);
    int shown = 0;
    for (int i = 0; i < 512 && shown < 16; ++i) {
        if (!Mem::Readable(sp + i, 8)) break;
        auto v = reinterpret_cast<void*>(sp[i]);
        if (Mem::InModule(v)) { BL_LOG("  stack[%d] +0x%llx", i, (unsigned long long)Mem::Rva(v)); ++shown; }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI MainThread(LPVOID) {
    BL_LOG("[BattleLab] client module loaded, image base %p", Mem::Main().base);
    AddVectoredExceptionHandler(0, CrashLogger);

    DWORD t0 = GetTickCount();
    while (!Mem::CodeLooksDecrypted()) {
        if (GetTickCount() - t0 > 120000) { BL_LOG("[BattleLab] timed out waiting for code decryption"); return 0; }
        Sleep(5);
    }
    BL_LOG("[BattleLab] game code decrypted after %lu ms", GetTickCount() - t0);

    if (InstallHttpRedirect()) BL_LOG("[BattleLab] HTTP redirect to %ls active", kLocalBase);
    else BL_LOG("[BattleLab] HTTP redirect FAILED (requests stay blocked by the firewall guard)");
    if (!wcsstr(GetCommandLineW(), L"localbattlelabhost") || g_teamWatchOnHost) ConsoleCommands::Install();
    // Development diagnostics for the friendly marker (player client; on the host only with teamwatch-host.flag, to validate it).
    if (!wcsstr(GetCommandLineW(), L"localbattlelabhost") || g_teamWatchOnHost) TeamWatch::Start();
    wchar_t watch[8];
    if (GetEnvironmentVariableW(L"BATTLELAB_WATCH_GLOBAL",watch,8)) {
        g_watchPage = Mem::Main().base + 0x8195000;
        AddVectoredExceptionHandler(1, WriteWatch);
        BL_LOG("[WRITE WATCH] global +8195718 initial=%p", *reinterpret_cast<void**>(g_watchPage+0x718));
        VirtualProtect(g_watchPage,4096,PAGE_READONLY,&g_watchProtection);
    }
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        wchar_t flag[MAX_PATH];
        GetModuleFileNameW(self, flag, MAX_PATH);
        if (auto slash = wcsrchr(flag, L'\\')) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - flag), L"teamwatch-host.flag");
        g_teamWatchOnHost = GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES;
        ReadBackendPort();
        // The local Battle Lab host runs the same module; give it its own log (marker from the launcher).
        Log::Init(self, wcsstr(GetCommandLineW(), L"localbattlelabhost") ? L"BattleLabHostClient.log" : L"BattleLabClient.log");
        CloseHandle(CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr));
    }
    return TRUE;
}
