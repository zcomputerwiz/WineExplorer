@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright 2026 WineExplorer contributors
rem Overwrite a font that the machine root links to an open font (docs/OFFICE_BUG_NOTES.md OB-5).
rem Run as session 0's account.  Before the server change: "Access is denied"; after: "1 file(s) copied".
echo test> Z:\tmp\fonttest.ttf
copy /y Z:\tmp\fonttest.ttf C:\windows\Fonts\segoeui.ttf
