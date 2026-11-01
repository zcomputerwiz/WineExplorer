<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native exit helper — OFFLINE ONLY, NOT INTEGRATED / NOT RUN IN WINE

Bounded first building block under CHROMIUM_EXIT_PLAN b22878d4 and subsequent
parent narrowing. No runner/procedure/preparation changes. No guest/SSH/boot,
Wine/native-PE/browser execution, debugger, security changes or runtime copying.
Original failed Chromium baseline/cache/preparations remain unchanged.

## Concrete helper

g4_native_exit.cpp + shared g4_native_exit_contract.h implement ONE fixed ordinary
CreateProcessW schema: retained Chromium r1 full Z: image, new fixed diagnostic
profile/log/journal paths, lowercase32hex nonce and exact127.0.0.1 HTTP app URL.
Only the original normal Chromium flags are constructed. Arbitrary images/flags,
path aliases, extra/misordered options, wrong profile/log/nonce/URL are rejected.
Chromium profile/log must not exist; journal CREATE_NEW never truncates evidence.
Mutable quoted command, separate lpApplicationName, flags0, bInheritHandles FALSE,
NULL security/environment/current-directory parameters; no token/job changes.

The returned process handle is the ONLY target: primary-thread handle closes,
zero-time waits bracket PID/full-image/creation-time binding; early signaled/
failed polls or query failure are unresolved failure, never a replacement PID.
Bound event is written AND FlushFileBuffers completes before bounded wait45s.
Signaled wait then queries THAT handle's unsigned32 code and explicit8-digit hex;
actual exit259 is valid after signaled wait, never treated as liveness. Timeout
and API/flush failures have separate raw events. Monotonic tick_ms is native,
not silently mapped to host clock. Helper exit0 means observation success only,
NOT child exit0 or graphics acceptance. This changes browser launch parentage.

Native event manifest field explicitly says manifest_verified_by_native=false:
this small helper does not hash the runtime. Future reviewed integration MUST
independently verify all258 pinned files/inventory before dispatch; this helper
alone is not runtime attestation or ownership proof suitable for live authority.
On timeout/failure it does not kill the child. Future runner must preserve raw
events, separately record Linux helper identity/natural exit/cleanup-start and
reap observer BEFORE owned-prefix teardown. None of that integration is claimed.

Original g4_native_exit_child.cpp supports only nonce + status3221225599
(0xc000007f), sleeps1s then ExitProcess; helper's separate fixed calibration
schema waits at most5s. This is an arbitrary calibration value, not a proposed
Chromium cause. Neither calibration PE nor helper PE executed. No HWND required.

## Actual verification

`python3 -B -m unittest test_g4_native_exit -v` from explicit C: worktree WSL
scripts path:1 test PASS/no skips in0.316s. It compiles and EXECUTES a Linux C
harness against the ACTUAL shared wex_validate/wex_quote/wex_observe/status-hex
functions used by the Win32 helper; not source assertions or a Python model.
Independent raw host harness compile/run also exit0. Tests cover each option/
path/profile/log/events/nonce/URL and argument-count mutation, exact flags,
Windows backslash/quote/trailing-slash/empty-argument and buffer bounds, exact
callback order, every fallible callback, pre/post-binding races/failed polls,
wait/query/flush failure, timeout vs signaled259, full127 vs0xc000007f/FFFFFFFF.
These fake callbacks do NOT establish actual Windows API behavior or Wine proof.

Both PEs cross-compiled with x86_64-w64-mingw32-g++ -std=c++11 -O2 -static
-Wall -Wextra -Werror -municode:exit0, zero warnings. Host cc used -std=c11 -O2
-Wall -Wextra -Werror:exit0. All output/temp D:, compile caps30/30/20s, host
execution5s. Retained commands/source hashes/logs/PE metadata:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-exit-helper-r1-20261023.build/`.
build-results.json SHA256
`8a5ebb7936a1a34c96be8e34f29056b49f0bc55b19093ff9eb3afe443616c7fe`.

| Actual AMD64 PE (data inspection only) | SHA256 |
| --- | --- |
| native-exit.exe | 366c69286a7eb6469e4bfd9921643202b746fc987de46e9f9ded2bf6713b8d60 |
| exit-child.exe | 6087e3ef18afca2381d7d675e3c7b3ef19141be34d5427c8293684fdfab6c5f9 |

Stop for parent source/test review before integration/preparation or calibration.
No unrelated regression reruns: existing runners/fixture/binaries untouched.
Only these five source/test files and this report staged; existing `M wine`,
D: master, other agents/submodules untouched. No live authority implied.

## Independent parent acceptance of this building block

Parent read all five source/test files and independently ran actual shared C
contract test:1 unittest PASS/no skips in0.326s. Independently cross-compiled both
Windows sources using -std=c++17 -O2 -static -municode -Wall -Wextra -Werror,
exit0/no diagnostics; data-only PE parser confirmed AMD64. Parent output on D:
native-exit-parent-review-r1-20261023 is separate and NOT the deployment pin;
timestamps/compiler options mean these bytes need not equal the worker's PE.
Parent PE hashes3415b452.../33ebecac... retained, neither PE executed. The file(1)
utility was unavailable in WSL; PE verification instead used actual existing
g4_chromium_cache.pe. No warning or test failure was waived.

Code correctly preserves fixed normal flags, explicit application/mutable quoted
command, flags0/no inheritance/NULL environment, held-handle identity and
signaled-before-status distinction (including259). Fake callbacks execute the
same decision function as production helper; this is NOT real Win32/Wine proof.
No runner integration accepted yet. Next OFFLINE only calibration runner/prep
under appended CHROMIUM_EXIT_PLAN; no browser/guest/native calibration authority.
