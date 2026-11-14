@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright 2026 WineExplorer contributors
rem build xamlproxy.dll (x64) into build\x64 (not "xamlshim.dll": file names are case-insensitive and the managed
rem XamlShim.dll lives in the same directory)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist "%~dp0build\x64" mkdir "%~dp0build\x64"
cd /d "%~dp0build\x64"
cl /nologo /W3 /O2 /LD /D_CRT_SECURE_NO_WARNINGS "%~dp0xamlproxy.c" /Fe:xamlproxy.dll /link /DEF:"%~dp0xamlproxy.def" runtimeobject.lib advapi32.lib
