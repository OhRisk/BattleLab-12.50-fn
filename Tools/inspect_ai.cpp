// Local 12.50 diagnostic: resolves loaded objects and logs their AI vtable slot.
// No gameplay hooks or object mutations. Run only after the host reports READY.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#ifdef INSPECT_DLL
DWORD WINAPI Inspect(void*) {
    auto base = (uintptr_t)GetModuleHandleW(nullptr);
    auto find = (void*(*)(void*,void*,const wchar_t*,bool))(base + 0x2e909a0);
    FILE* log = nullptr;
    fopen_s(&log, "E:\\Fortnite Battle Lab revival\\Logs\\ai-inspection.txt", "w");
    if (!log) return 1;
    auto pe = (void(*)(void*,void*,void*))(base + 0x2e88310);
    auto strings = find(nullptr,nullptr,L"/Script/Engine.Default__KismetStringLibrary",false);
    auto tostring = find(nullptr,nullptr,L"/Script/Engine.KismetStringLibrary.Conv_NameToString",false);
    for (auto name : {L"/Script/FortniteGame.Default__FortKismetLibrary",
        L"/Game/Athena/Apollo/Maps/Apollo_Terrain.Apollo_Terrain:PersistentLevel.PlayerPawn_Athena_C_2147460859",
        L"/Script/FortniteGame.Default__FortPlayerStateAthena",
        L"/Script/FortniteGame.Default__CustomCharacterPart"}) {        auto obj = find(nullptr, nullptr, name, false);
        fprintf(log, "%ls object=%p", name, obj);
        if (obj) fprintf(log, " RunBehaviorTree=%llx", ((uintptr_t*)*(void**)obj)[0x7a8/8] - base);
        fprintf(log, "\n");
        if (obj && strings && tostring) {
            for (auto cls = *(char**)((char*)obj+0x10); cls; cls=*(char**)(cls+0x40)) {
                for (auto fn=*(char**)(cls+0x48); fn; fn=*(char**)(fn+0x28)) {
                    struct { uint64_t name; wchar_t* data; int num,max; } args{*(uint64_t*)(fn+0x18)};
                    pe(strings,tostring,&args);
                    fprintf(log,"  FUNCTION %ls RVA=%llx\n",args.data?args.data:L"?",*(uintptr_t*)(fn+0xf0)-base);
                    if (args.data) ((void(*)(void*))(base+0x2ba6570))(args.data);
                }
                int count=0;
                for (auto prop=*(char**)(cls+0x50); prop && ++count<1024; prop=*(char**)(prop+0x20)) {
                    int offset=*(int*)(prop+0x4c);
                    if (offset<0 || offset>0x1200) continue;
                    struct { uint64_t name; wchar_t* data; int num,max; } params{*(uint64_t*)(prop+0x28)};
                    pe(strings,tostring,&params);
                    fprintf(log,"  +%x %ls\n",offset,params.data?params.data:L"?");
                    if (params.data) ((void(*)(void*))(base+0x2ba6570))(params.data);
                }
            }
        }
    }
    fclose(log);
    return 0;
}
BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(module); CloseHandle(CreateThread(nullptr,0,Inspect,nullptr,0,nullptr)); }
    return TRUE;
}
#else
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 1;
    auto p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,wcstoul(argv[1],nullptr,10));
    wchar_t path[32768]; DWORD size=32768;
    if (!p || !QueryFullProcessImageNameW(p,0,path,&size) || _wcsicmp(path,L"E:\\Fortnite Battle Lab revival\\Fortnite-12.50-BattleLab\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping.exe")) return 2;
    const wchar_t dll[]=L"E:\\Fortnite Battle Lab revival\\Tools\\inspect_ai6.dll";
    auto mem=VirtualAllocEx(p,nullptr,sizeof(dll),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(p,mem,dll,sizeof(dll),nullptr)) return 3;
    auto thread=CreateRemoteThread(p,nullptr,0,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32"),"LoadLibraryW"),mem,0,nullptr);
    if (!thread) return 4;
    if (WaitForSingleObject(thread,10000)!=WAIT_OBJECT_0) return 5;
    DWORD result=0; GetExitCodeThread(thread,&result);
    VirtualFreeEx(p,mem,0,MEM_RELEASE);CloseHandle(thread);CloseHandle(p);
    return result ? 0 : 6;
}
#endif

