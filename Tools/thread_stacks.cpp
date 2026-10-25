// Local process diagnostic; no symbol server or network access.
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#pragma comment(lib,"dbghelp.lib")
int wmain(int argc,wchar_t**argv) {
 if(argc!=2)return 2;
 DWORD pid=wcstoul(argv[1],nullptr,10);
 HANDLE proc=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
 if(!proc){printf("open failed %lu\n",GetLastError());return 1;}
 SymSetOptions(SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
 SymInitialize(proc,"",TRUE);
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
 THREADENTRY32 e{sizeof(e)};
 if(Thread32First(snap,&e))do {
  if(e.th32OwnerProcessID!=pid)continue;
  HANDLE t=OpenThread(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_INFORMATION,FALSE,e.th32ThreadID);
  if(!t)continue;
  if(SuspendThread(t)==DWORD(-1)){CloseHandle(t);continue;}
  CONTEXT c{};c.ContextFlags=CONTEXT_FULL;
  if(GetThreadContext(t,&c)) {
   STACKFRAME64 s{};s.AddrPC.Offset=c.Rip;s.AddrStack.Offset=c.Rsp;s.AddrFrame.Offset=c.Rbp;
   s.AddrPC.Mode=s.AddrStack.Mode=s.AddrFrame.Mode=AddrModeFlat;
   printf("THREAD %lu\n",e.th32ThreadID);
   for(int i=0;i<18;++i) {
    DWORD64 base=SymGetModuleBase64(proc,s.AddrPC.Offset);
    IMAGEHLP_MODULE64 mod{};mod.SizeOfStruct=sizeof(mod);
    SymGetModuleInfo64(proc,s.AddrPC.Offset,&mod);
    printf(" %d %s +%llx pc=%llx\n",i,mod.ModuleName,s.AddrPC.Offset-base,s.AddrPC.Offset);
    if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,proc,t,&s,&c,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
   }
  }
  ResumeThread(t);CloseHandle(t);
 }while(Thread32Next(snap,&e));
 CloseHandle(snap);SymCleanup(proc);CloseHandle(proc);return 0;
}
