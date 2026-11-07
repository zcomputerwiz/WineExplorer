# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Read-only host inventory script for the WineExplorer UWP track.
# (docs/uwp/UWP_COMPATIBILITY_PLAN.md Phase 1, first bullet; docs/uwp/README.md)
#
# Performs strictly read-only inspection of the Windows oracle host:
#   - Windows edition, build, UBR, and architecture
#   - Windows SDK versions, MakeAppx.exe, SignTool.exe, and SDK C++/WinRT headers/compiler
#   - Visual Studio installations and C++ / UWP workloads via vswhere.exe
#   - NuGet CLI (nuget.exe) and C++/WinRT NuGet package cache
#   - MinGW-w64 and Clang/LLVM toolchains with WinRT headers
#   - Developer Mode state (read-only registry query)
#   - Sideloading policy state (read-only registry query)
#   - Local test accounts (names only)
#   - Synthesis of what exists, what is missing, and what is required to build a packaged C++/WinRT fixture
#
# Optional Calculator package metadata export:
#   If -CalculatorMetadataPath is specified (or defaulted) and -SkipCalculatorExport is not passed,
#   queries Get-AppxPackage for Microsoft.WindowsCalculator (metadata only) and writes it
#   exclusively to the external file outside this repository.
#   NEVER write Calculator metadata into the WineExplorer repository.

[CmdletBinding()]
param(
    [string]$CalculatorMetadataPath = 'D:\DeepThought\Projects\WineExplorer-local\uwp-oracle\calculator-metadata.txt',
    [switch]$SkipCalculatorExport
)

$ErrorActionPreference = 'Stop'

function Get-HostInventoryReport {
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("================================================================================")
    $lines.Add(" WINEEXPLORER UWP TRACK - ORACLE HOST INVENTORY REPORT")
    $lines.Add(" Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss K')")
    $lines.Add(" Mode: Read-Only (no system, registry, or package modifications performed)")
    $lines.Add("================================================================================")
    $lines.Add("")

    # 1. OS & Architecture
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add(" 1. OPERATING SYSTEM & ARCHITECTURE")
    $lines.Add("--------------------------------------------------------------------------------")
    $ntCurrent = Get-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion" -ErrorAction SilentlyContinue
    $productName = if ($ntCurrent) { $ntCurrent.ProductName } else { [Environment]::OSVersion.VersionString }
    $displayVer = if ($ntCurrent -and $ntCurrent.DisplayVersion) { $ntCurrent.DisplayVersion } else { "N/A" }
    $currentBuild = if ($ntCurrent) { $ntCurrent.CurrentBuild } else { [Environment]::OSVersion.Version.Build }
    $ubr = if ($ntCurrent -and $ntCurrent.UBR) { $ntCurrent.UBR } else { "N/A" }
    $editionId = if ($ntCurrent -and $ntCurrent.EditionID) { $ntCurrent.EditionID } else { "N/A" }

    $osArchitecture = if ([System.Environment]::Is64BitOperatingSystem) { "64-bit (x64)" } else { "32-bit (x86)" }
    $cpuArchitecture = $env:PROCESSOR_ARCHITECTURE

    $lines.Add("  Product Name:         $productName")
    $lines.Add("  Display Version:      $displayVer")
    $lines.Add("  Windows Edition ID:   $editionId")
    $lines.Add("  Build Number:         $currentBuild")
    $lines.Add("  Update Build (UBR):   $ubr")
    $lines.Add("  Full OS Version:      10.0.$currentBuild.$ubr")
    $lines.Add("  OS Architecture:      $osArchitecture")
    $lines.Add("  Processor Arch:       $cpuArchitecture")
    $lines.Add("")

    # 2. Windows SDK & Tooling under Program Files (x86)\Windows Kits
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add(" 2. WINDOWS SDK & PACKAGING TOOLING")
    $lines.Add("--------------------------------------------------------------------------------")
    $sdkRoot = "C:\Program Files (x86)\Windows Kits\10"
    $sdkRootExists = Test-Path -LiteralPath $sdkRoot

    $lines.Add("  Windows Kits 10 Root: $sdkRoot (Exists: $sdkRootExists)")

    $sdkIncludeVersions = @()
    $sdkBinVersions = @()
    $makeAppxPaths = @()
    $signToolPaths = @()
    $cppwinrtToolPaths = @()
    $cppwinrtHeaderPaths = @()

    if ($sdkRootExists) {
        # Check Include versions
        $incPath = Join-Path $sdkRoot "Include"
        if (Test-Path -LiteralPath $incPath) {
            $sdkIncludeVersions = Get-ChildItem -LiteralPath $incPath -Directory | Select-Object -ExpandProperty Name
        }

        # Check bin versions
        $binPath = Join-Path $sdkRoot "bin"
        if (Test-Path -LiteralPath $binPath) {
            $sdkBinVersions = Get-ChildItem -LiteralPath $binPath -Directory | Select-Object -ExpandProperty Name
        }

        # MakeAppx.exe
        $makeAppxFiles = Get-ChildItem -LiteralPath $sdkRoot -Recurse -Filter "makeappx.exe" -ErrorAction SilentlyContinue
        if ($makeAppxFiles) {
            $makeAppxPaths = $makeAppxFiles | Select-Object -ExpandProperty FullName
        }

        # SignTool.exe
        $signToolFiles = Get-ChildItem -LiteralPath $sdkRoot -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue
        if ($signToolFiles) {
            $signToolPaths = $signToolFiles | Select-Object -ExpandProperty FullName
        }

        # cppwinrt.exe
        $cppwinrtFiles = Get-ChildItem -LiteralPath $sdkRoot -Recurse -Filter "cppwinrt.exe" -ErrorAction SilentlyContinue
        if ($cppwinrtFiles) {
            $cppwinrtToolPaths = $cppwinrtFiles | Select-Object -ExpandProperty FullName
        }

        # cppwinrt headers
        foreach ($v in $sdkIncludeVersions) {
            $candCppWinrt = Join-Path $incPath "$v\cppwinrt"
            if (Test-Path -LiteralPath $candCppWinrt) {
                $cppwinrtHeaderPaths += $candCppWinrt
            }
            $candWinrt = Join-Path $incPath "$v\winrt"
            if (Test-Path -LiteralPath $candWinrt) {
                $cppwinrtHeaderPaths += $candWinrt
            }
        }
    }

    $lines.Add("  SDK Include Versions: $(if ($sdkIncludeVersions.Count) { $sdkIncludeVersions -join ', ' } else { 'None' })")
    $lines.Add("  SDK Bin Directories:  $(if ($sdkBinVersions.Count) { $sdkBinVersions -join ', ' } else { 'None' })")
    $lines.Add("  MakeAppx.exe Present: $(if ($makeAppxPaths.Count) { 'Yes (' + $makeAppxPaths.Count + ' found)' } else { 'No' })")
    foreach ($p in $makeAppxPaths) {
        $lines.Add("    - $p")
    }
    $lines.Add("  SignTool.exe Present: $(if ($signToolPaths.Count) { 'Yes (' + $signToolPaths.Count + ' found)' } else { 'No' })")
    foreach ($p in $signToolPaths) {
        $lines.Add("    - $p")
    }
    $lines.Add("  cppwinrt.exe Present: $(if ($cppwinrtToolPaths.Count) { 'Yes (' + $cppwinrtToolPaths.Count + ' found)' } else { 'No' })")
    foreach ($p in $cppwinrtToolPaths) {
        $lines.Add("    - $p")
    }
    $lines.Add("  C++/WinRT Headers:    $(if ($cppwinrtHeaderPaths.Count) { 'Yes (' + $cppwinrtHeaderPaths.Count + ' directories found)' } else { 'No' })")
    foreach ($p in $cppwinrtHeaderPaths) {
        $lines.Add("    - $p")
    }
    $lines.Add("")

    # 3. Visual Studio Installations & Workloads via vswhere.exe
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add(" 3. VISUAL STUDIO INSTALLATIONS & WORKLOADS (vswhere)")
    $lines.Add("--------------------------------------------------------------------------------")
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vswhereExists = Test-Path -LiteralPath $vswhere
    $lines.Add("  vswhere.exe:          $vswhere (Exists: $vswhereExists)")

    $vsInstances = @()
    if ($vswhereExists) {
        try {
            $rawJson = & $vswhere -all -products '*' -include packages -format json -utf8 | Out-String
            if (-not [string]::IsNullOrWhiteSpace($rawJson)) {
                $vsInstances = $rawJson | ConvertFrom-Json
            }
        } catch {
            $lines.Add("  [!] Error invoking vswhere: $_")
        }
    }

    if ($vsInstances.Count -eq 0) {
        $lines.Add("  No Visual Studio instances detected by vswhere.")
    } else {
        foreach ($inst in $vsInstances) {
            $lines.Add("  Instance ID:          $($inst.instanceId)")
            $lines.Add("  Display Name:         $($inst.displayName)")
            $lines.Add("  Installation Path:    $($inst.installationPath)")
            $lines.Add("  Version:              $($inst.installationVersion)")
            $lines.Add("  IsComplete:           $($inst.isComplete)")

            # Extract Workloads
            $packages = $inst.packages
            $workloads = $packages | Where-Object { $_.type -eq 'Workload' }
            $lines.Add("  Installed Workloads:")
            if ($workloads) {
                foreach ($wl in $workloads) {
                    $lines.Add("    - $($wl.id) (v$($wl.version))")
                }
            } else {
                $lines.Add("    - (None)")
            }

            # Check C++ Desktop workload
            $hasCpp = ($workloads | Where-Object { $_.id -like '*VCTools*' -or $_.id -like '*NativeDesktop*' }) -ne $null
            $lines.Add("  C++ Workload Present: $(if ($hasCpp) { 'Yes (Microsoft.VisualStudio.Workload.VCTools)' } else { 'No' })")

            # Check UWP workload
            $uwpWorkloads = $workloads | Where-Object { $_.id -like '*Universal*' -or $_.id -like '*UWP*' }
            $lines.Add("  UWP Workload Present: $(if ($uwpWorkloads) { 'Yes (' + ($uwpWorkloads.id -join ', ') + ')' } else { 'No' })")

            # Check C++ UWP component
            $uwpVcComp = $packages | Where-Object { $_.id -like '*UWP.VC*' -or $_.id -like '*Universal.VC*' }
            $lines.Add("  C++ UWP Tools Comp:   $(if ($uwpVcComp) { 'Yes (' + ($uwpVcComp.id -join ', ') + ')' } else { 'No' })")

            # Check Windows SDK component in VS
            $sdkComps = $packages | Where-Object { $_.id -like '*Windows*SDK*' }
            $lines.Add("  VS SDK Components:")
            foreach ($sc in $sdkComps) {
                $lines.Add("    - $($sc.id) (v$($sc.version))")
            }
            $lines.Add("")
        }
    }

    # 4. NuGet & C++/WinRT NuGet Package
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add(" 4. NUGET & C++/WINRT NUGET PACKAGE")
    $lines.Add("--------------------------------------------------------------------------------")
    $nugetCmd = Get-Command nuget -ErrorAction SilentlyContinue
    $nugetCmdPath = if ($nugetCmd) { $nugetCmd.Source } else { "Not in PATH" }
    $lines.Add("  nuget.exe in PATH:    $nugetCmdPath")

    $nugetPkgDir = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.windows.cppwinrt"
    $nugetPkgExists = Test-Path -LiteralPath $nugetPkgDir
    $lines.Add("  CppWinRT NuGet Cache: $nugetPkgDir (Exists: $nugetPkgExists)")
    if ($nugetPkgExists) {
        $versions = Get-ChildItem -LiteralPath $nugetPkgDir -Directory | Select-Object -ExpandProperty Name
        $lines.Add("    Versions: $(if ($versions) { $versions -join ', ' } else { 'None' })")
    }
    $lines.Add("")

    # 5. MinGW-w64 / Clang Toolchains
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add(" 5. MINGW-W64 & CLANG / LLVM TOOLCHAINS")
    $lines.Add("--------------------------------------------------------------------------------")
    $gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
    $clangCmd = Get-Command clang -ErrorAction SilentlyContinue
    $clangClCmd = Get-Command clang-cl -ErrorAction SilentlyContinue

    $lines.Add("  gcc in PATH:          $(if ($gccCmd) { $gccCmd.Source } else { 'Not found' })")
    $lines.Add("  clang in PATH:        $(if ($clangCmd) { $clangCmd.Source } else { 'Not found' })")
    $lines.Add("  clang-cl in PATH:     $(if ($clangClCmd) { $clangClCmd.Source } else { 'Not found' })")

    $commonToolPaths = @(
        "C:\msys64",
        "C:\mingw64",
        "C:\Program Files\LLVM",
        "C:\Program Files (x86)\LLVM",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm"
    )
    $lines.Add("  Candidate Toolchain Locations:")
    foreach ($tp in $commonToolPaths) {
        $tpExists = Test-Path -LiteralPath $tp
        $desc = "Exists: $tpExists"
        if ($tpExists -and $tp -like "*BuildTools*") {
            $llvmBins = Get-ChildItem -LiteralPath (Join-Path $tp "bin") -Filter "*.exe" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
            if ($llvmBins) {
                $desc += " (Contains: $($llvmBins -join ', '))"
            }
        }
        $lines.Add("    - $tp -> $desc")
    }
    $lines.Add("")

    # 6. Developer Mode State (Read-only)
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add(" 6. DEVELOPER MODE STATE (READ-ONLY)")
    $lines.Add("--------------------------------------------------------------------------------")
    $devKeyPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock"
    $devKey = Get-Item -LiteralPath $devKeyPath -ErrorAction SilentlyContinue
    $devModeVal = if ($devKey) { $devKey.GetValue("AllowDevelopmentWithoutDevLicense") } else { $null }

    $devPolicyPath = "HKLM:\SOFTWARE\Policies\Microsoft\Windows\AppModelUnlock"
    $devPolicyKey = Get-Item -LiteralPath $devPolicyPath -ErrorAction SilentlyContinue
    $devPolicyVal = if ($devPolicyKey) { $devPolicyKey.GetValue("AllowDevelopmentWithoutDevLicense") } else { $null }

    $isDevMode = ($devModeVal -eq 1 -or $devPolicyVal -eq 1)

    $lines.Add("  Registry Key:         $devKeyPath (Exists: $([bool]$devKey))")
    $lines.Add("  AllowDevelopmentWithoutDevLicense: $(if ($devModeVal -ne $null) { $devModeVal } else { 'Not set (0)' })")
    $lines.Add("  Policy Key:           $devPolicyPath (Exists: $([bool]$devPolicyKey))")
    $lines.Add("  Policy Value:         $(if ($devPolicyVal -ne $null) { $devPolicyVal } else { 'Not set' })")
    $lines.Add("  Developer Mode State: $(if ($isDevMode) { 'ENABLED' } else { 'DISABLED' })")
    $lines.Add("")

    # 7. Sideloading State (Read-only)
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add(" 7. PACKAGE SIDELOADING STATE (READ-ONLY)")
    $lines.Add("--------------------------------------------------------------------------------")
    $sideVal = if ($devKey) { $devKey.GetValue("AllowAllTrustedApps") } else { $null }

    $appxPolicyPath = "HKLM:\SOFTWARE\Policies\Microsoft\Windows\Appx"
    $appxPolicyKey = Get-Item -LiteralPath $appxPolicyPath -ErrorAction SilentlyContinue
    $appxPolicySideVal = if ($appxPolicyKey) { $appxPolicyKey.GetValue("AllowAllTrustedApps") } else { $null }

    # On Windows 10 (2004+) and Windows 11, sideloading is enabled by default unless blocked by policy.
    $sideloadingAllowed = ($sideVal -eq 1 -or ($sideVal -eq $null -and $appxPolicySideVal -ne 0))

    $lines.Add("  AllowAllTrustedApps (Key):    $(if ($sideVal -ne $null) { $sideVal } else { 'Not set (defaults to enabled in Windows 11)' })")
    $lines.Add("  AllowAllTrustedApps (Policy): $(if ($appxPolicySideVal -ne $null) { $appxPolicySideVal } else { 'Not set' })")
    $lines.Add("  Sideloading Allowed:          $(if ($sideloadingAllowed) { 'YES (Trusted signed packages can be sideloaded)' } else { 'NO (Restricted by policy)' })")
    $lines.Add("")

    # 8. Local Test Accounts (Names only)
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add(" 8. LOCAL ACCOUNTS & TEST ACCOUNTS (NAMES ONLY)")
    $lines.Add("--------------------------------------------------------------------------------")
    $localUsers = @()
    try {
        $localUsers = Get-LocalUser | Select-Object -ExpandProperty Name
    } catch {
        $localUsers = (Get-CimInstance Win32_UserAccount -Filter "LocalAccount=True") | Select-Object -ExpandProperty Name
    }

    $testAccounts = $localUsers | Where-Object { $_ -like '*Sandbox*' -or $_ -like '*Test*' }

    $lines.Add("  All Local User Accounts (Names Only):")
    foreach ($u in $localUsers) {
        $marker = if ($testAccounts -contains $u) { " [Designated Test/Sandbox Account]" } else { "" }
        $lines.Add("    - $u$marker")
    }
    $lines.Add("  Identified Test Accounts: $(if ($testAccounts) { $testAccounts -join ', ' } else { 'None' })")
    $lines.Add("")

    # 9. Readiness Assessment for Building Packaged C++/WinRT Fixture
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add(" 9. READINESS ASSESSMENT FOR PACKAGED C++/WINRT FIXTURE")
    $lines.Add("--------------------------------------------------------------------------------")
    $lines.Add("  [EXISTS on this host]:")
    $lines.Add("    - Windows 11 Pro x64 host (OS build 10.0.$currentBuild.$ubr)")
    $lines.Add("    - Visual Studio Build Tools 2022 with MSVC C++ compiler (v143, cl.exe, link.exe, MSBuild)")
    $lines.Add("    - Windows 11 SDK 10.0.26100.0 (Include & Lib headers: ucrt, um, shared, winrt)")
    $lines.Add("    - Windows SDK C++/WinRT headers (Include\10.0.26100.0\cppwinrt\winrt\*.h)")
    $lines.Add("    - Windows SDK cppwinrt.exe compiler (bin\10.0.26100.0\x64\cppwinrt.exe)")
    $lines.Add("    - Windows App Packaging tool (bin\10.0.26100.0\x64\makeappx.exe)")
    $lines.Add("    - Windows App Signing tool (bin\10.0.26100.0\x64\signtool.exe)")
    $lines.Add("    - Sideloading enabled for signed packages")
    $lines.Add("    - Dedicated test accounts available (CodexSandboxOffline, CodexSandboxOnline)")
    $lines.Add("")
    $lines.Add("  [MISSING on this host]:")
    $lines.Add("    - Visual Studio Universal Windows Platform build tools workload (Microsoft.VisualStudio.Workload.UniversalBuildTools)")
    $lines.Add("    - Visual Studio C++ Universal Windows Platform tools component (Microsoft.VisualStudio.ComponentGroup.UWP.VC)")
    $lines.Add("    - UWP MSBuild project templates, AppContainer CRT import libraries, and UWP vclibs targeting props")
    $lines.Add("    - Developer Mode (AllowDevelopmentWithoutDevLicense = 1) is currently disabled")
    $lines.Add("    - nuget.exe CLI and cached Microsoft.Windows.CppWinRT NuGet package")
    $lines.Add("    - MinGW-w64 and Clang compiler toolchains")
    $lines.Add("")
    $lines.Add("  [WHAT THE USER WOULD NEED TO INSTALL / CONFIGURE]:")
    $lines.Add("    1. Visual Studio Installer -> Modify 'Visual Studio Build Tools 2022':")
    $lines.Add("       - Select workload 'Universal Windows Platform build tools' (or add component 'C++ Universal Windows Platform tools' / v143).")
    $lines.Add("       - Note: For U1.0 (packaged full-trust console C++/WinRT probe), the existing SDK cppwinrt.exe, MakeAppx.exe,")
    $lines.Add("         and MSVC v143 are already sufficient to compile native binaries, project headers, and package them;")
    $lines.Add("         the UWP workload is strictly required when compiling standard UWP AppContainer GUI applications (U1.1/U2).")
    $lines.Add("    2. Optional: Enable Developer Mode in Settings ('System > For developers > Developer Mode')")
    $lines.Add("       if registering loose-file packages via Add-AppxPackage -Register without signing.")
    $lines.Add("    3. Optional: Download nuget.exe if NuGet package restore workflows are used instead of SDK cppwinrt.")
    $lines.Add("    *(DO NOT install or change anything during read-only inventory)*")
    $lines.Add("================================================================================")

    return ($lines -join "`r`n")
}

# Run the inventory report
$report = Get-HostInventoryReport
Write-Output $report

# Handle Calculator metadata export (strictly outside WineExplorer repo)
if (-not $SkipCalculatorExport -and -not [string]::IsNullOrWhiteSpace($CalculatorMetadataPath)) {
    # Guard: verify the path is not inside this repository
    $repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
    $resolvedTarget = [System.IO.Path]::GetFullPath($CalculatorMetadataPath)
    $resolvedRepo = [System.IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar

    if ($resolvedTarget.StartsWith($resolvedRepo, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "POLICY VIOLATION: Refusing to write Calculator metadata into the WineExplorer repo: $CalculatorMetadataPath"
    }

    $targetDir = Split-Path -Parent $resolvedTarget
    if (-not (Test-Path -LiteralPath $targetDir)) {
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    }

    $calcPkg = Get-AppxPackage -Name "Microsoft.WindowsCalculator" -ErrorAction SilentlyContinue

    $calcLines = [System.Collections.Generic.List[string]]::new()
    $calcLines.Add("# Windows Calculator Package Metadata (Oracle Host)")
    $calcLines.Add("# Collected: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss K')")
    $calcLines.Add("# Source: Get-AppxPackage -Name Microsoft.WindowsCalculator (Metadata only; no file inspection)")
    $calcLines.Add("# Location: Private oracle directory outside WineExplorer repository")
    $calcLines.Add("")

    if ($calcPkg) {
        $calcLines.Add("Name: $($calcPkg.Name)")
        $calcLines.Add("Publisher: $($calcPkg.Publisher)")
        $calcLines.Add("Architecture: $($calcPkg.Architecture)")
        $calcLines.Add("Version: $($calcPkg.Version)")
        $calcLines.Add("PackageFullName: $($calcPkg.PackageFullName)")
        $calcLines.Add("PackageFamilyName: $($calcPkg.PackageFamilyName)")
        $calcLines.Add("InstallLocation: $($calcPkg.InstallLocation)")
        $calcLines.Add("IsFramework: $($calcPkg.IsFramework)")
        $calcLines.Add("SignatureKind: $($calcPkg.SignatureKind)")
        $calcLines.Add("Status: $($calcPkg.Status)")
        $calcLines.Add("")
        $calcLines.Add("Dependencies:")
        foreach ($dep in $calcPkg.Dependencies) {
            $depArch = if ($dep.Architecture) { $dep.Architecture } else { "neutral" }
            $depVer = if ($dep.Version) { $dep.Version } else { "unspecified" }
            $calcLines.Add("  - Name: $($dep.Name)")
            $calcLines.Add("    PackageFullName: $($dep.PackageFullName)")
            $calcLines.Add("    Architecture: $depArch")
            $calcLines.Add("    Version: $depVer")
        }
    } else {
        $calcLines.Add("Status: Microsoft.WindowsCalculator not found on this host.")
    }

    [System.IO.File]::WriteAllText($resolvedTarget, ($calcLines -join "`r`n"), [System.Text.Encoding]::UTF8)
    Write-Output ""
    Write-Output "[Notice] Windows Calculator metadata successfully written to: $resolvedTarget (outside WineExplorer repo)"
}
