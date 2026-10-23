<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Same-file / private-loopback-UP control: offline checkpoint

**PREPARED_NOT_RUN.** Follows the authoritative refinement at the top of
EDGE_LOOPBACK_CONTROL_PLAN. HTTP is DEFERRED: no HTTP server was written or
deployed. Current selected variable is only private loopback availability, while
the original file-URL construction/page, installed payload/prefix, backend,
startup logging, security and diagnostic non-acceptance remain unchanged.
Preserve both failed file arms and their evidence; no causal claim or fix.

## Small implementation (`2c4d6d22`)

`run-isolated-graphics.py --private-loopback` is opt-in. Default command/isolation
is unchanged. Parent opens its network-namespace descriptor read-only, passes it
through unshare --mount --net, and the child verifies its current namespace differs
BEFORE any modifying command. Require fresh lo DOWN/no addresses/no routes and
no other interfaces. The ONLY modifying network command is `ip link set dev lo up`.
Then require only lo,127.0.0.1/8 (optionally::1/128), loopback-only routes and no
gateway. ip queries/link commands each have1s bounds. No real NIC, route, DNS,
firewall, external listener or guest network setting is changed.

Original-netns descriptor proof survives user drop. Setup before/after namespace,
address/route state is logged on stderr. The runner adds explicit
`--private-loopback-control`, requires --startup-diagnostic and genuine inherited
descriptor proof, and independently observes state before and after the test.
Rejected mode/payload/space/proof guards do not launch Wine or create output.
Browser subprocesses retain ordinary close-fds behavior; WEX proof environment is
removed by existing environment sanitization. No Windows/Linux PID equivalence.

NEW profile/output is required; file scheme and original bytes remain the same.
Diagnostic mode remains passed=false/exit1 even if readiness/fronts complete.
If outcomes differ, only dependency on this tested namespace change is observed:
LO/IPС availability is a possible variable, not evidence of a file-specific,
sandbox, security or rendering cause. Parent review precedes any HTTP arm.

## Actual offline validation

```powershell
wsl -d Debian --exec sh -lc 'cd /mnt/d/DeepThought/Projects/WineExplorer/scripts && python3 -B -m unittest test_graphics_private_loopback test_g4_edge_headful test_g4_edge_install test_g4_virgl_budget test_g4_virgl_launcher test_g4_consumer_harness'
node scripts/test_g4_edge_scene.js
```

**118 tests PASS, no skips** (104 existing +10 loopback +4 runner integration),
plus actual original-fixture Node tests PASS. Covers original-namespace refusal
before modifying commands, wrong interfaces/addresses/routes, missing proof,
dirty initial state, bounded query/setup failure, default wrapper unchanged,
descriptor handoff, diagnostic gating, exact unchanged file URL/argv and lost
final proof failing closed. Expected missing-helper argparse stderr is a passing
negative consumer control. `git diff --check` passed.

Actual bounded WSL check PASSED: default unshare leaves only lo DOWN; opt-in
enables ONLY lo in a distinct namespace, then a <=1s localhost TCP roundtrip
passes after runuser drops to nobody. Descriptor proof survives that real user
drop; original namespace identity stays unchanged. No parent/real-guest lo was
raised. No VM, SSH, Wine/browser or vendor process ran.

Separate existing `test_graphics_x_socket_isolation.py` integration initially
FAILED with a D:/DrvFS socket directory: Xvfb listener bind failures/startup
timeout. Preserved under ignored
`build/g4-virgl-20261022/edge-file-loopback-offline-sockets-20261023/`.
Without source changes, the bounded check PASSED with short Linux-native
socket paths: both displays had their own SO_PEERCRED PID/distinct socket inode,
including first-after-second-start. Logs/results copied to D: under
`build/g4-virgl-20261022/edge-file-loopback-offline-sockets-linux-20261023/`;
original Linux test evidence retained. This host test-path failure is not guest
graphics evidence. Neither failure nor success is hidden or counted as Wine/VM
coverage. Owned test children were bounded/cleaned by the existing test.

## Private preparation / next review

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-file-loopback-r1-20261023.preflight/`
contains exact manifest launch/run/stop commands and PREPARED_NOT_RUN. Archive
SHA256 `8f280725a01c8d30f17e9853e04380b93668a8ddb460a2a3792c36d8f251aeb2`.
All9 members/current source hashes/sizes,5 host helpers, archive bytes and the
accepted installed-evidence reference independently verified; run directory absent.
Expected binaries remain17 (16 stack + installed Edge). Source inventory includes
the updated isolation wrapper; original HTML/JS/native probe are unchanged.

Existing private procedure adds only `edge-file-loopback` command parameters:
wrapper --private-loopback, runner --startup-diagnostic --private-loopback-control,
fresh profile/name and explicit no-HTTP interpretation. Private helper SHA256
`dbe807ee8482ea5a4666ab4995c91c40fb58e5bc319d646a1958cf1a0d74e8cf`.
iproute2 is an explicit required tool, not a downloaded dependency. Existing
>=1GiB pre-transfer gate, all deployed/installed hashes,285s runner/300s SSH,
45s readiness,128MiB trace soft threshold,650/435 raw-UTC guards,900s launcher,
90s shutdown reserve and exact identity/QMP/controller stopper remain required.
Old preparations/evidence untouched. Return for parent review before ONE later
same-file/LO-UP live control; no HTTP, retry, production patch or G4 claim.

## Parent review / bounded dispatch

Parent read opt-in isolation/runner changes and tests; independently passed
all118 Linux tests without skips (including real private loopback and runuser
handoff), original-fixture Node tests, and the X-socket SO_PEERCRED/inode control
with native Linux paths `/var/tmp/wex-lo-parent-20261023-x01`. Verified all nine
archive/current-source sizes/hashes, five helpers, installed-evidence reference,
explicit mode flags and absent output. The source/manifest is accepted for ONE
same-file/loopback-UP diagnostic, only if live usage still permits safe execution
(do not boot at>=90% five-hour used). Capture/keep evidence and cleanly stop;
return before another profile/run or any HTTP arm. No advance causal/acceptance claim.
