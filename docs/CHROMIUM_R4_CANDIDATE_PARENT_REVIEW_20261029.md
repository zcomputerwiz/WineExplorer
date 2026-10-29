<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium r4 candidate: independent parent review — 2026-10-29

Reviewed Wine 4afbb4cd04b90c3061df8c041fdbf05a3034b489 source against 871f6183, root 34fb016d report, and actual r4 results/fixture/QMP records and terminal-front.png. No independent Wine unit tests or browser execution by parent.

## Browser outcome

The retained fixture records HTTP 200 for nonce-matched index.html and scene.js; the actual terminal-front.png shows expected pale green background, blue marker, magenta/cyan canvases, and a title containing the matching nonce, kind=ready and passed=true. This is real observed Chromium fixture progress. The diagnostic's own results.json nevertheless records passed=false and observation_complete=false due to the bound helper's 45-second timeout. Do not describe that as an overall automated pass. Results record QMP acknowledged powerdown, guest shutdown, QEMU exit 0 and no fallback. Worker-reported test counts not independently rerun; TokenCapabilities is explicitly skipped on both architectures.

## Remaining findings

1. **P1: Deleting a missing profile can still remove unrelated empty directories.** dlls/userenv/userenv_main.c:868 maps ERROR_FILE_NOT_FOUND from unregister to S_OK, and :871 then enters SUCCEEDED(hr) cleanup and calls RemoveDirectoryW on the name-derived AC and profile directories. Therefore the previously requested protection for unowned/missing profiles is not present. Preserve existing folders unless successful unregister positively establishes this operation owns the profile; validate native semantics and add a test creating an unregistered profile-named directory with a sentinel and asserting no deletion (include empty AC directory case).
2. **P2: Profile creation treats any ERROR_ALREADY_EXISTS as a valid directory.** userenv_main.c:818,823,828 accept this error without verifying that each existing path is a directory. A regular file at Packages/<name>/AC can cause successful registration and returned S_OK despite no usable AC directory. Validate the path kind and correct error/rollback, preserve pre-existing state, and add an isolated test with a file at the relevant component.
3. **Security semantics not established.** kernelbase/tests/security.c:1092-1095 explicitly skips TokenCapabilities when Wine reports the class unimplemented. The fixture's positive rendering and TokenIsAppContainer query do not demonstrate the required capability membership or effective access enforcement. Existing server code and native tests need a focused comparison. This is a clear limitation, not a claim that the browser's page failed; no false isolation claim or success stub. Avoid a large unrelated matrix.

The previous unterminated REG_SZ bounds check and path separator checks were added; they should remain. TokenUser buffer fix addresses the reported 122 regression for tested sizes but does not establish general token rights/native parity.

## Next work

Same Antigravity/Claude owner, same candidate branches; repair findings 1 and 2 and run focused tests using unique names/private prefixes; check native behavior without deleting unrelated data. For finding 3 use the smallest meaningful Windows-vs-Wine effective access/capability probe; implement a real targeted fix only if needed and supported, otherwise explicitly document the missing guarantee. Preserve the real observed Chromium improvement. Publish exact GitHub branch commits, and after production changes run one fresh normal 8 GiB browser verification with HTTP/nonce/title/front and strict owned shutdown; report diagnostic passed=false separately if helper still times out. No generic harness expansion, security bypass, false success or shared build/evidence writes. Preserve original attempts. This is a focused review/repair, not authorization to rewrite master. After review/acceptance, user wants dedicated Wine integration atop latest fork master.
