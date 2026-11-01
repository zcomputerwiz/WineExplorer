<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Registration implementation: parent review requires rework

Reviewed published root35c07fc054be63534d7c3355a49f43caf4d7786e and Wine
74cb1f3d8aff9d86811b815f69fd8dbccaca4482. Parent read all production/test diff,
retained x64/i386 logs and private p26.sh. This is not accepted for import or R6.
Worker logs report34 assertions/0 failures/0 skips per architecture; parent has
not independently built or run these production binaries. Root delivery only
changes gitlink, with no committed implementation/acceptance report.

## R1 (P1): locking is absent and errors deliberately fail open

kernelbase/security.c ac_lock_open passes relative WineAppContainerRegistrationLock
to NtCreateMutant with NULL RootDirectory. This is not a valid NT object path.
Parent original native Windows probe with the same relative-name/NULL-root/
OBJ_CASE_INSENSITIVE/SYNCHRONIZE shape returns STATUS_OBJECT_PATH_SYNTAX_BAD
(0xc000003b), NULL handle. Retained privately at
D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/appcontainer-parent-lock-review-20261026/native-relative-lock.json.
Wine server/object.c at this very revision also rejects a relative name with no
root as that status. Even after fixing the path, the flags omit OBJ_OPENIF;
existing-object creation failure is also silently turned into an unlocked path.
ac_lock_take ignores wait status. Atomic NtCreateKey disposition alone cannot
prevent a reader or losing registration from observing a leaf before Moniker
and DisplayName publish. The passing two-writer test checks disposition, not
serialization or partial publication. The comment saying lock failure is safe
and that indefinite waiting cannot wedge is incorrect.

Fix actual named-object resolution/create-or-open, user/HKCU identity and
cross-session scope, access masks, wait/release/error propagation. No unlocked
fallback. Distinguish abandoned acquisition; establish a deterministic policy
for a prior writer dying between key creation and both values. Do not invent
mapping existence from an incomplete leaf. Add regression proving a second
process actually waits for lock/publication, not just exactly-one creation.
Test acquisition failure leaves registry unchanged. Keep the implementation
small and limited to metadata, no token authority redesign.

## R2 (P1): reproducibility and cleanup harness violate isolation

Private p26.sh kill_wine uses broad pkill -f wineserver, install/lib/wine,
wineboot.exe, explorer.exe, services.exe, etc. These can terminate other workers'
processes. Do not execute this script again. Replace with explicit owned build,
prefix, exact launched process handles/process group and prefix-specific
wineserver control; no global name/pattern kills. Include a foreign sentinel
process/prefix regression proving cleanup leaves it alive.

Reported build depended on uncommitted include/Makefile.in/configure changes
and deleted directories. Source-only commit tests are therefore not reproducible
from the delivery. Produce an isolated clean source build (or clearly separate
and commit a minimal reviewed prerequisite rather than quietly using dirt).
Record exact build compiler/configure arguments, loaded x64/i686 module paths
and actual input DLL hashes. Do not rebuild/patch the existing dirty old graphics
build or anyone else's running binaries. All storage D:/D-backed WSL. Report
whether broad cleanup affected another worker if known; do not guess it did.

## R3 (P2): tests miss required contracts and can leave children behind

Tests use fixed S-1-5-21 test SIDs and unconditionally unregister them on entry,
not legitimate fresh package identities corresponding to monikers. Use unique
owned per-run package SIDs/monikers (derive with accepted API) and pass them to
bounded children. Do not execute the current tests on native Windows against
fixed names/SIDs. Test both independent allocations and exact Unicode content.

Persistence/concurrency ignores some WaitFor* results and closes live child
handles on failure. Second-child launch failure can leave child0 blocked forever;
it closes its handles once inside the failure loop and again at cleanup. Other
timeouts terminate without wait/reap and sometimes count as skips. Replace with
single owned-child cleanup paths, bounded waits/status checks, deterministic
barriers, no Sleep-based readiness assertion, and failure counts for required
cases that fail to execute. Demonstrate launch failure and timeout cleanup,
failed publication rollback, abandoned writer, corrupt size/terminator as well
as REG_DWORD, and original SID regression tests on actual x64/i686 binaries.

## R4 (P2): storage errors and malformed values are misrepresented

AppContainerRegisterSid maps STATUS_OBJECT_NAME_NOT_FOUND or
STATUS_OBJECT_PATH_INVALID to ERROR_ALREADY_EXISTS. These are not duplicate
key-disposition observations; propagate the actual failure. Add a regression
showing failed key creation cannot masquerade as existing registration.

ac_read_moniker_value accepts odd-length/unterminated REG_SZ and appends/truncates
NUL without validating stored data; no required corrupted-size/terminator test.
Validate lengths/overflow/terminator and use correct bounded allocation before
returning a mapping. ac_open_hkcu requests write/create even for lookup and
collapses every failure into ACCESS_DENIED: use operation-appropriate rights
and preserve actual error status. Do not claim unmeasured Windows NULL/error
contracts as conformance; distinguish deliberate safe behavior from oracle facts.

## Deliverable / authority

Antigravity: supervise corrections on the SAME isolated root/Wine branches;
coordinate existing implementer, do not create a competing implementation.
Commit AND push production/tests, reproducible owned runner and original root
report, exact counts/no skips and limitations. Preserve original failed/partial
reports and logs. Reuse accepted SID base. No master merge, R6 preparation,
VM/SSH/browser test, security bypass, unrelated token/OOBE import or vendor
publication. Parent independently reviews before any trial. Acknowledge/reply
codex with updated commits and report; do not just relay the old34-test claim.
