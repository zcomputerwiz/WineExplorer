# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
@{
    RootModule        = 'WexComputer.psm1'
    ModuleVersion     = '1.0.0'
    GUID              = '7d1e2f44-6a1b-4c5e-9f0a-3b8c2d5e7a10'
    Author            = 'WineExplorer contributors'
    Description       = 'Add-Computer, Remove-Computer, Test-ComputerSecureChannel and Reset-ComputerMachinePassword for PowerShell 7 on WineExplorer, over Win32_ComputerSystem.'
    PowerShellVersion = '7.0'
    CompatiblePSEditions = @('Core')
    FunctionsToExport = @('Add-Computer', 'Remove-Computer', 'Test-ComputerSecureChannel', 'Reset-ComputerMachinePassword')
    CmdletsToExport   = @()
    VariablesToExport = @()
    AliasesToExport   = @()
}
