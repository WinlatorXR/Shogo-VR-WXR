@echo off
rem Builds ShogoVR.exe (the minimal WinlatorXR container launcher).
rem Requires Visual Studio 2022+ Build Tools with the C++ x86 toolchain.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x86 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
rc.exe /nologo /fo ShogoVR.res ShogoVR.rc
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /c ShogoVR.cpp
if errorlevel 1 exit /b 1
link.exe /nologo /SUBSYSTEM:WINDOWS /OUT:ShogoVR.exe ShogoVR.obj ShogoVR.res user32.lib
if errorlevel 1 exit /b 1
echo ShogoVR.exe built.