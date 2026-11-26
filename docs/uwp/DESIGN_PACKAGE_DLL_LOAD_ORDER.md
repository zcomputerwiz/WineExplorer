# Design: a package's own DLL beats a Wine builtin of the same name

Status: implemented on Wine branch `claude/package-dll-load-order` (signed off by Session 0 with strict package-name matching, loader.c test on the host too).

## Problem

Notepad ships `riched20.dll` (Office RichEdit 16.0, registers `RichEditD2DPT`) and `msptls.dll` in its package folder.
On Windows the process loads the package copy: `riched20` is not a KnownDLL and the application directory comes first.
Wine loads its own builtin riched20, which has no `RichEditD2DPT`, so Notepad fails to create its edit control.

Cause (measured by reading the loader, not guessed): `dlls/ntdll/unix/loadorder.c` `get_load_order()` falls through
to `version_heuristics()` for a native file outside the system directory. That function maps a version resource
with company "Microsoft" to `LO_DEFAULT` (builtin first), so a Microsoft file loses to a Wine builtin of the same name.
That default is right for a Windows installer or a copied system DLL; it is wrong for a DLL that a package ships for its own use.
Not a KnownDLL problem: `open_known_dll` is only reached for names listed in the KnownDLLs registry key.

## Rule

In `get_load_order()`, in the `!is_system_dir` branch, after the explicit and per-app settings and the program-as-module rule, and
before `version_heuristics()`:

> If the file's NT path is inside a package installation directory, the load order is `LO_NATIVE_BUILTIN`.

A package installation directory is a path component sequence `...\WindowsApps\<name>_<version>_<arch>_<resid>_<publisherid>\...`
(the form `<name>_<version>_<arch>__<publisherid>`, as the package manager lays it out in the prefix). The check is on the path of the
file being loaded only; it needs no process state and no token. A DLL of the same name outside a package folder is unaffected.

Order of precedence stays: `WINEDLLOVERRIDES`, `AppDefaults` and `DllOverrides` registry keys first (a user can still force `riched20=b`),
then this rule, then the existing heuristics. Builtin fallback stays (`n,b`): if the package file cannot load, Wine's builtin is used.

## Why path based and not manifest or token based

* The package manager already installs under one well-known root, so the path is a reliable marker and the rule is three lines.
* An activation context (`<file name=.../>`) only redirects the search; it would not change the Microsoft-vendor builtin preference.
* A process token or package identity check would miss a package DLL loaded by an unpackaged helper (a test tool, `wexhost`) and would
  couple ntdll's unix side to session state.

## Blast radius

Only DLLs in package folders whose name equals a Wine builtin. Known cases: `riched20`, and in other packages the VC runtime
(`vcruntime140`, `msvcp140`) and `d3dcompiler_47`. For the VC runtime the package copy is what the app was built against, and it runs
on Wine; the registry override remains the escape hatch. No change for files in `system32`/`syswow64`, the prefix `drive_c`, installers or the Wine tree.

## Stock change

One condition and one small helper in `dlls/ntdll/unix/loadorder.c`. Entry in `docs/STOCK_CHANGES.md`.

## Tests

1. `dlls/ntdll/tests/loader.c`: create `<tmp>\WindowsApps\Test.Pkg_1.0.0.0_x64__8wekyb3d8bbwe\` (and the x86 form), copy a native test DLL
   named like a builtin (`version.dll` stand-in with a marker export) into it, `LoadLibraryW` by full path and by bare name with
   the folder as the DLL directory: expect the native module (marker export present). Same DLL in a plain temp folder: expect today's behaviour.
2. Registry override wins: `DllOverrides` `version=b` with the package copy present: expect the builtin.
3. **32-bit regression**: the test is built for i386 and x86_64 (WoW64 build, per the repo rule) and run for both; plus one 32-bit program
   (the existing 32-bit sample in the suite) is run in a package-like folder next to a native DLL with a builtin's name.
4. Lab: Notepad without the `riched20=native` override creates `RichEditD2DPT`; the comparison probe `richedit-probe` passes with no override.

## Not part of this change

A product launcher that sets the package DLL directory and AppContainer state; font packages (Carlito, Caladea) for Calibri/Cambria.
