# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
param([string]$Script, [string]$Log, [int]$TimeoutSec = 280, [string]$Trace = 'D:\DeepThought\ttd-notepad2\Notepad01.run')
# runs cdb on a Notepad TTD trace with a command file and writes the log (symbols from the Microsoft server)
$cdb = 'C:\Program Files\WindowsApps\Microsoft.WinDbg_1.2611.4001.0_x64__8wekyb3d8bbwe\amd64\cdb.exe'
$env:_NT_SYMBOL_PATH = 'srv*C:\Users\Owner\AppData\Local\Temp\symcache*https://msdl.microsoft.com/download/symbols'
$p = Start-Process -FilePath $cdb -ArgumentList @('-z', $Trace, '-cf', $Script, '-logo', $Log) -PassThru -WindowStyle Minimized
if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); 'timed out' }
