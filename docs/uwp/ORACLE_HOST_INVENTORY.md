<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Windows Host Oracle Inventory (Track U1)

Status: Complete (Read-Only Inventory).  
Date: 2026-11-07  
Scope: Windows 11 reference comparison environment for WineExplorer Track U1 (`UWP_COMPATIBILITY_PLAN.md` Phase 1, first bullet; `README.md`).

> [!IMPORTANT]
> **Read-Only Inspection Policy**: All data was collected via non-destructive queries. No packages were installed or uninstalled; Developer Mode was not toggled; no registry or policy modifications were made; no files inside installed packages were opened, copied, or extracted; and no installed packages were executed.
>
> **Microsoft Proprietary Material Exclusion**: In compliance with `CLAUDE.md` and `README.md` decision 8, metadata for the installed Windows Calculator package is recorded strictly outside this repository in `../WineExplorer-local/uwp-oracle/calculator-metadata.txt` and is not included in this document or tracked repository files.

---

## 1. Host Operating System & Architecture

| Property | Value | Notes |
|---|---|---|
| **Product Name** | Windows 10 Pro / Windows 11 Pro | Registry `ProductName` reports `Windows 10 Pro`; CIM `OsName` reports `Microsoft Windows 11 Pro` |
| **Display Version** | 26H2 | Windows 11 version 26H2 |
| **Windows Edition ID** | Professional | Client edition |
| **Current Build** | 26300 | Windows 11 Canary/Preview build channel |
| **Update Build Revision (UBR)** | 9818 | Full OS build: `10.0.26300.9818` |
| **OS Architecture** | 64-bit (`x64`) | `Is64BitOperatingSystem = True` |
| **Processor Architecture** | `AMD64` | `PROCESSOR_ARCHITECTURE = AMD64` |

---

## 2. Windows SDK & Packaging / Signing Tooling

Inspected root: `C:\Program Files (x86)\Windows Kits\10`

### 2.1 Installed SDK Versions

* **Include directory versions**:
  * `10.0.26100.0` (Windows 11 SDK, 24H2)
  * Subdirectories: `cppwinrt`, `shared`, `ucrt`, `um`, `winrt`
* **Bin directory versions**:
  * `10.0.14393.0` (Partial tooling)
  * `10.0.15063.0` (Partial tooling)
  * `10.0.16299.0` (Partial tooling)
  * `10.0.17134.0` (Partial tooling)
  * `10.0.26100.0` (Full Windows 11 SDK tooling: `arm64`, `chpe`, `x64`, `x86`)

### 2.2 App Packaging Tool (`MakeAppx.exe`)

* **Status**: **Present** (4 instances found)
* **Paths**:
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\makeappx.exe`
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\makeappx.exe`
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\arm64\makeappx.exe`
  * `C:\Program Files (x86)\Windows Kits\10\App Certification Kit\makeappx.exe`

### 2.3 App Signing Tool (`SignTool.exe`)

* **Status**: **Present** (4 instances found)
* **Paths**:
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe`
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\signtool.exe`
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\arm64\signtool.exe`
  * `C:\Program Files (x86)\Windows Kits\10\App Certification Kit\signtool.exe`

### 2.4 Windows SDK C++/WinRT Headers & Compiler Tool

* **C++/WinRT Headers**: **Present**
  * `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt\winrt\`
  * `C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt\`
* **Windows SDK `cppwinrt.exe` Compiler**: **Present** (4 architectures found)
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\cppwinrt.exe`
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\cppwinrt.exe`
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\arm64\cppwinrt.exe`
  * `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\chpe\cppwinrt.exe`

---

## 3. Visual Studio Installation & Workloads (`vswhere`)

Inspected via `C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe`:

* **Installation Path**: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`
* **Product**: `Microsoft.VisualStudio.Product.BuildTools` (Visual Studio Build Tools 2022)
* **Installation Version**: `17.14.37614.0` (Channel: `VisualStudio.17.Release`, Release: August 2026)
* **State**: Complete (`isComplete = True`, `isLaunchable = True`)

### 3.1 Workloads & Workload Components

| Workload / Component | Status | Details |
|---|---|---|
| **C++ Build Tools Workload** | **Present** | `Microsoft.VisualStudio.Workload.VCTools` (v17.14.36331.10) |
| **MSBuild Tools Workload** | **Present** | `Microsoft.VisualStudio.Workload.MSBuildTools` (v17.14.36015.10) |
| **Native Desktop Core** | **Present** | `Microsoft.VisualStudio.ComponentGroup.NativeDesktop.Core` (v17.14.36510.44) |
| **Windows 11 SDK 26100** | **Present** | `Microsoft.VisualStudio.Component.Windows11SDK.26100` (v17.14.37011.9) |
| **Universal Windows Platform (UWP) Workload** | **MISSING** | Neither `Microsoft.VisualStudio.Workload.Universal` nor `Microsoft.VisualStudio.Workload.UniversalBuildTools` is installed |
| **C++ UWP Tools Component** | **MISSING** | `Microsoft.VisualStudio.ComponentGroup.UWP.VC` is not installed |

---

## 4. NuGet CLI & C++/WinRT NuGet Package

| Item | Status | Details |
|---|---|---|
| **`nuget.exe` in PATH** | **Not Present** | Neither in system PATH nor user local app folders |
| **C++/WinRT NuGet Cache** | **Not Present** | `%USERPROFILE%\.nuget\packages\microsoft.windows.cppwinrt` does not exist |

> [!NOTE]
> Standalone `nuget.exe` is not strictly necessary for building native C++/WinRT fixtures because the Windows 11 SDK `10.0.26100.0` already includes the native `cppwinrt.exe` compiler tool and projection headers in `bin` and `Include`. However, NuGet would be needed if projects use MSBuild NuGet package restore for `Microsoft.Windows.CppWinRT`.

---

## 5. MinGW-w64 & Clang / LLVM Toolchains

| Toolchain | Status | Details |
|---|---|---|
| **`gcc` / `g++` (MinGW-w64)** | **Not Installed** | Not found in PATH; `C:\msys64` and `C:\mingw64` do not exist |
| **Standalone LLVM / Clang** | **Not Installed** | Not found in PATH; `C:\Program Files\LLVM` and `C:\Program Files (x86)\LLVM` do not exist |
| **Visual Studio BuildTools Clang** | **Partial (Linting Only)** | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm` contains `clang-format.exe` and `clang-tidy.exe` only; `clang.exe` and `clang-cl.exe` are not installed |

---

## 6. Developer Mode State (Read-Only)

Queried registry paths:
* `HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock`
* `HKLM:\SOFTWARE\Policies\Microsoft\Windows\AppModelUnlock`

| Value | Query Result | Interpretation |
|---|---|---|
| `AllowDevelopmentWithoutDevLicense` (Key) | *Not set* (null / 0) | Developer Mode is **DISABLED** |
| `AllowDevelopmentWithoutDevLicense` (Policy) | *Key does not exist* | No domain or local policy override |

---

## 7. Package Sideloading State (Read-Only)

Queried registry paths:
* `HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock`
* `HKLM:\SOFTWARE\Policies\Microsoft\Windows\Appx`

| Value | Query Result | Interpretation |
|---|---|---|
| `AllowAllTrustedApps` (Key) | *Not set* | Default Windows 11 behavior applies |
| `AllowAllTrustedApps` (Policy) | *Not set* (only `MdmHosts` configured) | No policy restriction |
| **Effective State** | **ALLOWED** | Windows 11 enables sideloading of trusted signed `.msix`/`.appx` packages by default without requiring Developer Mode |

---

## 8. Local Accounts & Test Accounts (Names Only)

Local accounts retrieved via `Get-LocalUser` (names only):

* `Administrator` (Built-in administrator, disabled)
* `CodexSandboxOffline` (**Designated local test / sandbox account**, enabled)
* `CodexSandboxOnline` (**Designated local test / sandbox account**, enabled)
* `DefaultAccount` (Built-in system managed, disabled)
* `Guest` (Built-in guest, disabled)
* `Owner` (Interactive user account, enabled)
* `WDAGUtilityAccount` (Windows Defender Application Guard utility, disabled)

---

## 9. Gap Analysis & Fixture Build Readiness

### 9.1 What Exists

1. **Host Environment**: Windows 11 Pro x64 (Build 26300.9818).
2. **Native C++ Compilers & Linkers**: MSVC v143 toolset (`cl.exe`, `link.exe`, `MSBuild.exe`) via Visual Studio Build Tools 2022.
3. **Windows 11 SDK (10.0.26100.0)**:
   * System headers (`ucrt`, `um`, `shared`, `winrt`).
   * WinRT projection headers (`Include\10.0.26100.0\cppwinrt\winrt\*.h`).
   * WinRT projection tool (`bin\10.0.26100.0\x64\cppwinrt.exe`).
4. **Packaging & Signing Tools**:
   * `MakeAppx.exe` (`bin\10.0.26100.0\x64\makeappx.exe`).
   * `SignTool.exe` (`bin\10.0.26100.0\x64\signtool.exe`).
5. **Deployment Capability**: Sideloading is allowed for signed packages; test accounts (`CodexSandboxOffline`, `CodexSandboxOnline`) exist for clean-environment validation.

### 9.2 What Is Missing

1. **Visual Studio C++ UWP Workload**:
   * Workload `Microsoft.VisualStudio.Workload.UniversalBuildTools` is missing.
   * Component `Microsoft.VisualStudio.ComponentGroup.UWP.VC` (C++ Universal Windows Platform tools) is missing.
   * UWP MSBuild targets (`Microsoft.Cpp.AppContainerApplication.props`/`targets`), AppContainer CRT import libraries (`vcruntime140_app.lib`), and UWP VCLibs targeting files are not installed.
2. **Developer Mode**: Disabled (`AllowDevelopmentWithoutDevLicense = 0`). Required for loose-file package registration (`Add-AppxPackage -Register`) or unsigned development deployments.
3. **NuGet / Package Management**: `nuget.exe` and cached `Microsoft.Windows.CppWinRT` NuGet package are missing.
4. **Alternative Toolchains**: MinGW-w64 and Clang compiler frontends are missing.

### 9.3 What the User Would Need to Install / Configure

> [!CAUTION]
> Do NOT install or modify these settings during inventory. These requirements are documented for future fixture development.

1. **To build standard UWP AppContainer GUI fixtures (U1.1 / U2)**:
   * Open the Visual Studio Installer and modify **Visual Studio Build Tools 2022**:
     * Add the **Universal Windows Platform build tools** workload (`Microsoft.VisualStudio.Workload.UniversalBuildTools`).
     * Ensure the **C++ Universal Windows Platform tools** optional component (`Microsoft.VisualStudio.ComponentGroup.UWP.VC` or `v143`) is checked.
2. **For U1.0 (Packaged Full-Trust Native C++/WinRT Console Fixture)**:
   * The host **already has** the necessary tools: SDK `cppwinrt.exe` can generate headers from WinMD metadata; MSVC v143 (`cl.exe` / `link.exe`) can compile full-trust native executables and DLLs; `MakeAppx.exe` can package them with an `AppxManifest.xml`; and `SignTool.exe` can sign the package with a test certificate.
   * Therefore, U1.0 development can proceed immediately without installing the full UWP workload.
3. **If Loose-File Package Registration Is Desired**:
   * Enable Developer Mode via Windows Settings (`Settings > System > For developers > Developer Mode`) or by setting registry DWORD `HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock\AllowDevelopmentWithoutDevLicense` to `1`.
   * Alternatively, generate and install a self-signed test certificate into the local machine's `Trusted People` store and deploy packages using `Add-AppxPackage` (sideloading is already permitted).
4. **If NuGet-based MSBuild Workflows Are Used**:
   * Download the standalone `nuget.exe` CLI and add it to PATH.

---

## 10. Automated Inventory Script Reference

The read-only inspection script used to verify this environment is maintained in the repository at:
* [`tests/uwp/inventory/host-inventory.ps1`](file:///D:/DeepThought/Projects/WineExplorer/tests/uwp/inventory/host-inventory.ps1)

To run the read-only host inventory and regenerate the stdout report:
```powershell
powershell.exe -ExecutionPolicy Bypass -File .\tests\uwp\inventory\host-inventory.ps1
```
