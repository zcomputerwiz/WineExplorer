@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright 2026 WineExplorer contributors
rem Run token-probe.exe as a service of session 0, as LocalSystem and as NetworkService, to see the token a
rem service process gets.  The probe's output goes to the journal of wex-session0-services.service.  Run as
rem session 0's account.
sc create wextokls binPath= "Z:\home\wineuser\office\lab\token-probe.exe" type= own obj= LocalSystem
sc start wextokls
ping -n 6 127.0.0.1 > nul
sc delete wextokls
sc create wextokns binPath= "Z:\home\wineuser\office\lab\token-probe.exe" type= own obj= "NT AUTHORITY\NetworkService"
sc start wextokns
ping -n 6 127.0.0.1 > nul
sc delete wextokns
