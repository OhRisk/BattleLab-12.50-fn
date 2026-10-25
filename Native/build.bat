@echo off
rem Builds BattleLabClient.dll and BattleLabLauncher.exe into Native\bin (MSVC 2022 Build Tools, x64 Release).
setlocal
set "VSDEV=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VSDEV%" >nul || (echo vcvars64 not found & exit /b 1)
cd /d "%~dp0"
if not exist bin mkdir bin
if not exist obj mkdir obj
if "%~1"=="launcher" goto launcher

cl /nologo /std:c++17 /O2 /MT /EHsc /W3 /LD /Fo"obj\\" BattleLabClient\*.cpp /Fe"bin\BattleLabClient.dll" /link /NOLOGO || exit /b 1
:launcher
cl /nologo /std:c++17 /O2 /MT /EHsc /W3 /Fo"obj\\" BattleLabLauncher\launcher.cpp /Fe"bin\BattleLabLauncher.exe" /link /NOLOGO || exit /b 1
if "%~1"=="launcher" goto done

rem Battle Lab game host: Project Reboot 3.0 (loopback-only local build) -> bin\BattleLabHost.dll
msbuild "ThirdParty\Project-Reboot-3.0-master\Project Reboot 3.0.sln" /m /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo /clp:ErrorsOnly || exit /b 1
copy /y "ThirdParty\Project-Reboot-3.0-master\x64\Release\Project Reboot 3.0.dll" "bin\BattleLabHost.dll" >nul || exit /b 1
:done
echo BUILD OK
