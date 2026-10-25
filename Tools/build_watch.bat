@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /std:c++17 /O2 /MT /EHsc /W3 watch_global.cpp /Fe:watch_global.exe /Fo:watch_global.obj /link /NOLOGO
cl /nologo /std:c++17 /O2 /MT /EHsc /W3 thread_stacks.cpp /Fe:thread_stacks.exe /Fo:thread_stacks.obj /link /NOLOGO
