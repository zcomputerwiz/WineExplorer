<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S4 account authority

Implemented for static review, 2026-11-06; build and runtime acceptance are
unverified. This does not enable standard-user elevation, advertise
`ACCOUNT_MAP_V1`, issue tokens or change `security_unix_uid_to_sid()`.

The authority is `/etc/wineexplorer/accounts.map`, alongside `sysrun.conf`,
owned by root and the configured system group (normally `root:wexsys`), 0640.
All ancestors must be root-owned directories without group/other write and
without symlink components. **Design correction:** the machine root is 2775
and intentionally writable by wexsys. A root-owned file inside it would still
be replaceable by that group. Moving only the authority beside sysrun.conf
preserves the existing machine-root permission model.

The native tool `tools/wex-accounts/wex-accounts.c` compiles the same parser and
NSS provider as `wine/server/accounts.c`. It is installed root:root 0755 as
`/usr/local/libexec/wineexplorer/wex-accounts`, never setuid/setgid. Only root
can execute its maintenance operations successfully. There is no IPC service,
polkit rule, credential handling or privileged execution interface.

## Format, version 1

ASCII, LF-terminated, exact single spaces, canonical unsigned decimal numbers:

```text
WEXACCOUNTS 1 <generation> <next_rid> <machine_a> <machine_b> <machine_c>
<name> <uid> <incarnation_hex> <active|tombstone> 21 <a> <b> <c> <rid>
```

The last five fields are the SID subauthorities after `S-1-5`; for example,
`21 0 0 0 1000` is the existing `S-1-5-21-0-0-0-1000`. New accounts use the
header's three nonzero randomly generated machine components and monotonically
allocated RIDs starting at 1001. The counter may reach 4294967296 to represent
exhaustion; another allocation then fails. Every record, including a tombstone,
permanently reserves its SID and nonzero 128-bit random incarnation. The fixed
legacy SID is reserved separately and never assigned to a recreated account.
Duplicate SIDs are rejected even across tombstones, as are duplicate keys and
active names/UIDs. Machine components must match the header or be the one
permitted legacy all-zero domain. Limits: 1 MiB, 4096 records, 63-byte supported
ASCII names, 32 lowercase hexadecimal incarnation digits, UID 1..4294967294,
nonzero u64 generation. No skipped lines, trailing fields or embedded NULs.

Each successful changed transaction increments the header generation once;
resync with identical accounts leaves the map bytes and generation unchanged.
All records belong to that header generation. Group eligibility is queried
fresh; this map generation is **not** an implemented S1 `policy_gen` or a
membership cache. Later registration/policy work must bind and recheck policy
snapshots too.

## Maintenance and lifecycle

```sh
# Explicitly identify the ONE existing interactive account on first provision:
sudo /usr/local/libexec/wineexplorer/wex-accounts sync --initialize --legacy-user bob
sudo /usr/local/libexec/wineexplorer/wex-accounts sync
sudo /usr/local/libexec/wineexplorer/wex-accounts retire alice
sudo /usr/local/libexec/wineexplorer/wex-accounts rename oldname newname
```

Provisioning builds or installs the helper, then runs sync at the end when the
system role is enabled. `WEX_ACCOUNTS_BIN` selects a trusted prebuilt tool;
packaging provides `wex-accounts.bin`. `WEX_ACCOUNTS_LEGACY_USER` supplies the
explicit initial legacy account. Without it the legacy SID remains unassigned;
provisioning never guesses from root, SUDO_USER, the first passwd entry or UID
1000. Once initialized, a legacy binding cannot be added or reassigned. Existing
ordinary tokens/prefixes keep their current SID regardless. New lookup of a
server's own UID with a different mapped SID denies `LEGACY_CONFLICT`; this
does not claim migration for that prefix. Multiple preexisting interactive
prefixes with the same old local SID require separate migration review.

`--directory PATH` is a root-tool-only provisioning override: the directory
must already exist and pass all ancestor checks, and must contain protected
`sysrun.conf`. The production server always uses `/etc/wineexplorer`. Alternate
config directories in fixtures do not configure a production server. Setup
preserves an existing protected `admin_group=` choice, or accepts the root
provisioner's `WEX_ADMIN_GROUP`; sudo remains the compatibility default.

Sync enumerates local `/etc/passwd` candidates and requires canonical
`getpwnam`/`getpwuid` agreement, including primary GID, and unique names/UIDs.
Root is excluded. Directory-only NSS accounts are outside this local subset.
Records may include service users to reserve their identity, but lookup denies
accounts with empty shell or the standard false/nologin shells. This limited
shell exclusion is not a complete distro service-account eligibility policy;
PAM account checks and production supported-account policy remain later work.
NSS enumeration/round-trip errors fail the transaction without publishing.

Missing/name-changed/UID-changed active records become tombstones before new
records are added. Plain sync treats an unannounced rename conservatively as a
new identity. **Contract correction:** S1 requires a genuine rename to retain
the key, while the proposed plain name/UID comparison cannot distinguish that
from UID reuse. Use the explicit root-only `rename OLD NEW` immediately after
the host rename: it requires OLD absent and NEW holding the same UID, with no
active NEW mapping, and preserves key/SID. It is an authoritative lifecycle
assertion, never an inference from UID equality alone.

Sync cannot detect an account deleted and recreated with the identical name
and UID between observations. Root lifecycle tooling must `retire NAME` before
recreation (or sync while the account is absent), then sync. Retirement does not
re-add a present account in the same transaction. Out-of-band useradd/userdel,
failed notifications and restored old maps are not magically detected. Root
administrators must serialize host lifecycle operations with this maintenance;
there is no cross-tool NSS/account-database transaction in S4. Until that
operational requirement and the later policy/registration gates are validated,
the provider must not be advertised for production credential elevation.

Wexhost's successful AccountsService CreateUser calls sync immediately, before
session/password setup, best effort with failures logged. There is no deletion
method in the current wexhost source, so no deletion call site was fabricated.
AccountsService's authorization does not give the calling Wine process root:
ordinary wexhost will log the tool's root-required refusal, and protected
provisioning/lifecycle maintenance must perform the update. No new root broker
or privilege acquisition was added to disguise that limitation.

The tool holds a nonblocking exclusive `.accounts.lock`, validated on its open
descriptor. It refuses an already-running writer. It writes a random O_EXCL
temporary file in the same checked directory, sets root:system-group 0640,
flushes and fsyncs it, renames it atomically, and fsyncs the directory. The
root-only `.accounts.initialized` guard prevents a missing map from silently
resetting RID/key history on reinstall. Interrupted initialization can leave a
guard without a map and intentionally requires manual, reviewed recovery. A
post-rename directory-fsync failure reports failure even if a new map is visible;
re-read under the lock for recovery. Never remove tombstones or reset the counter.
Backup/rollback of this authority must preserve history; root disk rollback is
outside the lookup's ability to detect.

## Private lookup API

```c
enum wex_account_status lookup_account(const char *name, uid_t uid,
    uint64_t negotiated_caps, struct wex_account *result);
```

A non-NULL name selects name lookup; NULL selects UID lookup. Success returns
canonical name, UID, primary GID, binary 16-byte incarnation, five host-order SID
subauthorities (revision 1, NT authority 5), fresh administrator membership and
map generation. Administrator membership uses the one configured group and
`getgrouplist`, including primary and supplementary groups. A nonadministrator
returns success with administrator=0; that never grants eligibility.

Every failure zeroes the entire result. Status numbers, private to this API:

| Value | `WEX_ACCOUNT_` suffix | Meaning |
| --- | --- | --- |
| 0 | OK | Complete snapshot returned. |
| 1 | UNKNOWN | No mapped name/UID. |
| 2 | TOMBSTONED | Only historical records match. |
| 3 | NSS_MISMATCH | Active mapping no longer agrees with current local/NSS name/UID. |
| 4 | SID_COLLISION | Two records reserve the same SID. |
| 5 | MAP_UNREADABLE | Map I/O/allocation/read failure. |
| 6 | MAP_UNPROTECTED | Unsafe owner, writable mode, ancestor, symlink, type, group or hardlink. |
| 7 | MAP_INVALID | Malformed/oversized map/config, duplicate identity/key or invalid counter. |
| 8 | ADMIN_GROUP_MISSING | Configured administrator group unavailable. |
| 9 | NSS_ERROR | Local enumeration, NSS round trip or group expansion failed/ambiguous. |
| 10 | UNSUPPORTED | Mapped account outside the implemented shell subset. |
| 11 | LEGACY_CONFLICT | Legacy SID ambiguity or incompatible self/ordinary mapping. |
| 12 | CAPABILITY_MISSING | ACCOUNT_MAP_V1 bit 6 absent. |

An inaccessible/missing ancestor fails protection; inaccessible/missing map
contents fail unreadable. SID validation checks the entire map before returning
any account. Files must be regular, root-owned, not group/other writable,
single-linked and opened without following symlinks. The map must have the
configured system GID. Directories are traversed with checked open descriptors
and openat; the server temporarily uses the existing sysrun group helpers only
for this private lookup and restores the group on all exits.

This API is compiled but unused in the production server. Its capability
argument is a readiness gate, **not endpoint authentication**. Only later
authenticated registration may supply it. There is no new Wine request,
export, environment switch, capability advertisement or production test hook.
`WEX_ACCOUNTS_TEST` builds a standalone lab tool with fake passwd/group paths,
a simulated server UID, capability-denial control and lookup command. The
normal tool and server lack these overrides and commands.

## Qwen validation handoff

Run `sudo bash scripts/test_accounts_map.sh` only in the disposable Linux lab.
It builds production and test native tools, uses a root-owned `/run` fixture,
and changes no real accounts or system configuration. It covers first sync,
byte-idempotent resync, added/deleted/recreated accounts, reused UID, explicit
retirement/rename, both membership routes and an alternate configured group,
missing group, SID collision, duplicate NSS UID, malformed/oversized map, capability
denial, legacy self conflict, mode/owner/config/ancestor/symlink/hardlink tamper,
lost-map and lock-contention refusal, unsupported service shell, failure-output
clearing and production CLI hook rejection.

Performed here: source/path/cleanup review, Git whitespace checks and Git Bash
`bash -n` on setup, packaging and the new fixture. No test execution.
**Unverified:** native and Wine builds/linking; all test execution; libc/NSS EOF,
enumeration and allocation-failure behavior; actual primary/supplementary group
expansion (the fake group fixture covers logic only); server sysrun raise/drop
and wexsys read access; old filtered-admin/standard denial controls and both
client architectures; provisioning/package installation/updates and real
CreateUser notifications; lock contention; crashes, disk-full, RNG failure,
fsync/rename failure and initialization recovery; actual root-only permissions
and ACL inheritance; production artifact hook exclusion; out-of-band lifecycle
coordination and rollback recovery. No VM, WSL, build or runtime test was run.
