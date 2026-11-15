@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright 2026 WineExplorer contributors
rem Lab only (docs/SPPSVC_RESEARCH.md, path 2): the default values that Windows has under
rem SoftwareProtectionPlatform, read from the Windows 11 host (names and plain defaults, nothing machine specific).
rem Run as session 0's account.  The intervals are minutes; VLActivationInterval and VLRenewalInterval are the
rem documented KMS client retry and renewal intervals.
set K=HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\SoftwareProtectionPlatform
reg add "%K%" /v InactivityShutdownDelay /t REG_DWORD /d 30 /f /reg:64
reg add "%K%" /v KeepRunningThresholdMins /t REG_DWORD /d 15 /f /reg:64
reg add "%K%" /v VLActivationInterval /t REG_DWORD /d 120 /f /reg:64
reg add "%K%" /v VLRenewalInterval /t REG_DWORD /d 10080 /f /reg:64
reg add "%K%" /v SkipRearm /t REG_DWORD /d 0 /f /reg:64
reg add "%K%" /v NoExpirationUX /t REG_DWORD /d 0 /f /reg:64
reg add "%K%" /v UserOperations /t REG_DWORD /d 0 /f /reg:64
reg add "%K%" /v IgnoreContainerOrigin /t REG_DWORD /d 0 /f /reg:64
