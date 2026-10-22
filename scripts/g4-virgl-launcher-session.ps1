# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$')]
    [string] $RunName
)

$ErrorActionPreference = 'Stop'
$labRoot = 'D:\VirtualBox_VMs\WEX-G4-VirGL-20261022'
$wslExe = Join-Path $env:WINDIR 'System32\wsl.exe'
$linuxLauncher = '/mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/launch.py'
$outputDir = Join-Path $labRoot $RunName
$stdoutPath = Join-Path $labRoot ($RunName + '.wsl.stdout.log')
$stderrPath = Join-Path $labRoot ($RunName + '.wsl.stderr.log')
$sessionPath = Join-Path $labRoot ($RunName + '.wsl-session.json')

foreach ($path in @($outputDir, $stdoutPath, $stderrPath, $sessionPath)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing existing run artifact: $path" }
}
if (-not (Test-Path -LiteralPath $linuxLauncher -PathType Leaf)) {
    throw "Launcher missing: $linuxLauncher"
}

$arguments = @('-d', 'Debian', '--exec', 'unshare', '--mount', '--propagation', 'private',
    'env', "WEX_G4_RUN_NAME=$RunName", 'python3', '-B', $linuxLauncher)
$wsl = Start-Process -FilePath $wslExe -ArgumentList $arguments -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$record = [ordered]@{
    runName = $RunName
    runDir = $outputDir
    windowsPid = $wsl.Id
    windowsStartTimeUtc = $wsl.StartTime.ToUniversalTime().ToString('o')
    wslExecutable = $wslExe
    arguments = $arguments
    stdout = $stdoutPath
    stderr = $stderrPath
    state = 'running'
}
$temporaryRecord = $sessionPath + '.tmp'
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $temporaryRecord -Encoding utf8
Move-Item -LiteralPath $temporaryRecord -Destination $sessionPath

# Keep this PowerShell session alive while the consumer runs. Do not Ctrl-C it;
# a separate WSL stop command signals the verified Linux launcher identity.
$wsl.WaitForExit()
$record.state = 'exited'
$record.windowsExitCode = $wsl.ExitCode
$record.windowsExitTimeUtc = [DateTime]::UtcNow.ToString('o')
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $temporaryRecord -Encoding utf8
Move-Item -LiteralPath $temporaryRecord -Destination $sessionPath -Force
Write-Output ("WSL launcher exit code: {0}; session record: {1}" -f $record.windowsExitCode, $sessionPath)
