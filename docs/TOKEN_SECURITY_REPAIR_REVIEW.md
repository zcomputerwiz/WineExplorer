<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Independent token-security correctness repair

Date: 2026-10-18. Parent and Wine branch: `codex/token-security-correctness`.
Submission baseline: parent `e35732a8` / Wine
`69c1d6b55e00a92c98cb0142837c15ee549f3383`.
The published parent gitlink identifies the repaired Wine commit; resolve it with
`git rev-parse HEAD:wine`. No master merge or blanket T0–T5 acceptance is implied.
Repaired/published Wine: `ae5fec1e11c6ae42e2ba73a6e9b6368d40a34d92`.

## Findings and implementation

1. **Deny-only groups were skipped by deny ACEs.** token_sid_present checked
   ENABLED before DENY_ONLY. Filtering clears ENABLED, so a matching deny could
   disappear and a later user allow grant the right. Deny-only membership now
   participates in deny matching regardless of ENABLED and never grants allow
   rights. Disabled ordinary groups remain ignored. The original PE probe covers
   ordered deny + user allow, deny-only allow, user allow, actual event opens,
   retained handle rights, separate-thread identity and revert. Native Windows
   controls pass on both PE architectures.
2. **Shipping consent trusted a file-based approval fixture.** There were no
   Unix ownership/mode checks, and even such checks through a remappable Wine
   drive path would not establish a trusted host path. Removed the entire fixture
   path, parser and startup shortcut. Production no longer interprets an unattended
   approval file or legacy AUTO_APPROVE/AUTO_DENY variables. Test-only helper
   programs remain separate source probes, not a production consent option.
3. **Password verification was not administrator authorization.** Direct
   unix_chkpwd handling did not establish authorized admin identity or bind that
   identity/request to the resulting Windows token. Removed this path rather than
   accepting a regular account's password as permission to become an administrator.
   Standard-user, unknown-token and policy-required credential requests return
   ERROR_NOT_SUPPORTED without collecting secrets. This is deliberate fail-closed
   behavior, NOT completion of real administrator authentication. The next stage
   implements the request-bound polkit/identity mechanism.
4. **Helper failures were collapsed into cancellation.** Shell launch now preserves
   unsupported/startup/timeout errors and cancellation separately, and returns no
   target handle on rejection. Timeouts terminate/reap only that helper with bounded
   waits. A separate test-only helper verifies cancellation vs error 50, and a
   target that stays alive then exits 37 verifies the actual returned process handle.
5. **Duplicate consent sources broke clean assembly.** The fork also contained
   programs/consent while the parent owned src/programs/consent. Assembly correctly
   refused to overwrite an undeclared nonempty module. Removed the four duplicate
   fork files (recoverable from Git), keeping the owned parent module authoritative.
   No stale vulnerable fork copy is shipped. Resource default AND initial keyboard
   focus are Cancel. Credential policies 1 and 3, invalid values and wrong registry
   types fail closed; they cannot silently select ordinary approval.
6. **Runner false greens and unsafe ownership.** Replaced installed-Wine fallbacks,
   absent-test skips, guessed X display numbers, global socket removal and weak
   pre-allocation dry-run sentinel tests. An explicit combined build is required.
   Missing test executables fail before allocating resources. Full upstream runs
   require a positive executed-test count, zero assertion failures AND zero exit.
   Focused/cleanup-fixture modes are explicitly labeled and are not full acceptance.
   Displays are allocated with -displayfd; child groups and matching build/prefix
   servers are cleaned with bounded waits. SIGTERM/INT/HUP enter cleanup. A live
   external-to-runner Wine client/server heartbeat and display test ownership.

## Reproduction and evidence

Use the actual combined WoW64 build, both MinGW C++ compilers, Python 3 and Xvfb:

```
bash scripts/assemble.sh
mkdir -p build/wow64
cd build/wow64
../../wine/configure --enable-archs=i386,x86_64 --prefix=/usr/local --disable-tests
make -j8
```

From the source root:

```
python3 -B scripts/check-token-security.py --wine-build ABSOLUTE_BUILD \
  --output-dir NEW_DIRECTORY --focused --all
python3 -B scripts/test-token-runner-isolation.py --wine-build ABSOLUTE_BUILD \
  --output-dir ANOTHER_NEW_DIRECTORY
```

The focused runner builds/runs both original PE architectures. Its shipping-
consent test copies the actual built helper and test probe into its fresh C drive,
then redirects ONLY that prefix's Z mapping to an owned fixture root containing
an approval file with mode 0666. It exercises old environment switches, default
Cancel, close/cancel/explicit approval and unsupported credential policy. The
host's real /etc, live prefixes and machine permissions are never changed.
UI click fixtures are not real polkit/administrator authentication evidence.

For upstream suites, configure/build their test executables and omit --focused.
The default runner refuses missing advapi32 security, ntdll info and shell32
shlexec PEs for either architecture instead of reporting an empty successful suite.
--checkpoint-a and --checkpoint-b select the corresponding required sets.
Never describe --disable-tests plus focused probes as the full upstream suite.

On WSLg, run inside `unshare --mount --net --propagation private` and bind an OWNED
mode-1777 directory containing .X11-unix over /tmp inside that namespace only. Do not
remount/change/delete the global WSLg sockets. The separate network namespace
also isolates abstract X sockets; these do not follow mount namespaces. Private
/tmp also isolates X lock files and per-user Wine socket directories. Capture
the host net namespace ID before dropping privileges and export the public
WEX_TOKEN_HOST_NETNS identity if /proc/1/ns/net is then unreadable. Bring up only
the namespace's own loopback. No
network test is claimed by these local probes. The runner rejects a read-only
global socket directory. All prefixes, profiles, groups and Xvfb processes are owned
by the run. Test passwords, Unix accounts or real privileged policy changes are
not needed for this repair's focused evidence.

### Independent results

Machine-readable results and retained probe stdout/stderr are in
`evidence/token-security-correctness-20261018.json`. Source-built focused tests
ran as Debian UID/GID 65534, not root, in private mount/network namespaces with
owned Xvfb displays, homes and fresh prefixes. The native source/build was on
Debian's Linux filesystem, not a reused worker build.
Recorded source hashes identify the exact staged build bytes (including local
line endings); Git may normalize text to LF when publishing. The source content
and Wine revision are pinned independently of that byte-format difference.

| Case | Before | Repaired |
| --- | --- | --- |
| Combined i386/x86_64 assembly/build | Duplicate fork consent prevents assembly | PASS after consolidation; full build then final incremental check exit 0 |
| Deny Everyone + allow user, AccessCheck AND real OpenEvent | Actual old server grants both; focused x64 exit 1 | Both deny with access denied, x64/x86 PASS |
| Deny-only allow, explicit user allow, retained handle, thread isolation/revert | Mixed controls in same reproduction | PASS x64/x86 |
| Same final token-only probe on native Windows | Contract control, not Wine acceptance | PASS x64/x86, both exit 0 |
| Cancellation 1223 vs unsupported 50; actual target handle waits/exits 37 | Fixed helper stack used in consent control | PASS x64/x86 |
| Actual shipping consent, mode-0666 former fixture + old env switches | Old helper exits without prompt; 8 assertion failures, x64 exit 1 | PASS x64/x86; close/no/cancel 1223, explicit Continue 0 |
| Default/focus Cancel; credential policies 1/3; invalid/wrong-type policy | Additional audit cases | PASS x64/x86; unsupported cases 50 |
| Empty required upstream test directory | Extracted old dispatch prints all passed, exit 0 | Actual new preflight exit 2, no resources allocated |
| Live sentinel through success/failure/timeout/SIGTERM and final teardown | Old dry-run did not prove live ownership | PASS four scenarios; same server/display PID/start IDs and continuing Wine heartbeat; final owned cleanup checked |
| Full upstream security/info/shlexec suites | Historical claims unverified | NOT RUN: build configured --disable-tests; default runner refuses this |
| Real polkit auth, installed desktop, browser sandbox | Historical claims unverified | NOT RUN / pending gates below |

Before-token control staged only the exact old server/token.c from Wine
69c1d6b into the owned build. Before-consent control restored repaired token.c
then staged only parent e35732a8's old consent.c. Neither was represented as a
whole unmodified baseline. Before tests stop at the expected x64 failure; no
x86-before pass/fail is claimed. Both original production sources were restored
from the repaired checkout and rebuilt before the final two-architecture run.
Final consent evidence includes numeric policy/exit expectations for all eight
cases. Native Windows runs use the final same PE hashes, token-only mode, and
never run HKLM-writing shell/consent fixtures on the workstation.

Final focused cleanup has bounded -k/-w, no remaining matching servers and removed
owned prefix roots. The live isolation sentinel is terminated only AFTER proving
it survives all four other runners; its own cleanup is checked separately. No
global kill/socket removal, credentials, accounts or shared policy changes were
needed. Build/source/logs and empty private socket scaffolding remain locally for
review; no owned runtime is intentionally left running.

### Additional issue recorded, not hidden

Fresh uninstalled prefix startup logs a background division-by-zero at
0x14000ca88, both before-consent and final x64/x86 probe stderr. Symbol lookup
in that build's explorer.exe points to the balloon/tray path (systray.c around
balloon_timer), but does not by itself prove the cause. The probe processes exit
successfully after their assertions; actual consent stderr is empty. This is
NOT a clean installed-desktop PASS and is explicitly a checkpoint-B desktop
investigation gate. No explorer disable/diagnostic suppression was used to hide it.

## Explicit remaining gates

No complete R1–R8, T3/T4, browser sandbox or native Unix containment claim is made.
Real request-bound polkit authorization, administrator identity selection, standard-
user elevation, machine writes and exact permission rollback, cross-UID handle
semantics, remaining restriction/MIC/LowBox/handle-table/priority/mitigation cases,
stock-policy browser interaction/persistence and installed-image acceptance remain
gated. The inherited Null SID fallback for disjoint restrictions and allocation
failures require native-contract tests; it is not certified by this small repair.
Helper registry validation also remains an explicit B2 gate in BOTH shell32 and
kernelbase: malformed/oversized/inaccessible configuration must not be confused
with intentionally unset compatibility configuration. This repair does not
certify the remaining inherited token implementation or enable its deployment.

Classic/Open Luna, locked/unlocked taskbar, toolbars and expanded/collapsed/absent
tray chevrons on real X11/Wayland desktops remain NOT RUN here. An owned
unprivileged Xvfb prefix/dialog is not installed normal-user desktop or authentication
acceptance. No original Gemini, graphics or OOBE checkout was switched or edited.
Next worker: `TOKEN_SECURITY_CHECKPOINT_B_WORKORDER.md`.

## Failed attempts, workarounds and local state

- Initial unprivileged git status failed with `fatal: cannot chdir to
  'D:/DeepThought/Projects/WineExplorer/.git/modules/wexcomp': Permission denied`.
  Used authorized escalated diagnostics and --ignore-submodules when appropriate.
- The independent Windows Wine clone did not contain the requested tree:
  `fatal: unable to read tree (69c1d6b55e00a92c98cb0142837c15ee549f3383)`.
  Fetched gemini/token-security from the actual Wine fork, then checked out the
  parent's exact pin on our own branch. No unrelated checkout was reset.
- Read-only searches guessed src/wexhost rather than src/dlls/wexhost, and named
  nonexistent config / shell-glob paths. rg reported OS errors 2/123. Used rg --files
  and exact repository paths instead. Some long document reads were truncated;
  relevant sections were read separately rather than treated as complete output.
- The first generated consent-deletion patch listed hunks in descending source
  order and failed `Failed to find expected lines` at draw_user_tile. The atomic
  failure changed no files. Reissued hunks in ascending order successfully.
- First fresh assembly failed `RuntimeError: Nonempty undeclared destination:
  /var/tmp/wex-token-repair-20261018/source/wine/programs/consent`. Removed the
  duplicate fork module via the committed patch. In the build-only fixture, moved
  the pristine original into duplicate-consent-original after exact path and Git
  checks; rerunning assembly linked the owned source correctly. No unknown user
  edits were overwritten. Original assembly failure remains in assemble.log.
- Read-only checks for configure.log and consent.exe before their build stages
  completed reported No such file or directory; these are not test passes/failures.
- The before-token and before-consent runs intentionally exit 1 on the reproduced
  old defects; their cleanup still completed. These are negative controls, not
  repaired-code PASS claims. Initial fixed runs passed, then policy/focus coverage
  and sentinel-final-cleanup checks were added and rerun on the final sources.
- A WSL process check ended `bash: line 1: rg: command not found`. Used grep -E
  on Debian (host rg remains available). A later shell stderr-loop diagnostic
  lost its $f arguments through the PowerShell/WSL command boundary, so read no
  logs; replaced it with find -exec head, whose actual output is retained.
- Optional post-publication Debian `git fetch --no-tags
  https://github.com/zcomputerwiz/WineExplorer.git codex/token-security-correctness`
  did not complete promptly. Its exact owned fetch/remote-https processes were
  inspected and terminated (exit 1, Terminated); the network cause is unconfirmed.
  Windows Git push AND ls-remote verified both public branch SHAs successfully.
  Retried with a bounded local fetch from /mnt/d/DeepThought/Projects/WineExplorer
  into the owned native clone, which also fetched the published Wine pin. Git-show
  of the committed LF shell wrappers passed bash -n; both committed Python scripts
  passed ast.parse. No fetch/check changed another checkout's working files.

Native Debian source/build/evidence root: /var/tmp/wex-token-repair-20261018.
Logs include assemble.log, assemble-retry.log, configure.log and build.log.
The archived duplicate module is retained for the failed-assembly record. No VM
was reset/reinstalled and no shared-machine ACL or authentication policy was
changed. Other workers' builds/processes are not ours to terminate. Final owned
process/prefix/display cleanup is recorded with the results above.
