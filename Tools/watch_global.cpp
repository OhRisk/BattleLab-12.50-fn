// Read-only diagnosis: watch writes to an observed global with a CPU data breakpoint.
// Usage: watch_global.exe <pid> <module-relative-hex-address> <output-directory>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 2;
    DWORD pid = wcstoul(argv[1], nullptr, 10);
    uintptr_t rva = _wcstoui64(argv[2], nullptr, 16), base = 0;
    if (!DebugActiveProcess(pid)) { printf("attach failed %lu\n", GetLastError()); return 1; }
    DebugSetProcessKillOnExit(FALSE);
    HANDLE proc = nullptr;
    std::map<DWORD, HANDLE> threads;
    auto read = [&](uintptr_t addr, void* out, size_t n) {
        SIZE_T got = 0;
        return ReadProcessMemory(proc, (void*)addr, out, n, &got) && got == n;
    };
    auto arm = [&](HANDLE thread) {
        CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (!GetThreadContext(thread, &c)) return;
        c.Dr0 = base + rva; c.Dr6 = 0;
        c.Dr7 = (c.Dr7 & ~0xf0003ull) | 0x90001; // local DR0, write, 8 bytes
        if (!SetThreadContext(thread, &c)) printf("arm failed %lu\n", GetLastError());
    };
    bool alive = true, initial = true, dumped = false;
    while (alive) {
        DEBUG_EVENT e{};
        if (!WaitForDebugEvent(&e, 1000)) continue;
        DWORD disposition = DBG_CONTINUE;
        switch (e.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            proc = e.u.CreateProcessInfo.hProcess;
            base = (uintptr_t)e.u.CreateProcessInfo.lpBaseOfImage;
            threads[e.dwThreadId] = e.u.CreateProcessInfo.hThread;
            if (e.u.CreateProcessInfo.hFile) CloseHandle(e.u.CreateProcessInfo.hFile);
            printf("attached pid=%lu base=%llx watched=%llx\n", pid, base, base+rva);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            threads[e.dwThreadId] = e.u.CreateThread.hThread;
            if (!initial) arm(e.u.CreateThread.hThread);
            break;
        case EXIT_THREAD_DEBUG_EVENT:
            if (threads.count(e.dwThreadId)) { CloseHandle(threads[e.dwThreadId]); threads.erase(e.dwThreadId); }
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (e.u.LoadDll.hFile) CloseHandle(e.u.LoadDll.hFile);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            DWORD code = e.u.Exception.ExceptionRecord.ExceptionCode;
            auto thread = threads[e.dwThreadId];
            CONTEXT c{}; c.ContextFlags = CONTEXT_ALL;
            GetThreadContext(thread, &c);
            if (code == EXCEPTION_BREAKPOINT && initial) {
                initial = false;
                for (auto& t : threads) arm(t.second);
                uintptr_t value = 0; read(base+rva, &value, 8);
                printf("watch active initial value=%llx\n", value);
            } else if (code == EXCEPTION_SINGLE_STEP && (c.Dr6 & 1) && c.Dr0 == base+rva) {
                uintptr_t value = 0; read(base+rva, &value, 8);
                printf("WRITE tid=%lu next-rip=+%llx value=%llx rax=%llx rcx=%llx rdx=%llx r8=%llx r9=%llx\n",
                    e.dwThreadId,c.Rip-base,value,c.Rax,c.Rcx,c.Rdx,c.R8,c.R9);
                unsigned char bytes[96]{};
                if (read(c.Rip-48,bytes,sizeof(bytes))) {
                    printf("code +%llx: ",c.Rip-base-48);
                    for (auto b:bytes) printf("%02x",b); puts("");
                }
                uintptr_t stack[64]{};
                if (read(c.Rsp, stack,sizeof(stack))) for (int i=0;i<64;++i)
                    if (stack[i] >= base && stack[i] < base+0xa000000)
                        printf("stack[%d]=+%llx\n",i,stack[i]-base);
                if (!dumped) {
                    dumped = true;
                    // Save mapped code/data locally for static xrefs, without patching the game.
                    std::wstring file=std::wstring(argv[3])+L"\\runtime-image.bin";
                    FILE* f=nullptr; _wfopen_s(&f,file.c_str(),L"wb");
                    if(f) {
                        unsigned char block[4096];
                        for(uintptr_t off=0;off<0x8300000;off+=sizeof(block)) {
                            ZeroMemory(block,sizeof(block)); read(base+off,block,sizeof(block));
                            fwrite(block,1,sizeof(block),f);
                        }
                        fclose(f);
                    }
                }
                c.ContextFlags = CONTEXT_DEBUG_REGISTERS; c.Dr6=0; SetThreadContext(thread,&c);
            } else {
                static int otherEvents = 0;
                if (otherEvents++ < 30) printf("exception %08lx first=%lu rip=+%llx dr0=%llx dr7=%llx\n",code,e.u.Exception.dwFirstChance,c.Rip-base,c.Dr0,c.Dr7);
                disposition=DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            printf("exit=%08lx\n",e.u.ExitProcess.dwExitCode); alive=false; break;
        }
        fflush(stdout);
        ContinueDebugEvent(e.dwProcessId,e.dwThreadId,disposition);
    }
    for(auto& t:threads) CloseHandle(t.second);
    if(proc) CloseHandle(proc);
    return 0;
}
