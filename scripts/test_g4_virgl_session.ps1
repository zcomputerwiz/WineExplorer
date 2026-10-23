# SPDX-License-Identifier: GPL-2.0-or-later
# Offline: execute the real controller through path validation/conversion only.
# The process boundary is intercepted BEFORE any launcher or log write.
$ErrorActionPreference = 'Stop'
$controller = Join-Path $PSScriptRoot 'g4-virgl-launcher-session.ps1'
$tokens = $null; $parseErrors = $null
$null = [System.Management.Automation.Language.Parser]::ParseFile($controller, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
$runName = 'offline-portability-' + [guid]::NewGuid().ToString('N')
$global:g4OfflineIntercepted = $null
function Start-Process {
    param($FilePath, $ArgumentList, [switch]$PassThru, $WindowStyle,
          $RedirectStandardOutput, $RedirectStandardError)
    $global:g4OfflineIntercepted = @{
        executable = $FilePath; arguments = $ArgumentList; style = $WindowStyle
        stdout = $RedirectStandardOutput; stderr = $RedirectStandardError
    }
    throw 'OFFLINE_PROCESS_BOUNDARY'
}
try {
    & $controller -RunName $runName
    throw 'Controller unexpectedly returned'
} catch {
    if ($_.Exception.Message -ne 'OFFLINE_PROCESS_BOUNDARY') { throw }
}
if (-not $global:g4OfflineIntercepted) { throw 'Missing process interception' }
$expectedWindows = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\g4-virgl-20261022\launch.py'
$expectedLinux = & "$env:WINDIR\System32\wsl.exe" -d Debian --exec wslpath -u $expectedWindows
if ($LASTEXITCODE -ne 0) { throw 'Expected path conversion failed' }
$expectedArgs = @('-d', 'Debian', '--exec', 'unshare', '--mount', '--propagation', 'private',
    'env', "WEX_G4_RUN_NAME=$runName", 'python3', '-B', $expectedLinux)
if (($global:g4OfflineIntercepted.arguments | ConvertTo-Json -Compress) -ne ($expectedArgs | ConvertTo-Json -Compress)) {
    throw 'Controller did not use its own checkout launcher with unchanged isolation'
}
if ($global:g4OfflineIntercepted.style -ne 'Hidden') { throw 'Controller lost hidden-window policy' }
$lab = 'D:\VirtualBox_VMs\WEX-G4-VirGL-20261022'
foreach ($suffix in @('', '.wsl.stdout.log', '.wsl.stderr.log', '.wsl-session.json')) {
    if (Test-Path -LiteralPath (Join-Path $lab ($runName + $suffix))) { throw 'Offline test created run artifacts' }
}
if ($global:g4OfflineIntercepted.stdout -ne (Join-Path $lab ($runName + '.wsl.stdout.log')) -or
    $global:g4OfflineIntercepted.stderr -ne (Join-Path $lab ($runName + '.wsl.stderr.log'))) { throw 'Logs left D: lab' }
Remove-Variable -Name g4OfflineIntercepted -Scope Global
Write-Output "PASS: real controller parses and selects $expectedLinux; process intercepted, no guest/run artifacts"
