<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S12 prefix profile storage

S13 follow-up: the implementation/header now live in
`wine/server/wex_profile_storage.c/.h`; `tools/wex-profile/profile.c/.h` are thin
includes so the original native CLI/tests and server use the same validator.
The survey below records the incoming S12 checkout. S13 adds a separate
no-create reopen API and Windows descriptor enforcement; see
[E1_S13_HKU_LOAD.md](E1_S13_HKU_LOAD.md). S12 prepare still reports ACL intent
pending and never creates a lease or claims load readiness.

Disabled implementation work, 2026-11-07. No build or runtime acceptance is
claimed. Read S1, S4, S10, section 5 of E1-S, E1_DECISIONS.md and TOOLING.md.
Bob remains the Unix identity; Alice is the Windows identity. Same-UID native
Bob can read or alter Bob-owned storage. This is not host isolation.

## Survey completed before implementation

References describe the inspected checkout, including existing uncommitted work.

| Source | Existing behavior and reuse decision |
| --- | --- |
| `wine/server/registry.c:969-996` | `key_in_user_tree()` recognizes the fixed `local_user_sid` and its classes tree under enforced system role. It is not an account-profile manager. |
| `wine/server/registry.c:2676-2693` | Startup loads machine/shared `userdef.reg` into `.Default`, and prefix `user.reg` into the local user's HKCU. Neither is an Alice seed. S12 must not change these paths or live keys. |
| `wine/server/registry.c:1087`, `:2730-2744` | Existing default descriptors name the local SID; service HKU trees are initialized separately. These defaults cannot supply Alice's descriptors. S13 owns explicit per-account registry/file security. |
| `wine/server/registry.c:2509`, `:2936`, `:3192` | Init-load, save-branch and request machinery exists. It does not provide S1 multi-account references or checked persistence. S13 loads; S14 registers saving, locks and leases. |
| `wine/dlls/userenv/userenv_main.c:236-393` | `CreateEnvironmentBlock()` reads ProfileList and token HKU, but constructs profile paths using account names. `GetUserProfileDirectoryW()` at `:470-515` likewise appends `LookupAccountSidW()`'s name to ProfilesDirectory. S15 must consult the validated prefix mapping. |
| `wine/dlls/userenv/userenv_main.c:668-697` | LoadUserProfile A/W return cached HKEY_CURRENT_USER; UnloadUserProfile returns FALSE. They neither provision nor load an Alice hive. |
| `wine/dlls/shell32/shellpath.c:2127-2195`, `:2395-2462` | Shell-folder lookup reads the selected HKU SID for an explicit token, but can fall back to HKLM/default paths. Missing managed state must eventually deny rather than use that fallback. |
| `wine/dlls/shell32/shellpath.c:2497-2549`, `:2564-2678` | ProfileList defaults to `C:\users`; `%USERPROFILE%` expansion appends GetUserNameW. It can write shared HKLM defaults. Do not invoke this as an offline provisioner. |
| `wine/dlls/shell32/shellpath.c:3098-3249`, `:3284-3327`, `:4733-4755` | `_SHRegisterFolders` uses CSIDL defaults to create profile folders and HKCU shell-folder entries. Extra folders include private Local AppData Temp, Themes, AccountPictures and Screenshots. Reuse these audited folder defaults as bounded data, without calling whole-shell registration as SYSTEM/Bob. |
| `src/dlls/wexhost/unixlib.c:813-849` | CreateUser delegates to AccountsService; its documented `/etc/skel` copy creates the home/template prefix. It does not create an over-the-shoulder profile in another user's prefix. S4 deliberately removed the ineffective unprivileged map-sync call. |
| `src/programs/wexelevate/main.c:1004-1020`, `:1083-1097` | The existing filtered-admin path creates TEMP/TMP directory chains best effort and ignores creation errors. It has no per-account hive/mapping authority and is not reusable as transactional provisioning. A src-wide search found no ProfileList/local_user_sid/profile-load implementation replacing the stock paths above. |
| `scripts/setup-machine-root.sh:193-238`, `:416-450` | Bootstraps/promotes a machine prefix; creates `/etc/skel/.wine`, shared machine links and its offline `user.reg` template; rebases root to wexsetup and removes old profile trees. Shared `system.reg` and `userdef.reg` links are intentional. Never run this against Bob's live prefix for S12. |
| `scripts/personalize-prefix.sh:18-29` | Existing new-user personalization rewrites template profile references in each non-symlink user/system/default hive. It is not descriptor anchored and is too broad for S12. Reuse only the offline template's audited, account-independent defaults, not this mutation loop. |
| `scripts/wine-explorer-session-common.sh:21-37` | First logon copies the skel prefix, personalizes it and invokes per-user shell32 registration. This is ordinary Bob behavior and remains unchanged. |
| `wine/loader/wine.inf.in:85-99`, `:287-288` | Fresh-user defaults include Explorer HideFileExt=1 and empty per-user registry scaffolding. S12 takes a narrow projection from the protected offline template, with this source default when the value is absent; it never imports arbitrary template startup commands or cached paths. |
| `wine/server/accounts.c:113-185`, `wine/server/accounts.h:43-64` | S4 provides canonical name, binary incarnation, SID and generation. Its checked openat/O_NOFOLLOW descriptor traversal and ownership/type/link/ACL checks are the model. Account data must come from that authority, not prefix metadata. |
| `wine/server/fd.c:2369-2455`, `wine/dlls/ntdll/unix/file.c:1387-1392`, `:3773-3890` | Wine stores Windows reparse data in `user.WINEREPARSE`; a plain Unix directory can redirect a Windows open. S12 rejects this xattr as well as Unix symlinks and differing mount IDs. |

## Storage and package boundary

The implementation belongs in a root-owned native tool under `tools/`, with a
small descriptor-based internal API for later server integration. No Wine
source, protocol, startup, installer or ordinary profile path needs changing.
The default build denies storage operations. Only an explicitly compiled lab
build can exercise them; no environment variable enables the normal build.

Layout refinement from section 5's preliminary split-tree proposal: keep the
profile and `user.reg` together under
`drive_c/users/.e1s/<SID>/<incarnation>-<readable-name>/`. A single directory
rename can publish the whole bundle atomically, unlike two renames across the
proposed profile and hive trees. The hive is a Wine text hive, not a native
NTUSER.DAT. No paths are published into machine ProfileList. The SID and
incarnation select identity; the initial readable name is a label. An
authoritative S4 rename retaining the same SID/incarnation reuses that label;
a new incarnation gets a new directory, including same-name/UID recreation.

The seed is a bounded projection of protected offline `/etc/skel/.wine/user.reg`
and the audited shell32 defaults above. Only the recognized boolean
HideFileExt DWORD is imported. Absent value uses wine.inf's 1; malformed or
duplicate selected values deny. Arbitrary template keys, commands, user paths,
shell links, hives and host-home symlinks are never copied. Folder and HKCU
shell-folder defaults are generated directly for the validated new path.
This deliberately does not clone every product preference from the setup
account. Broader default imports require a separate audit.

S13 TODO: consume the returned opened profile/hive descriptors and ACL intent;
apply and query back Alice owner, protected Alice/SYSTEM-only DACLs, inherited
file/directory ACEs and High integrity (no Bob/Users/Everyone grant) on the
profile, hive and every existing skeleton child **before any managed use**.
Registry roots/children need explicit Alice descriptors rather than local-SID
defaults. Storage completion means ACL intent pending, never ACL enforcement.
S13 must deny if application, enforcement or persistence cannot be verified.
S14 owns leases, HKU lifetime, storage-in-use locks and durable hive saving.
S15 owns consumer/environment mapping; S18 remains disabled until these gates.

## API and validation

`tools/wex-profile/profile.h` declares `wex_profile_layout(account, layout)`
and `wex_profile_prepare(prefix_fd, expected_dev, expected_ino, bob_uid,
bob_gid, offline_seed_fd, account, storage)`. Layout computes a candidate;
prepare returns the validated published mapping, which can retain an older
readable label after an authoritative S4 rename. The account input must be the
trusted canonical S4 snapshot independently matched to S10's issued token and
current generation. This API does not authenticate a supplied struct or SID.
Production callers/transport do not exist. The default prepare implementation
always returns DISABLED with cleared results. `WEX_E1S12_PROFILE_TEST` compiles
the Linux implementation and the synthetic root-only CLI; no environment hook
exists. The default CLI rejects every invocation, including lab arguments.

Success returns owned, close-on-exec profile and read/write hive descriptors,
relative and Windows paths, an incarnation-specific random nonzero storage
generation, reuse flag, ACL intent and `acl_pending=1`. Close both descriptors.
The provision lock is released on return; these descriptors are **not leases**
and do not authorize a launch or concurrent HKU load. All failures close owned
descriptors and clear results (fd values -1). S1 kind-6 fields `lease_id`,
`request_id`, `server_gen`, `prefix_id`, `account_key`, binary `user_sid`,
`map_gen`, `profile_generation` and `state` have native reserved storage and
remain entirely zero. This is not a new serialized S1 record or capability.
S14 fills/serializes them from authoritative manager objects, not this manifest.

Names are 1..63 ASCII bytes, starting with letter/underscore and containing
only letters/digits/underscore/hyphen; dots, separators, colon, whitespace,
control bytes and non-ASCII deny. DOS devices and product default/service names
deny case insensitively. The label is lowercased once. Binary incarnation must
be nonzero; SID must be S4's nonlegacy S-1-5-21 with three nonzero domain words
and RID >=1001; account administrator/generation/UID must be nonzero. Every
formatted path fits bounded buffers and the initial Windows path is below
MAX_PATH. Existing label collisions and case aliases deny, including name-only
legacy profiles differing only in case. No implicit legacy migration occurs.

The authority supplies a pinned prefix descriptor/device/inode and a proven
offline seed descriptor. The lab bootstrap opens each absolute ancestor with
openat/O_NOFOLLOW, rejects dot/dot-dot/empty components, retains the descriptor
chain and rechecks it after preparation. Offline seed ancestry is root-owned,
not group/other writable, without extended/default POSIX ACLs or Wine reparse
xattrs. Prefix ancestors can also belong to Bob. The seed is a regular,
single-linked, root-owned, non-writable-to-group/other LF Wine text hive of
24 bytes..1 MiB; pread and before/after size/mtime/ctime checks detect observed
change. Header/selected DWORD duplication or syntax failure denies. Only the
selected boolean enters the new hive. Existing profiles never get reseeded.

The lab process opens authority descriptors as root, clears supplementary
groups and permanently drops real/effective/saved UID/GID to Bob before any
prefix mutation. The library refuses root or a different effective UID/GID.
New directories/files naturally belong to Bob; no chown, permission widening,
foreign-object repair or Alice host-UID access occurs. Prefix/dosdevices/drive_c/
users must already be Bob-owned with Bob's supplied GID and no group/other write;
managed directories are exactly 0700, files exactly 0600, no special mode bits,
POSIX ACLs, special files, hard-linked files or `user.WINEREPARSE` xattrs.
Supported initial filesystems are ext2/3/4, XFS, Btrfs, tmpfs and overlayfs.
Unknown filesystems and unavailable statx mount IDs deny. Every storage child
must have the prefix's device **and mount ID**, excluding even same-device bind
mount substitutions. Kernel/header support for statx STATX_MNT_ID and
renameat2 RENAME_NOREPLACE is a lab prerequisite; there is no unsafe fallback.

The one necessary mapping symlink is validated as data: `dosdevices/c:` must be
Bob-owned and contain **exactly** `../drive_c`, with unchanged link identity.
No filesystem operation follows it. Actual profile traversal opens the real
`drive_c/users` directory chain with no-follow descriptors. All other traversed
directory/file components reject symlinks. Observed edge replacement is checked
before publication and success. This does not prevent arbitrary same-UID native
tampering or make the enclosing home a security boundary.

## Transaction, reuse and recovery

1. Validate the snapshot/prefix/mapping; deny name-only legacy directories or
   an existing preliminary prefix `.e1s` split-tree layout.
2. Open/create checked private `users/.e1s`, open the single-linked 0600
   `.profile.lock`, acquire nonblocking exclusive flock, then open/create the SID
   directory. Another cooperative provisioner receives BUSY, with no results.
3. Under that lock, remove only exact `.txn-<32 lowercase hex>` directories in
   this SID directory after a bounded no-follow ownership/type/mode preflight.
   Unsafe stale state denies and is retained for diagnosis; unrelated names are
   untouched. A valid existing key/label is reused only after exact manifest,
   required skeleton, hive header and full tree validation. More than one label
   for an incarnation denies. Different incarnations have independent storage;
   S14 must enforce one active incarnation per SID when loading.
4. For missing state, allocate a random exclusive 0700 temp directory under the
   same SID parent. Create the bounded shell-default skeleton with explicit
   mkdirat modes and a 0600 Wine text hive containing only audited defaults and
   the validated new shell-folder/TEMP/TMP paths. No scripts, wineboot,
   regsvr32, live HKCU or machine hive mutation runs.
5. Write/fsync the last `.complete` record, binding version, full SID,
   incarnation, initial label, prefix device/inode, random profile generation,
   seed schema 1 and S13_PENDING ACL intent. Fsync the skeleton, revalidate
   directory edges/mapping/tree, then renameat2 with RENAME_NOREPLACE. Fsync the
   SID and ancestor directories before success. No existing destination is
   overwritten. Pre-publication failure cleans only this transaction's temp.

The manifest is consistency metadata in Bob-owned storage, **not authenticated
identity evidence**. Accounts are resolved by S4 and tokens by S10. An existing
profile is not rewritten, including its user hive or completion record. A new
name with the same authoritative SID/key finds the first stored label; a new
key never falls back to the old directory. Missing completion record is a
legacy denial, mismatched record an identity denial, missing required skeleton
or invalid hive an incomplete-state denial.

Kill before rename leaves only an unpublished temp, reclaimed under the next
lock. Kill after rename leaves a whole complete bundle. Directory fsync failure
after rename reports I/O failure even though that bundle may be visible; retry
must validate it and must not overwrite or delete published state. Process-kill
tests do not prove power-loss durability. The provision lock does not replace
S14's hive-in-use/save/recovery lock and reference manager. Tree scans are
bounded to 16384 visits and depth 32; exceeding the supported subset denies.

Private status values (not public authentication errors or NTSTATUS):

| Value | WEX_PROFILE suffix | Meaning |
| --- | --- | --- |
| 0 | OK | Storage returned; ACL application/HKU/lease still pending. |
| 1 | DISABLED | Default build does not provision. |
| 2 | COMPONENT | Invalid canonical input, reserved name or bounded component. |
| 3 | MAPPING | Missing required prefix component or invalid c: mapping. |
| 4 | SUBSTITUTION | Symlink/reparse, changed edge, wrong type or device/mount. |
| 5 | OWNER | Wrong Bob owner/group or wrong execution identity. |
| 6 | MODE | Unsafe permissions, POSIX ACL or file link count. |
| 7 | LEGACY | Fixed/legacy SID, name-only directory, alias, missing completion or old layout. |
| 8 | IDENTITY | Conflicting incarnation/label/manifest or publication destination. |
| 9 | BUSY | Provision lock already held. |
| 10 | SEED | Invalid/unprotected offline seed or malformed/duplicate selected default. |
| 11 | IO | I/O/RNG/allocation/rename/fsync failure. |
| 12 | INCOMPLETE | Missing skeleton/hive or invalid existing hive format. |
| 13 | LIMIT | Directory/depth/size supported limit. |
| 14 | FILESYSTEM | Unsupported filesystem or unavailable mount identity. |

For S13, the precise intended internal handoff is
`apply_e1s_profile_security(trusted_account, pinned_prefix_identity,
profile_fd, hive_fd, profile_generation, acl_intent)` followed by queried-back
verification on the same objects and all existing children. The result must
deny until descriptors, inheritance and actual Wine access checks match the
intent. This is a named required interface, **not an implemented export**.
S13 then registers/loads the exact HKU SID and per-user classes; S14 acquires its
own load/save lock and creates the first nonzero lease before any environment
or target setup. No path, manifest or S12 success bypasses those gates.

## Qwen build/test handoff and evidence

In a disposable Linux wexbuild root session, from the repository root:

```sh
bash scripts/test_profile_storage.sh
```

It builds the native default and lab tools with `-O2 -Wall -Wextra -Werror`,
uses only a fresh `/run/wex-profile-test-*` tree and synthetic numeric Bob
UID/GID 62001 (no passwd/group modification), opens the fixture seed as root,
and drops the lab process permanently to Bob. No existing role, guest prefix,
service, user, machine root or Wine process is touched. Cleanup kills/reaps
only its tracked lab PID and removes only its own temporary root. There is no
root helper installation or delegated launch command. Do not add the define to
normal packaging. The normal binary must deny and lack implementation/barrier
strings. Requires cc, Linux headers/kernel above, coreutils/binutils, Bash;
install `acl` and `attr` for the optional injected POSIX-ACL/reparse cases.

Implemented lab cases, all **unrun here**:

* First provision, exact layout, private temp/skeleton/hive, Bob ownership/modes,
  Alice ACL intent pending, zero S1 reserved fields and default-binary denial.
* Byte-preserving idempotent reuse, unchanged storage generation after a map
  generation change, genuine same-key rename, new-key renamed/recreated account,
  same-name new incarnation and a 63-byte readable component.
* Symlinks at external ancestors/prefix, dosdevices, drive_c, users, managed
  base, SID, profile, lock, metadata, hive and every skeleton directory; bad c:
  mapping; observed ancestor replacement while a transaction is stopped.
* Foreign ownership, unsafe directory/file modes, hard-linked hive, missing
  skeleton/manifest, identity mismatch, duplicate labels, exact/case-folded
  name-only legacy conflict, preliminary split-tree conflict and unsafe stale
  temp symlink; unrelated stale names preserved.
* SIGKILL after lock, temp creation, skeleton, completion fsync and atomic
  rename, followed by recovery; same-account concurrent provision receives BUSY
  then reuses the first result; raced destination never overwritten.
* Separators, dots, reserved/device/product names, controls, non-ASCII, empty/
  overlong names, malformed/zero keys, legacy RID, zero map generation;
  offline seed owner/mode/header/duplicate-DWORD/symlink/hardlink denials.
* Optional POSIX ACL and Wine reparse-xattr injection, reported UNVERIFIED if
  tools are missing. Bob/shared hives, original Alice hive and offline seed
  hashes remain unchanged across the fixture.

Performed here: source/descriptor/cleanup and explicit C/string-escape review,
Git whitespace checks (including new untracked files), and Git Bash `bash -n`
syntax parsing only. No build, fixture, Wine, WSL, guest, role setup or runtime
probe was executed. Qwen must retain compiler diagnostics, final OK/FAIL and
optional UNVERIFIED lines. A native fixture pass is not S11/S16/S20 acceptance.

Still unverified: compilation/linking and Linux-header compatibility; every
test, lock/crash/substitution behavior and leak check; actual seed projection
against a clean installed skel; generated hive loadability and default-folder
consumer behavior; xattr/ACL enforcement and persistence on actual filesystems;
bind-mount rejection, power loss, disk full, allocation/RNG/fsync/rename errors
and large hostile-tree stress. S4 authority/S10 token association is an API
precondition with synthetic CLI inputs, not new authenticated integration.
S13 Windows ACLs/HKU/classes, S14 leases/save/unload, S15 consumer mapping and
S18 broker wiring remain open packages. No new owner decision is needed for
this disabled component; release choices and acceptance gates in E1-S section 9
remain unresolved. No production enablement, Bob-profile changes or commit.

Tooling observations: the default PowerShell runner returned empty successful
output for initial reads; those were discarded. Explicit `cmd.exe`,
`login:false` restored visible output. A quoted multiword rg expression was
split by the runner (OS error 123); single-token `-e` searches avoided it.
Broad rg/status reported the existing inaccessible `unsloth-tmp` directory;
targeted source reads succeeded. `git diff --no-index --check NUL NEWFILE`
returns 1 for the differing new file with no whitespace diagnostics, not a
runtime failure. A temporary stray test-edit placeholder was removed before
syntax review. No failed command changed VM/service/fixture state.
