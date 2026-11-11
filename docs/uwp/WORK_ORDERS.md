<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# UWP track U1: work orders (Qwen / Antigravity)

Current owner instruction (2026-11-11): Codex plans, reviews and handles lab
setup; Qwen and Antigravity implement until feature completion. This supersedes
the historical Claude-only review assignment below. Current packages and
ownership are in `EXECUTION_PLAN.md`; implementers leave commits to Codex review.

2026-11-11 scheduling update: new U1-M1 (Qwen runner repair) and U1-L1
(Antigravity isolated lab preparation) are specified in `EXECUTION_PLAN.md`.
The no-VM rule below belongs to the historical WO-U1; L1 expressly permits
only a separately owned guest after its safety preflight. No shared elevation
VM/build fixture is authorized. Review ownership is updated above.

Context: `docs/uwp/README.md` (decision, backlog), `tests/uwp/u1_0/` (the U1.0 fixture and its Windows 11 oracle
results in `ORACLE_RESULTS.md`). Claude reviews every result before it is committed. Codex is not used for reviews
for now.

## Rules for every order

* Read `docs/TOOLING.md`, `CLAUDE.md` and `docs/uwp/README.md` first. Touch only the files the order names.
* No Wine runtime changes in these orders (README decision 1: U1 Wine changes wait for the elevation work, which is
  changing token and server code now). No commits, no pushes, no VM.
* The wexbuild Wine build tree and its system-role test fixture are in use by the elevation work: never run
  `unsloth-tmp/s9-sync.sh`, `make` in `/root/WE-prefetch`, `test_sysrun_server.sh`, `clean-wexbuild-fixture.sh` or any
  Wine program in wexbuild for these orders unless the order says so. Cross-compiling with the MinGW compilers in
  wexbuild is fine.
* Never pass C escapes through shell heredocs, `sed` or `python -c`; use the edit tool. No python on the Windows host.
* No proprietary Microsoft material: the fixture is self-owned; nothing from an installed Windows package.
* Report: files changed, each change in one line, the commands run and their last lines, anything not done.

## WO-U1 U1.0 probe builds with MinGW; Wine run script (baseline, no Wine changes)

Files: `tests/uwp/u1_0/probe.cpp`, `tests/uwp/u1_0/component.cpp` (only if needed to build), new
`tests/uwp/u1_0/build-mingw.sh`, new `tests/uwp/u1_0/run-wine.sh`, `tests/uwp/u1_0/README.md` (a short section).

1. Make `probe.cpp` (and the component DLL if the probe needs it for the negative cases) build with
   `x86_64-w64-mingw32-g++` and `i686-w64-mingw32-g++` (`-O2 -Wall -municode`, static libstdc++/libgcc so no MinGW
   runtime DLL is needed), without changing its behaviour, output lines or the MSVC build (`build.ps1` keeps working;
   use `#ifdef` only where the toolchains genuinely differ, for example a missing declaration). `build-mingw.sh` builds
   both ABIs into `tests/uwp/u1_0/build/mingw-x64/` and `build/mingw-x86/` (gitignored like `build/`).
2. `run-wine.sh <wine build dir> <prefix>`: runs only the cases that need no package installation on Wine (the
   unpackaged direct run, the identity queries of an unpackaged process, missing class) with that build's `wine`
   loader, in the given prefix, with `HKCU\Software\WineExplorer\Shell\UwpCompatibility` set to 1 in that prefix, and
   writes `actual-wine-<arch>.txt`; then prints a comparison with the Windows oracle lines for the same cases
   (`expected.txt`, `ORACLE_RESULTS.md` HRESULTs) and marks every package-activation case `NOT-RUN (no package
   activation in Wine yet)`. It must refuse to run if the prefix is the wexbuild test fixture or `/etc/wineexplorer`
   is involved, and it never starts or kills a wineserver it did not start.
3. Do not run `run-wine.sh` now (the wexbuild build tree is busy); check it with `bash -n` and say so.

Check: both MinGW builds succeed (report the commands and sizes); `bash -n` on both scripts passes; `git diff` shows no
change to the MSVC build files except what the order allows.

Result (2026-11-10, Claude ran it on the wexbuild main build, private prefix `/root/uwp-u1-prefix`): x64 and x86
unpackaged `PASS`, HRESULT `0x80040154`, both MATCH the Windows 11 oracle (`actual-wine-x64.txt`,
`actual-wine-x86.txt`); the package-activation cases are NOT-RUN. Three `run-wine.sh` fixes were needed: the probe gets
the `Z:` form of its temp output path (`_wfreopen` cannot open a Unix path); a new prefix is created with
`WINEDLLOVERRIDES=mscoree,mshtml=` (the Mono prompt blocked wineboot without a display and left `syswow64` empty); the
probe's CRLF output lines are stripped before matching. Wine today: `GetCurrentPackageFullName` is a stub returning
`APPMODEL_ERROR_NO_PACKAGE`, `RoGetActivationFactory` finds no library for the class.
