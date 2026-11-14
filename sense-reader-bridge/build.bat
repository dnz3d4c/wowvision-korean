@echo off
rem Builds the Sense Reader proxy (x64) into out\nvdaControllerClient.dll.
rem   build.bat        the DLL only
rem   build.bat test   the DLL, then the offline tests (out\test_bridge.exe)
rem Needs Visual Studio 2022 Build Tools (C++ workload).
setlocal
set VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
set CFLAGS=/nologo /utf-8 /W4 /WX /O2 /MT /DUNICODE /D_UNICODE
set LIBS=kernel32.lib user32.lib ole32.lib oleaut32.lib
cl %CFLAGS% /LD koSenseReader.c /Fo:out\ /Fe:out\nvdaControllerClient.dll /link /DEF:nvdaControllerClient.def /BREPRO %LIBS% || exit /b 1
if /i not "%~1"=="test" exit /b 0
cl %CFLAGS% /LD tests\fake_next.c /Fo:out\ /Fe:out\nvdaControllerClient.next.dll /link /DEF:tests\fake_next.def || exit /b 1
cl %CFLAGS% tests\test_bridge.c /Fo:out\ /Fe:out\test_bridge.exe /link %LIBS% || exit /b 1
out\test_bridge.exe
