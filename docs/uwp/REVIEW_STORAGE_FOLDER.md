<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R4 review: native ABI corrections required

Codex reviewed the draft while Qwen job
`b5ace3be-2cc2-4252-98a4-d44e339b5b35` was still running. Do not interpret
this file as permission to cancel that job or edit its active files in parallel.
Apply these corrections in its explicit continuation after delivery.

The draft raw native probe uses incorrect IIDs and vtable slots. Its retained
`observation_report.txt` claim that host WinRT is broken is unsupported.
Missing `System32/Windows.winmd` and a PowerShell projection lookup do not
establish native activation failure. Do not repair or configure the host.

Verified against installed public SDK 10.0.26100.0 declarations:

* `winrt/activation.h:95`: IActivationFactory is
  `00000035-0000-0000-c000-000000000046`; draft requests 00000032.
* `winrt/asyncinfo.h:114`: IAsyncInfo is
  `00000036-0000-0000-c000-000000000046`; draft differs.
* `winrt/windows.storage.h:5410`: IStorageFolderStatics is
  `08f327ff-85d5-48b9-aee9-28511e339f9f`; both draft probe and new Wine IDL
  incorrectly use `9316e21b-91b3-48f6-b8a5-6f75824d9b0e`.
* Existing Wine IStorageFolder IDL has
  `72d1cb78-b3ef-4f75-a80b-6fd9dae2944b`; draft probe's Data4 differs.
* Audit IInspectable and every other manually entered IID against public
  declarations. Use SDK-declared typed ABI or C++/WinRT for the native probe,
  rather than hand-written slots/GUIDs. An IAsyncOperation derives from
  IInspectable and exposes put_Completed/get_Completed/GetResults at slots
  6/7/8; IAsyncInfo is queried separately. Draft uses 11/13.

ABI facts may be implemented independently under Wine conventions; no copied
SDK source or proprietary implementation enters the repository. The native
observation probe can compile against the installed SDK.

Required before acceptance: corrected x64/x86 native probes and fresh bounded
observations for directory/missing/file/empty-path, truthful callback/error/
metadata/lifetime results, accurate IID/vtable declarations and generated
typed async IID, then Codex-owned both-ABI Wine build/runtime verification.
Keep old flawed outputs as historical diagnostics; do not label guessed
Win32 error mappings as native observations. Generated exe/obj/output files
must live in ignored fresh output children, with source/header licensing.

Coordinator diagnostic failure retained: invoking cmd.exe through the Node
subprocess wrapper with an embedded quoted vcvarsall command returned
`The syntax of the command is incorrect.` before compilation. No host change
was attempted. A second inline cmd-shell attempt treated escaped quotes as
literal characters and returned "is not recognized" before compilation.
Writing the exact command to an owned scratch `.cmd` and invoking that file
worked: SDK-declared IActivationFactory returned S_OK for StorageFolder, Uri
and InMemoryRandomAccessStream on this host. This directly disproves the
draft host-activation conclusion. Diagnostic source was Qwen's own
`u1r4_min.cpp`; outputs are retained under
`unsloth-tmp/uwp-u1-20261111/storage-sdk-min.*`. No host repair is needed.
