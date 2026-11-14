@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright 2026 WineExplorer contributors
rem build xamlhost.exe (x64) into the published x64 UnoSpike directory
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %TEMP%\claude\uno-spike\out-win-x64
cl /nologo /W3 /O2 /D_CRT_SECURE_NO_WARNINGS "%~dp0xamlhost.c" /Fe:xamlhost.exe ole32.lib
