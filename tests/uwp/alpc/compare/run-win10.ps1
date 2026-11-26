# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
<#
.SYNOPSIS
  Runs the probes of probes.txt in the Windows 10 lab VM (Copilot-Win10-B, see docs/WINDOWS_LAB_VM.md) and compares the normalised output
  with the Windows 11 golden files in expected/. Shows which behaviours differ between Windows versions.

.DESCRIPTION
  The VM must be running with Guest Additions (guest control answers). Probes are copied to C:\Users\lab\probes, run with a timeout,
  their output is fetched. Nothing else in the guest is touched. Writes out/NAME.win10.txt and report-win10.md.
#>
param(
    [string[]]$Only = @(),
    [string]$Vm = 'Copilot-Win10-B',
    [string]$PwFile = 'C:\Users\Owner\msisamples\lab-win10.pw',
    [string]$Vb = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot; $alpc = Split-Path $here -Parent
$build = Join-Path $here 'build'; $out = Join-Path $here 'out'; $exp = Join-Path $here 'expected'
New-Item -ItemType Directory -Force $build, $out | Out-Null
$wslHere = (wsl wslpath -a ($here -replace '\\', '/')).Trim()
$python = if (Get-Command python -ErrorAction SilentlyContinue) { 'python' } else { 'py' }
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

function Guest { param([string[]]$a) $sub = $a[0]; $rest = @($a | Select-Object -Skip 1); & $Vb guestcontrol $Vm $sub --username lab --passwordfile $PwFile @rest 2>&1 }

$probes = Get-Content (Join-Path $here 'probes.txt') | Where-Object { $_ -and $_ -notmatch '^\s*#' } | ForEach-Object {
    $p = $_ -split '\|'; [pscustomobject]@{ Name = $p[0].Trim(); Timeout = [int]$p[1].Trim(); Extra = $p[2].Trim(); What = $p[3].Trim() }
}
if ($Only.Count) { $probes = @($probes | Where-Object { $Only -contains $_.Name }) }
(Guest @('run', '--exe', 'C:\Windows\System32\cmd.exe', '--', 'cmd.exe', '/c', 'mkdir C:\Users\lab\probes 2>nul & ver')) | Out-Null

$report = @("# Windows 10 versus Windows 11 (golden files)", "", "Windows 10 build as reported by the guest; Windows 11 is the host.", "")
$rows = @(); $bad = 0
foreach ($p in $probes) {
    $name = $p.Name; Write-Host "== $name" -ForegroundColor Cyan
    $exe = Join-Path $build "$name.exe"
    if (-not (Test-Path $exe)) {
        wsl -e bash -c "cd '$wslHere/..' && x86_64-w64-mingw32-gcc -O1 -o 'compare/build/$name.exe' '$name.c' -luser32 -lntdll 2>&1 | head -5"
    }
    if (-not (Test-Path $exe)) { $rows += "| $name | build failed | |"; $bad++; continue }
    Guest @('copyto', '--target-directory', 'C:\Users\lab\probes\', $exe) | Out-Null
    $raw = Guest @('run', '--timeout', ([string]($p.Timeout * 1000)), '--exe', "C:\Users\lab\probes\$name.exe", '--wait-stdout', '--', "$name.exe") | Out-String
    [IO.File]::WriteAllText((Join-Path $out "$name.win10.raw"), $raw)
    $args2 = @((Join-Path $here 'normalize.py')); if ($p.Extra) { $args2 += @('--extra', $p.Extra) }
    $norm = ($raw -split "`r?`n" -join "`n") | & $python @args2
    [IO.File]::WriteAllText((Join-Path $out "$name.win10.txt"), (($norm -join "`n") + "`n"))
    $golden = Join-Path $exp "$name.windows.txt"
    if (-not (Test-Path $golden)) { $rows += "| $name | no Windows 11 golden file | $($p.What) |"; continue }
    $diff = (& $python (Join-Path $here 'diff.py') $golden (Join-Path $out "$name.win10.txt")) -join "`n"
    if ($LASTEXITCODE -eq 0) { Write-Host "same as Windows 11" -ForegroundColor Green; $rows += "| $name | same as Windows 11 | $($p.What) |" }
    else {
        $bad++; Write-Host "differs from Windows 11" -ForegroundColor Yellow
        $rows += "| $name | **differs** | $($p.What) |"
        $report += "## $name  ($($p.What))", "", '```diff', $diff, '```', ''
    }
}
$head = @("| probe | result | what it checks |", "|---|---|---|") + $rows
$report = $report[0..3] + $head + "" + $report[4..($report.Count - 1)]
[IO.File]::WriteAllText((Join-Path $here 'report-win10.md'), ($report -join "`n"))
Write-Host "report: $(Join-Path $here 'report-win10.md')  ($bad probe(s) differ)"
