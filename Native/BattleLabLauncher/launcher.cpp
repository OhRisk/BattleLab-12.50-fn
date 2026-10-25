// BattleLabLauncher.exe: starts the working-copy Fortnite 12.50 client with BattleLabClient.dll loaded.
// Usage: BattleLabLauncher.exe [extra client args...]
// Paths are resolved relative to this exe: <project>/Native/bin/BattleLabLauncher.exe
#include <windows.h>
#pragma comment(lib, "user32.lib")
#include <cstdio>
#include <string>

static std::wstring ProjectDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s(p);
    for (int i = 0; i < 3; ++i) s = s.substr(0, s.find_last_of(L'\\'));
    return s;
}

static std::wstring QuoteArg(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += ch;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

static bool Inject(HANDLE proc, const std::wstring& dll) {
    size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(proc, mem, dll.c_str(), bytes, nullptr)) return false;
    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE t = CreateRemoteThread(proc, nullptr, 0, loadLib, mem, 0, nullptr);
    if (!t) return false;
    WaitForSingleObject(t, 15000);
    DWORD code = 0;
    GetExitCodeThread(t, &code);
    CloseHandle(t);
    VirtualFreeEx(proc, mem, 0, MEM_RELEASE);
    return code != 0;
}

// ---------------------------------------------------------------- local Battle Lab host
// The host is a second working-copy client: it signs in to the local backend like the player does,
// then BattleLabHost.dll (Project Reboot 3.0, loopback-only build) travels it to Athena with the
// Battle Lab playlist and listens on 127.0.0.1:7777. The fltoken value (24 chars, the length the
// client requires) doubles as a marker that identifies the host's own log file.
static const wchar_t* kHostMarker = L"localbattlelabhost000000";

static std::string ReadShared(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string data;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (256ll << 20)) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &got, nullptr)) got = 0;
        data.resize(got);
    }
    CloseHandle(f);
    return data;
}

// Finds the engine log whose command line carries `markerW` and which was written after `since`.
static std::wstring FindLogWithMarker(const FILETIME& since, const wchar_t* markerW) {
    wchar_t local[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) return {};
    std::wstring dir = std::wstring(local) + L"\\FortniteGame\\Saved\\Logs\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"FortniteGame*.log").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    char marker[64];
    WideCharToMultiByte(CP_UTF8, 0, markerW, -1, marker, sizeof(marker), nullptr, nullptr);
    do {
        if (wcsstr(fd.cFileName, L"backup") || CompareFileTime(&fd.ftLastWriteTime, &since) < 0) continue;
        std::string head = ReadShared(dir + fd.cFileName).substr(0, 64 * 1024);
        size_t line = head.find("Filtered Command Line:");
        if (line != std::string::npos && head.find(marker, line) != std::string::npos &&
            head.find('\n', line) > head.find(marker, line)) {
            found = dir + fd.cFileName;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

// Headless host: the engine still creates a (blank) window; keep it hidden so nobody interacts with it.
static void HideWindowsOf(DWORD pid) {
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        DWORD owner = 0;
        GetWindowThreadProcessId(hwnd, &owner);
        if (owner == static_cast<DWORD>(lp) && IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
        return TRUE;
    }, static_cast<LPARAM>(pid));
}

static int RunHost(const std::wstring& root, const std::wstring& bin, const std::wstring& exe, const std::wstring& clientDll,
                   bool visible) {
    std::wstring hostDll = root + L"\\Native\\bin\\BattleLabHost.dll";
    if (GetFileAttributesW(hostDll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"[BattleLabServer] BattleLabHost.dll is missing. Build the host first.\n");
        return 1;
    }
    std::wstring cmd = QuoteArg(exe);
    cmd += L" -epicapp=Fortnite -epicenv=Prod -epiclocale=en-us -epicportal -skippatchcheck -nobe -noeac"
           L" -AUTH_TYPE=exchangecode -AUTH_LOGIN=BattleLab -AUTH_PASSWORD=battlelab-local"
           L" -nosplash -nosound";
    cmd += std::wstring(L" -fltoken=") + kHostMarker;
    cmd += visible ? L" -windowed -ResX=640 -ResY=360" : L" -nullrhi";

    FILETIME started;
    GetSystemTimeAsFileTime(&started);
    wprintf(L"[BattleLabServer] Starting (build 12.50 CL-13137020, %s)\n", visible ? L"windowed" : L"headless");
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, bin.c_str(), &si, &pi)) {
        wprintf(L"[BattleLabServer] CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }
    if (!Inject(pi.hProcess, clientDll)) {
        wprintf(L"[BattleLabServer] client module injection failed; terminating host\n");
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    wprintf(L"HOSTPID=%lu\n", pi.dwProcessId);
    fflush(stdout);

    // Wait until the host is signed in AND the frontend has finished loading. Sign-in completes while the
    // lobby rooms are still streamed synchronously on the game thread; loading the host module during that
    // window races the game thread (Test016 crash).
    std::wstring log;
    bool signedIn = false;
    for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < 240000;) {
        if (WaitForSingleObject(pi.hProcess, 500) == WAIT_OBJECT_0) break;
        if (!visible) HideWindowsOf(pi.dwProcessId);
        if (log.empty()) {
            log = FindLogWithMarker(started, kHostMarker);
            if (!log.empty()) { wprintf(L"[BattleLabServer] Host log: %s\n", log.c_str()); fflush(stdout); }
            continue;
        }
        // The game-mode (subgame) select screen is idle and needs a click to leave, so it is the trigger;
        // the Athena frontend covers profiles that skip that screen. Reboot travels straight to Athena.
        std::string text = ReadShared(log);
        if (text.find("to [UI.State.Startup.SubgameSelect]") != std::string::npos ||
            text.find("to [UI.State.Athena.Frontend]") != std::string::npos) { signedIn = true; break; }
    }
    if (signedIn && WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0) signedIn = false;
    if (!signedIn) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        wprintf(L"[BattleLabServer] Host did not finish local sign-in (exit code %lu); stopping\n", code);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        return 1;
    }
    wprintf(L"[BattleLabServer] Host signed in; loading Battle Lab host module\n");
    fflush(stdout);
    if (!Inject(pi.hProcess, hostDll)) {
        wprintf(L"[BattleLabServer] host module injection failed; terminating host\n");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        return 1;
    }
    wprintf(L"[BattleLabServer] Host module loaded; loading Athena with the Battle Lab playlist\n");
    fflush(stdout);
    // Stay attached so whoever started us sees the host's lifetime (and keep a headless host hidden).
    while (WaitForSingleObject(pi.hProcess, 1000) == WAIT_TIMEOUT)
        if (!visible) HideWindowsOf(pi.dwProcessId);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    wprintf(L"[BattleLabServer] Host exited (code %lu)\n", code);
    return 0;
}

// The 12.50 installation: <root>\GamePath.txt (written by the patcher's Install.bat) names its folder, the one that
// contains FortniteGame. Without that file this is the development layout (<project>\Fortnite-12.50-BattleLab).
static std::wstring GameDir(const std::wstring& root) {
    std::string text = ReadShared(root + L"\\GamePath.txt");
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text.erase(0, 3);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '\\'))
        text.pop_back();
    if (text.empty()) return root + L"\\Fortnite-12.50-BattleLab";
    std::wstring dir(MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, dir.data(), static_cast<int>(dir.size()));
    dir.resize(wcslen(dir.c_str()));
    return dir;
}

int wmain(int argc, wchar_t** argv) {
    std::wstring root = ProjectDir();
    std::wstring bin = GameDir(root) + L"\\FortniteGame\\Binaries\\Win64";
    std::wstring exe = bin + L"\\FortniteClient-Win64-Shipping.exe";
    std::wstring dll = root + L"\\Native\\bin\\BattleLabClient.dll";

    if (argc >= 2 && wcscmp(argv[1], L"--host") == 0)
        return RunHost(root, bin, exe, dll, argc >= 3 && wcscmp(argv[2], L"--visible") == 0);

    if (argc == 2 && wcsncmp(argv[1], L"--inject-host=", 14) == 0) {
        DWORD pid = wcstoul(argv[1] + 14, nullptr, 10);
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_CREATE_THREAD |
            PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
        wchar_t processPath[32768]; DWORD length = 32768;
        if (!proc || !QueryFullProcessImageNameW(proc, 0, processPath, &length) ||
            _wcsicmp(processPath, exe.c_str()) != 0) {
            wprintf(L"[BattleLab] Host target is not the working-copy game.\n");
            if (proc) CloseHandle(proc);
            return 1;
        }
        bool loaded = Inject(proc, root + L"\\Native\\bin\\BattleLabHost.dll");
        CloseHandle(proc);
        wprintf(L"[BattleLab] Host module load: %s (pid %lu)\n", loaded ? L"success" : L"failed", pid);
        return loaded ? 0 : 1;
    }

    std::wstring map;
    for (int i = 1; i < argc; ++i)
        if (wcsncmp(argv[i], L"--map=", 6) == 0) map = argv[i] + 6;
    std::wstring cmd = QuoteArg(exe) + (map.empty() ? L"" : L" " + QuoteArg(map));
    cmd += L" -epicapp=Fortnite -epicenv=Prod -epiclocale=en-us -epicportal"
                       L" -skippatchcheck -nobe -noeac"
                       L" -AUTH_TYPE=exchangecode -AUTH_LOGIN=BattleLab -AUTH_PASSWORD=battlelab-local";
    // Local launch context, never an Epic credential. This build schedules an
    // allocator-corrupting callback for a present fltoken of the wrong length.
    // The digits make each launch's log identifiable (two players can run on one PC).
    wchar_t digits[16];
    swprintf_s(digits, L"%010lu", GetTickCount() ^ (GetCurrentProcessId() << 16));  // 32-bit: at most 10 digits
    std::wstring localMarker = std::wstring(L"localbattlelab") + digits;
    cmd += L" -fltoken=" + localMarker;
    // Backend port (LocalServer\LocalProfile.json "httpPort"), when it is not the default 3551.
    std::string profile = ReadShared(root + L"\\LocalServer\\LocalProfile.json");
    size_t portKey = profile.find("\"httpPort\"");
    if (portKey != std::string::npos) {
        size_t colon = profile.find(':', portKey);
        unsigned long port = colon == std::string::npos ? 0 : strtoul(profile.c_str() + colon + 1, nullptr, 10);
        if (port && port != 3551) cmd += L" -battlelabport=" + std::to_wstring(port);
    }
    bool companions = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--companions") == 0) { companions = true; continue; }
        if (wcsncmp(argv[i], L"--map=", 6) == 0) continue;
        cmd += L" " + QuoteArg(argv[i]);
    }

    // Optional: the retail flow has FortniteLauncher.exe and the _EAC wrapper present alongside the client.
    // They are started SUSPENDED (no code of theirs runs, no network) and terminated with the client.
    HANDLE companionProcs[2] = {};
    if (companions) {
        const wchar_t* names[2] = {L"FortniteLauncher.exe", L"FortniteClient-Win64-Shipping_EAC.exe"};
        for (int i = 0; i < 2; ++i) {
            std::wstring p = bin + L"\\" + names[i];
            std::wstring c = L"\"" + p + L"\"";
            STARTUPINFOW csi{sizeof(csi)};
            PROCESS_INFORMATION cpi{};
            if (CreateProcessW(p.c_str(), c.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, bin.c_str(), &csi, &cpi)) {
                companionProcs[i] = cpi.hProcess;
                CloseHandle(cpi.hThread);
                wprintf(L"[BattleLab] companion %s held suspended (pid %lu)\n", names[i], cpi.dwProcessId);
            }
        }
    }

    wprintf(L"[BattleLab] launching %s\n", exe.c_str());
    FILETIME started;
    GetSystemTimeAsFileTime(&started);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, bin.c_str(), &si, &pi)) {
        wprintf(L"[BattleLab] CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }
    if (!Inject(pi.hProcess, dll)) {
        wprintf(L"[BattleLab] injection failed (%lu); terminating client\n", GetLastError());
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }
    wprintf(L"[BattleLab] client module loaded; resuming (pid %lu)\n", pi.dwProcessId);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    // Developer console: once the lobby has loaded, the same host module runs in console-only mode
    // in the player client (it detects the missing host marker). Press ` (tilde key) in game.
    std::wstring consoleDll = root + L"\\Native\\bin\\BattleLabHost.dll";
    std::wstring playerMarker = cmd.substr(cmd.find(L"-fltoken=") + 9, 24);
    std::wstring playerLog;
    bool frontend = false;
    wprintf(L"[BattleLab] waiting for the lobby to enable the developer console...\n");
    fflush(stdout);
    for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < 300000;) {
        if (WaitForSingleObject(pi.hProcess, 1000) == WAIT_OBJECT_0) break;
        if (playerLog.empty()) { playerLog = FindLogWithMarker(started, playerMarker.c_str()); continue; }
        std::string text = ReadShared(playerLog);
        if (text.find("to [UI.State.Athena.Frontend]") != std::string::npos) { frontend = true; break; }
    }
    if (frontend && WaitForSingleObject(pi.hProcess, 5000) == WAIT_TIMEOUT)
        wprintf(L"[BattleLab] developer console: %s\n", Inject(pi.hProcess, consoleDll) ? L"enabled (press ` in game)" : L"FAILED to load");
    else
        wprintf(L"[BattleLab] developer console not enabled (lobby not reached)\n");
    if (companions) {
        // Stay alive to clean up the suspended companions when the client exits.
        WaitForSingleObject(pi.hProcess, INFINITE);
        for (auto h : companionProcs)
            if (h) { TerminateProcess(h, 0); CloseHandle(h); }
    }
    CloseHandle(pi.hProcess);
    return 0;
}
