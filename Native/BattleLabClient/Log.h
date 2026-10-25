// Tiny thread-safe file logger: <project>/Logs/BattleLabClient.log (next to the DLL's parent folder).
#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>

namespace Log {

inline CRITICAL_SECTION g_cs;
inline FILE* g_file = nullptr;

inline void Init(HMODULE self, const wchar_t* fileName) {
    InitializeCriticalSection(&g_cs);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    // DLL lives in <project>/Native/bin/ -> log into <project>/Logs/
    for (int up = 0; up < 3; ++up) {
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash) *slash = 0;
    }
    wcscat_s(path, L"\\Logs");
    CreateDirectoryW(path, nullptr);
    wcscat_s(path, L"\\");
    wcscat_s(path, fileName);
    g_file = _wfsopen(path, L"w", _SH_DENYWR);
}

inline void Write(const char* fmt, ...) {
    char buf[32768] = {};
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = sprintf_s(buf, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
    buf[sizeof(buf) - 1] = 0;
    va_end(ap);
    EnterCriticalSection(&g_cs);
    if (g_file) {
        fputs(buf, g_file);
        fputc('\n', g_file);
        fflush(g_file);
    }
    LeaveCriticalSection(&g_cs);
}

}  // namespace Log

#define BL_LOG(...) Log::Write(__VA_ARGS__)
