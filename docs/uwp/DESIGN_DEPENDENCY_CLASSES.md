<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Activatable classes of a package's dependencies (built, 2026-11-17)

Status: built on `codex/uwp-runtime` (Wine 3a81db3e) and in the `uwp-frameworks` component (owner's go-ahead,
2026-11-17). It changes the package context (`DESIGN_PACKAGE_CONTEXT.md`, server `get_token_package`, combase
`RoGetActivationFactory`); `docs/STOCK_CHANGES.md` lists the files.

What differs from the first draft below: the server reads the dependency records from the machine catalog
(`C:\ProgramData\WineExplorer\appx-catalog`, where the frameworks are published; the prefix's own `appx-catalog` holds
the users' packages), and the table path of a dependency's class is absolute (`C:\Program Files\WindowsApps\<full
name>\<relative>`) rather than "full name plus relative", so combase needs no knowledge of roots: a path with `X:` is
used as it is (the server allows no `:` in a package's own relative paths). What Microsoft documents (checked
2026-11-17, Microsoft Learn: the `ActivatableClass` / `InProcessServer` elements of the package manifest, the Windows
App SDK deployment architecture and the Dynamic Dependencies specification): a framework package declares the classes
it implements in its own manifest, and a package reaches the framework through its package graph (a declared
dependency, or added at run time by the Dynamic Dependencies API); the manifest, not a registry key, is where
the classes of a package are declared. I found no statement that packaged apps depend on machine-wide registry
entries for framework classes, and none on how the resolver orders classes of several packages; the ordering rule
here (own classes first, then dependencies in the order the package lists them) is ours.

Result in the lab: `RoGetActivationFactory(XamlControlsXamlMetaDataProvider)` succeeds once and the 107 to 150 error
lines are gone; the Standard calculator still works (7 + 5 = 12), the U1.0 fixture passes on x64 and x86 (14 context
checks each). Not added yet: fixture cases for a dependency (class served, own class wins, missing dependency skipped,
oversized table); the component test covers the record's class lines.

# The first draft (kept for the reasoning)

## Problem

A process that runs a package gets, from the server's package context, the classes that package declares
(`class` lines of its catalog record) and nothing else. On Windows the classes of the package's dependencies
(framework packages: WinUI 2 `Microsoft.UI.Xaml`, VCLibs, .NET Native) are activatable too. In the Calculator run
`RoGetActivationFactory(Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider)` fails 107 to 150 times per
run (`Failed to find library`, REGDB_E_CLASSNOTREG): the application's generated type-info code asks WinUI 2's provider
for every WinUI type it is unsure of. The framework's manifest declares the class (137 `ActivatableClass` entries in the
installed `Microsoft.UI.Xaml.2.8` manifest); our framework record (`framework 1`, no `class` lines) does not.

## Proposal

1. `uwp-frameworks` component: the record of a framework package gets `class` lines from the package's own
   `AppxManifest.xml` (`windows.activatableClass.inProcessServer`: class id, DLL path relative to the package root).
   Same validation as `wexappx` (`valid_class_id`, relative path rules).
2. Server (`uwp_context.c`): when it builds the class table of a package, it also reads the records named by that
   package's `dependency` lines (already resolved at install: exact full names) and appends their classes after the
   package's own, each path written `<DependencyFullName>\<relative path>`. The package's own classes come first and
   win: a class the package declares is never taken from a dependency. A dependency that is missing or malformed is
   skipped, not an error (the package still starts). The 64 KiB record limit stays per record; the table is capped
   (suggest 256 KiB in total) and a dependency that would exceed it is skipped whole.
3. combase (`get_package_library_for_classid`): a path that starts with a full name is resolved against the
   dependency's root (`C:\Program Files\WindowsApps\<FullName>`; machine frameworks live only there) instead of the
   package root. The rule that a package's class is served by its module only, with no fallback, stays.
4. Tests: `tests/uwp/u1_0/context-negatives.c` gets cases (dependency class served; own class wins over a dependency's;
   missing dependency skipped; path with `..` in a dependency record refused; table cap).

## Why not the registry

HKLM `ActivatableClassId` keys (what `uwp-xaml` does for XamlShim's own classes) would make framework classes global
to every process and user, which is not the Windows model and breaks the rule that only a package's context declares
what it can activate. It also needs one entry per class per architecture.

## Open questions

* Transitive dependencies (a framework that depends on another): Windows resolves them; ours has none today
  (`a framework package with dependencies is not supported` in `wexappx`). Keep it that way until needed.
* x86 packages: dependency full names carry the architecture, so the record already picks the right one.
* What the Calculator gains: first the 107 to 150 error lines go. Whether WinUI 2's native provider then runs on our
  XamlShim (it asks the XAML layer for WinUI types) is not known; measure before claiming more.
