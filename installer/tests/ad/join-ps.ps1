# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Runs in PowerShell 7 inside the lab guest, elevated (sent there by join-ps-guest.sh). The domain join from the
# PowerShell cmdlets of docs/POWERSHELL.md: Add-Computer, Test-ComputerSecureChannel, Reset-ComputerMachinePassword,
# Rename-Computer and Remove-Computer, with the WexComputer module found without Import-Module (it sits in
# $PSHOME\Modules). Results go to Z:\tmp\ps-join.out; at a stage the guest checks the directory, the script waits for
# Z:\tmp\ps-continue.
$ErrorActionPreference = 'Continue'
$out = 'Z:\tmp\ps-join.out'
function Log($m) { Add-Content -Path $out -Value $m -Encoding ascii }
function Stage($name)
{
    Log "STAGE $name"
    Set-Content -Path "Z:\tmp\ps-stage-$name" -Value 'x' -Encoding ascii
    for ($i = 0; $i -lt 120; $i++) { if (Test-Path "Z:\tmp\ps-continue-$name") { break }; Start-Sleep -Seconds 2 }
}
function Step($name, [scriptblock]$code)
{
    try { $r = & $code 2>&1 | Out-String; Log "RESULT $name ok: $($r.Trim())" }
    catch
    {
        $e = $_.Exception
        $m = $e.Message
        while ($e.InnerException) { $e = $e.InnerException; $m += " <- $($e.GetType().Name): $($e.Message)" }
        Log "RESULT $name error: $m"
    }
}

$dom = 'ad.wexlab.example'
$ou = 'OU=WexMachines,DC=ad,DC=wexlab,DC=example'
$pw = (Get-Content -Path 'Z:\tmp\join-pw.txt' -Raw).Trim()
$cred = New-Object System.Management.Automation.PSCredential('Administrator', (ConvertTo-SecureString $pw -AsPlainText -Force))
$pw = $null
Log "pwsh $($PSVersionTable.PSVersion) pshome=$PSHOME"

Step autoload { (Get-Command Add-Computer, Remove-Computer, Test-ComputerSecureChannel, Reset-ComputerMachinePassword -ErrorAction Stop | ForEach-Object { "$($_.Name) from $($_.Source)" }) -join '; ' }
$orig = [Environment]::MachineName
Step cim_before { Get-CimInstance Win32_ComputerSystem | ForEach-Object { "Name=$($_.Name) DNSHostName=$($_.DNSHostName) PartOfDomain=$($_.PartOfDomain) Domain=$($_.Domain) Workgroup=$($_.Workgroup)" } }
Step sc_before { Test-ComputerSecureChannel -ErrorAction Stop }
Step add { Add-Computer -DomainName $dom -Credential $cred -OUPath $ou -PassThru -ErrorAction Stop }
Step cim_joined { Get-CimInstance Win32_ComputerSystem | ForEach-Object { "Name=$($_.Name) PartOfDomain=$($_.PartOfDomain) Domain=$($_.Domain)" } }
Step sc_joined { Test-ComputerSecureChannel -ErrorAction Stop }
Step add_again { Add-Computer -DomainName $dom -Credential $cred -ErrorAction Stop }
Step reset_pw { Reset-ComputerMachinePassword -Server "dc1.$dom" -Credential $cred -ErrorAction Stop }
Step sc_after_reset { Test-ComputerSecureChannel -ErrorAction Stop }
Stage joined

Step rename { Rename-Computer -NewName pslab1 -DomainCredential $cred -Force -PassThru -ErrorAction Stop }
Step cim_renamed { Get-CimInstance Win32_ComputerSystem | ForEach-Object { "Name=$($_.Name) DNSHostName=$($_.DNSHostName) PartOfDomain=$($_.PartOfDomain)" } }
Stage renamed

# (a running session keeps the name it started with, as Windows keeps the active name until a restart, so
# Rename-Computer would find the old name equal to the one asked for: the way back goes through the method itself)
Step rename_back {
    $plain = (Get-Content -Path 'Z:\tmp\join-pw.txt' -Raw).Trim()
    $r = Invoke-CimMethod -InputObject (Get-CimInstance Win32_ComputerSystem) -MethodName Rename -Arguments @{ Name = $orig; UserName = 'Administrator'; Password = $plain }
    $plain = $null
    "ReturnValue=$($r.ReturnValue)"
}
Step remove { Remove-Computer -UnjoinDomainCredential $cred -Force -PassThru -ErrorAction Stop }
Step cim_after { Get-CimInstance Win32_ComputerSystem | ForEach-Object { "Name=$($_.Name) PartOfDomain=$($_.PartOfDomain) Workgroup=$($_.Workgroup)" } }
Step sc_after { Test-ComputerSecureChannel -ErrorAction Stop }
Log '=== done'
