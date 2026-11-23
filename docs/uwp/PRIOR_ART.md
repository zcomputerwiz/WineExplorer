<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Prior art for the UWP and Windows App SDK work (surveyed 2026-11-22)

A web survey (the Wine GitLab was behind a bot challenge, so merge request state comes from the wine-mirror and wine-staging
GitHub mirrors and from list.winehq.org threads that could be read; a recent upstream merge may be missing from the mirror's
search index). Rule for this track: look here first, port what exists, write only what does not.

| Need | Prior art | Licence / state | Use |
|---|---|---|---|
| `NtCreateWaitCompletionPacket`, `NtAssociateWaitCompletionPacket`, `NtCancelWaitCompletionPacket` (Windows App SDK's UI and messaging libraries; React Native) | Wine merge request 6911 (Zhiyi Zhang, CodeWeavers, 7 patches with tests) | LGPL-2.1+; **not merged upstream** | merged by hand into this fork (branch `claude/wait-completion`) |
| DirectComposition (`IDCompositionDevice/Visual/Surface`...) | wine-staging `patches/dcomp-DCompositionCreateDevice2` (67 patches, Zhang; updated 2026-06 by Leslie-Hughes) | LGPL-2.1+; staging only | **already ported** in this fork's `dlls/dcomp` (`STOCK_CHANGES.md`: Wine-Staging 0063/0064 and more); it is the Win32 DirectComposition API, not `Windows.UI.Composition` |
| Visual hosting of WebView2 | Wine bugs 58921 (needs a Windows 7/8 override for `msedgewebview2.exe`), 59370 (blank at runtime 109) | no fix anywhere; staging dcomp is the nearest | nothing to port |
| `windows.ui`, `coremessaging` (`CreateDispatcherQueueController`), `windows.ui.core.textinput` | upstream Wine (Zhang, Al-Jaf, Leslie-Hughes, Gofman) | merged, small | already in the base; extend |
| Reading `.appx` / `.msix` (OPC) for `PackageManager` | upstream `opcservices` (`IOpcFactory::ReadPackageFromStream`, Zip64 and compression modes; MRs 8837, 8894, Vibhav Pant, goal: `Windows.Management.Deployment.PackageManager`) | merged by 2025-09 | `wexappx` has its own reader; a `PackageManager` should sit on this one |
| `appxdeploymentclient` (`PackageManager` activation factory, `IPackageManager2`) | upstream (Al-Jaf 2023; Bernon 2025) | stub | extend |
| Native XAML activation: `RoGetActivationFactory` never calls `DllGetActivationFactory` of a loaded native DLL | Wine bug 59987 (also 56916: `Microsoft.ui.xaml.dll`; 57668) | open | our package context table does the lookup; keep in mind for combase |
| Store installers: `Windows.Globalization.GeographicRegion` (58907, 58905, 59262), `Package.Current` without identity (52665), `SetRestrictedErrorInfo` (55180), `UISettings` (56983) | Wine bugs | open or fixed upstream | check each against the fork |
| MSIX/AppX app support, wine-uwp, switchyard-wine | no project found (their licences were never established in this repository either) | - | nothing |
| DWM overlay / composition | Wine MR 10180 (Namo Nath, 2026-07, proof of concept) | unreviewed | reference only |

**Nothing found** (so these are written here): WinUI 3 / Windows App SDK on Wine or Proton (no list of missing
`Microsoft.UI.*` APIs; WinUI's source is MIT, `microsoft/microsoft-ui-xaml`); app services, background activation and
`FullTrustProcessLauncher`; `StoreContext`, `AppInstallManager`, `BackgroundDownloader`, `Windows.Security.Cryptography.Core`;
documentation of `Windows.Internal.StateRepository` or `profapi` ordinals (ReactOS has nothing on WinRT; a wine-devel remark says
Wine will not use ReactOS code derived from reverse engineering).
