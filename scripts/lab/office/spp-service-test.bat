@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright 2026 WineExplorer contributors
rem Lab only (docs/SPPSVC_RESEARCH.md, path 2): register a locally held copy of the host's licensing service as a
rem service of session 0 and start it.  The copy is never committed.  Run as session 0's account.
sc create sppsvc binPath= "C:\spp-local\sppsvc.exe" type= own start= demand obj= "NT AUTHORITY\NetworkService" DisplayName= "Software Protection (local test)"
sc start sppsvc
ping -n 8 127.0.0.1 > nul
sc query sppsvc
