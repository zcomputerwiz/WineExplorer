# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Add-Computer, Remove-Computer, Test-ComputerSecureChannel and Reset-ComputerMachinePassword for PowerShell 7.
#
# PowerShell 7 left these four in Windows PowerShell 5.1 (it needs .NET Framework). They are written here over the
# same interface the 5.1 cmdlets use, Win32_ComputerSystem.JoinDomainOrWorkgroup and UnjoinDomainOrWorkgroup (wbemprox,
# docs/POWERSHELL.md), which ask the domain join engine. Rename-Computer is PowerShell 7's own. No restart is ever
# needed (the changes take effect at once), so -Restart is accepted and only says so.

Set-StrictMode -Version 3.0

$script:JoinDomain = 0x1
$script:AcctCreate = 0x2
$script:AcctDelete = 0x4

function Get-WexErrorText([uint32]$Code)
{
    ([System.ComponentModel.Win32Exception]::new([int]$Code)).Message
}

function Get-WexComputerSystem
{
    Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop
}

function Get-WexPlainPassword([pscredential]$Credential)
{
    # the CIM method takes the password as a string, as Windows PowerShell's cmdlets pass it
    if ($null -eq $Credential) { return $null }
    $Credential.GetNetworkCredential().Password
}

function Invoke-WexComputerMethod($Method, [hashtable]$Arguments)
{
    $cs = Get-WexComputerSystem
    $result = Invoke-CimMethod -InputObject $cs -MethodName $Method -Arguments $Arguments -ErrorAction Stop
    [uint32]$result.ReturnValue
}

function Add-Computer
{
    [CmdletBinding(DefaultParameterSetName = 'Domain', SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
    param(
        [Parameter(Mandatory = $true, Position = 0, ParameterSetName = 'Domain')]
        [Alias('DN', 'Domain')]
        [string]$DomainName,

        [Parameter(Mandatory = $true, ParameterSetName = 'Workgroup')]
        [Alias('WGN')]
        [string]$WorkgroupName,

        [Parameter(Mandatory = $true, ParameterSetName = 'Domain')]
        [Parameter(ParameterSetName = 'Workgroup')]
        [pscredential]$Credential,

        [Parameter(ParameterSetName = 'Domain')]
        [pscredential]$UnjoinDomainCredential,

        [Parameter(ParameterSetName = 'Domain')]
        [Alias('OU')]
        [string]$OUPath,

        [Parameter(ParameterSetName = 'Domain')]
        [string]$Server,

        [string]$NewName,
        [switch]$Restart,
        [switch]$Force,
        [switch]$PassThru,
        [switch]$Unsecure,
        [switch]$Options
    )

    $cs = Get-WexComputerSystem
    $local = $cs.Name

    if ($PSCmdlet.ParameterSetName -eq 'Workgroup')
    {
        # the join of a workgroup is not offered by the engine (ERROR_NOT_SUPPORTED), as in System Properties
        $e = [System.ComponentModel.Win32Exception]::new(50)
        $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
            [System.InvalidOperationException]::new("Cannot join the workgroup '$WorkgroupName': " + $e.Message),
            'WorkgroupJoinNotSupported', [System.Management.Automation.ErrorCategory]::NotImplemented, $WorkgroupName))
        return
    }
    if ($cs.PartOfDomain)
    {
        $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
            [System.InvalidOperationException]::new("Computer '$local' is already in domain '$($cs.Domain)'."),
            'AlreadyInDomain', [System.Management.Automation.ErrorCategory]::InvalidOperation, $local))
        return
    }
    if (-not $PSCmdlet.ShouldProcess($local, "Add computer to domain '$DomainName'")) { return }

    $arguments = @{
        Name         = $DomainName
        UserName     = $Credential.UserName
        Password     = (Get-WexPlainPassword $Credential)
        FJoinOptions = [uint32]($script:JoinDomain -bor $script:AcctCreate)
    }
    if ($OUPath) { $arguments.AccountOU = $OUPath }
    $status = Invoke-WexComputerMethod 'JoinDomainOrWorkgroup' $arguments
    if ($status -ne 0)
    {
        $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
            [System.InvalidOperationException]::new("Computer '$local' failed to join domain '$DomainName': " + (Get-WexErrorText $status) + " ($status)"),
            'FailToJoinDomain', [System.Management.Automation.ErrorCategory]::OperationStopped, $DomainName))
        return
    }
    if ($NewName)
    {
        $status = Invoke-WexComputerMethod 'Rename' @{ Name = $NewName; UserName = $Credential.UserName; Password = (Get-WexPlainPassword $Credential) }
        if ($status -ne 0)
        {
            $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
                [System.InvalidOperationException]::new("Computer '$local' joined '$DomainName' but could not be renamed to '$NewName': " + (Get-WexErrorText $status) + " ($status)"),
                'FailToRenameAfterJoin', [System.Management.Automation.ErrorCategory]::OperationStopped, $NewName))
            return
        }
    }
    if ($Restart) { Write-Verbose 'No restart is needed: the change is in force.' }
    Write-Warning "The changes will take effect at once; no restart is needed."
    if ($PassThru) { Get-WexComputerSystem }
}

function Remove-Computer
{
    [CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
    param(
        [Parameter(Position = 0)]
        [Alias('UDC')]
        [pscredential]$UnjoinDomainCredential,

        [Alias('WGN')]
        [string]$WorkgroupName,

        [switch]$Restart,
        [switch]$Force,
        [switch]$PassThru
    )

    $cs = Get-WexComputerSystem
    $local = $cs.Name
    if (-not $cs.PartOfDomain)
    {
        $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
            [System.InvalidOperationException]::new("Computer '$local' is not in a domain."),
            'NotInDomain', [System.Management.Automation.ErrorCategory]::InvalidOperation, $local))
        return
    }
    if ($WorkgroupName -and $WorkgroupName -ne 'WORKGROUP')
    {
        Write-Warning "The workgroup name is not changed here: the computer leaves the domain for WORKGROUP."
    }
    if (-not $PSCmdlet.ShouldProcess($local, "Remove computer from domain '$($cs.Domain)'")) { return }

    $arguments = @{ FUnjoinOptions = [uint32]$script:AcctDelete }
    if ($UnjoinDomainCredential)
    {
        $arguments.UserName = $UnjoinDomainCredential.UserName
        $arguments.Password = (Get-WexPlainPassword $UnjoinDomainCredential)
    }
    $status = Invoke-WexComputerMethod 'UnjoinDomainOrWorkgroup' $arguments
    if ($status -ne 0)
    {
        $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
            [System.InvalidOperationException]::new("Computer '$local' failed to leave domain '$($cs.Domain)': " + (Get-WexErrorText $status) + " ($status)"),
            'FailToRemoveFromDomain', [System.Management.Automation.ErrorCategory]::OperationStopped, $local))
        return
    }
    if ($Restart) { Write-Verbose 'No restart is needed: the change is in force.' }
    Write-Warning "The changes will take effect at once; no restart is needed."
    if ($PassThru) { Get-WexComputerSystem }
}

function Invoke-WexNltest([string[]]$Arguments)
{
    $exe = Get-Command nltest.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $exe) { throw 'nltest.exe is not installed.' }
    $text = & $exe.Source @Arguments 2>&1 | Out-String
    [pscustomobject]@{ ExitCode = $LASTEXITCODE; Text = $text }
}

function Test-ComputerSecureChannel
{
    [CmdletBinding()]
    param(
        [switch]$Repair,
        [string]$Server,
        [pscredential]$Credential
    )

    $cs = Get-WexComputerSystem
    if (-not $cs.PartOfDomain)
    {
        $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
            [System.InvalidOperationException]::new("The computer '$($cs.Name)' is not in a domain."),
            'NotInDomain', [System.Management.Automation.ErrorCategory]::InvalidOperation, $cs.Name))
        return $false
    }
    $domain = $cs.Domain
    $r = Invoke-WexNltest @("/sc_verify:$domain")
    if ($r.ExitCode -eq 0) { return $true }
    if ($Repair)
    {
        $r = Invoke-WexNltest @("/sc_change_pwd:$domain")
        if ($r.ExitCode -eq 0) { return $true }
    }
    $false
}

function Reset-ComputerMachinePassword
{
    [CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
    param(
        [string]$Server,
        [pscredential]$Credential
    )

    $cs = Get-WexComputerSystem
    if (-not $cs.PartOfDomain)
    {
        $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
            [System.InvalidOperationException]::new("The computer '$($cs.Name)' is not in a domain."),
            'NotInDomain', [System.Management.Automation.ErrorCategory]::InvalidOperation, $cs.Name))
        return
    }
    if (-not $PSCmdlet.ShouldProcess($cs.Name, "Reset the machine account password in '$($cs.Domain)'")) { return }
    $r = Invoke-WexNltest @("/sc_change_pwd:$($cs.Domain)")
    if ($r.ExitCode -ne 0)
    {
        $PSCmdlet.WriteError([System.Management.Automation.ErrorRecord]::new(
            [System.InvalidOperationException]::new("The machine account password could not be reset: " + $r.Text.Trim()),
            'FailToResetPassword', [System.Management.Automation.ErrorCategory]::OperationStopped, $cs.Name))
    }
}

Export-ModuleMember -Function Add-Computer, Remove-Computer, Test-ComputerSecureChannel, Reset-ComputerMachinePassword
