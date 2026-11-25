# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Records inbox Notepad from process creation with Time Travel Debugging (monitor mode). Needs an ELEVATED PowerShell.
# Output: D:\DeepThought\ttd-notepad2\ (a .run trace of Notepad.exe only, about 1-3 GB; delete the folder afterwards if you like).
$ErrorActionPreference = 'Continue'
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'Please run this from an elevated PowerShell (Run as administrator).' -ForegroundColor Red; exit 1
}
$out = 'D:\DeepThought\ttd-notepad2'
New-Item -ItemType Directory -Force $out | Out-Null
Get-Process Notepad -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 2
# the monitor traces every Notepad.exe that starts while it runs
$mon = Start-Process -FilePath 'tttracer.exe' -ArgumentList @('-accepteula', '-noUI', '-out', $out, '-monitor', 'Notepad.exe') -PassThru -WindowStyle Minimized
Start-Sleep 4
Write-Host "Monitor started (pid $($mon.Id)); starting Notepad ..."
Start-Process 'explorer.exe' 'shell:AppsFolder\Microsoft.WindowsNotepad_8wekyb3d8bbwe!App'
Write-Host 'Recording for 60 seconds. Please leave Notepad alone (do not type or close it).'
Start-Sleep 60
Get-Process Notepad -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host 'Notepad closed; waiting for the trace to be written ...'
for ($i = 0; $i -lt 60; $i++) { if (-not (Get-Process tttracer -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $mon.Id })) { break }; Start-Sleep 2 }
Start-Sleep 5
if (-not $mon.HasExited) { Stop-Process -Id $mon.Id -Force }
Get-ChildItem $out | Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB) } } | Format-Table | Out-String | Write-Host
Write-Host 'Done. Tell Claude it is finished.' -ForegroundColor Green
