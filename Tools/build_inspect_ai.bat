@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /std:c++17 /EHsc /MT /LD /DINSPECT_DLL /FIinitializer_list inspect_ai.cpp /Fe:inspect_ai6.dll /link /NOLOGO || exit /b 1
cl /nologo /std:c++17 /EHsc /MT inspect_ai.cpp /Fe:inspect_ai.exe /link /NOLOGO

