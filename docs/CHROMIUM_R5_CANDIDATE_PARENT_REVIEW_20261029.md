<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium r5 profile candidate parent review — 2026-10-29

Parent reviewed root b6053250/Wine f01bba45 diffs and test source, actual r5 HTTP and QMP results and terminal-front.png. No independent unit runs. HTTP index/scene 200, nonce-matched title ready/passed=true and visible expected blue/magenta/cyan were observed; diagnostic reports passed=false and observation_complete=false after helper timeout. QMP recorded guest shutdown/QEMU0/no fallback. This is meaningful fixture success, not a claim of complete automated or security acceptance.

The two requested narrow code fixes are present: DeleteAppContainerProfile only removes paths after S_OK unregistration, and ensure_directory checks existing object type. They are source-reviewed scoped fixes; worker unit counts have not been independently rerun. The new tests characterize native Windows differences with broken(), so do not call this behavior established Windows parity.

## Finding P1 — destructive fixed-name tests

dlls/userenv/tests/userenv.c:746-800 uses fixed WineExplorerUnregTest_12345 and WineExplorerFileCollTest_12345 names. It calls DeleteAppContainerProfile before ownership is established and opens sentinel/AC via CREATE_ALWAYS; if an existing user profile or file uses these names, the tests can delete profile data or truncate a file before cleanup. This also violates the work order's unique-name requirement. Use uniquely generated names, ensure nonexistence and fail closed on collisions, create with CREATE_NEW and track ownership of every path/registration; cleanup only positively owned artifacts, on every exit. Do not run current tests in an existing user profile.

## Security limitation

Wine's NtQueryInformationToken(TokenCapabilities) remains unimplemented and the worker explicitly reports a skipped assertion. server/token.c treats app_container SID and capabilities as ordinary token SID matches in token_sid_present; the restricted-SID access intersection is conditional on restricted_sids, not evidence of a Windows-equivalent lowbox access boundary. The reported native lowbox DACL filtering gap is plausible from source and must not be called independently proved or a valid security boundary without focused real Windows-vs-Wine effective-access checks. It is material for running untrusted web content; do not claim normal Chromium sandbox protection based on command-line flags. The accepted scope is fixture functionality only.

## Next action

Coordinate Antigravity/Claude on owned existing branches to repair the destructive tests and run focused x64/i686 tests in fresh isolated prefixes. This is test-only; no repeat browser needed unless production code changes. Deliver a bounded real native-vs-Wine effective-access comparison for an AppContainer token versus ordinary-user-only DACL, and an AppContainer/capability-grant DACL with relevant SID. If supported by native evidence implement a real targeted fix, with tests; do not fake success or label metadata as isolation. If it requires substantial separate work, report exact evidence and propose scoped follow-up rather than claiming security parity. Publish both real GitHub branch tips and preserve current working candidate/evidence.

User has authorized a later dedicated integration branch based on latest zcomputerwiz/wine master after candidate review. Leave master untouched, avoid rewriting active branches; no shared build writes, no bypass, no evidence deletion. Check usage and clean up owned processes.
