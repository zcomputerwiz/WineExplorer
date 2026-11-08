<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1.0 Windows 11 oracle results

Run 2026-11-07 on Windows 11 Pro 26H2, build 10.0.26300.9818, AMD64; MSVC 19.44.35228 (Build Tools 2022 17.14), Windows
SDK 10.0.26100.0. x64 package only (the x86 build compiles, see BUILD_CHECK.md, but was not packaged or run). Package name
WineExplorer.U1Oracle.Probe 1.0.0.0, publisher CN=WineExplorer U1 Oracle, signed with a throwaway 2-day certificate trusted
by the owner in the machine TrustedPeople store for this run. Each variant was installed, activated through package
activation (launcher.exe), read, and removed again.

All ten checks in expected.txt passed (diff of actual.txt against expected.txt is empty).

Observed HRESULTs (the values the Wine run has to reproduce):

| Case | Observed HRESULT | Meaning |
|---|---|---|
| class absent from the package manifest | 0x80040154 | REGDB_E_CLASSNOTREG |
| manifest declares the class but the DLL is missing from the package | 0x8007007E | HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND) |
| unpackaged direct run of the probe | 0x80040154 | REGDB_E_CLASSNOTREG |

The threading-model mismatch case from the plan is not covered: COM can host an in-process server in another apartment,
so no failure is predicted. Raw outputs: D:/DeepThought/ChromiumLab/uwp-u10-results (outside the repository).
