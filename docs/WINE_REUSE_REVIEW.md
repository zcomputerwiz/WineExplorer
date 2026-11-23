# Wine changes: reuse, consolidation, simplification

Review of 2026-11-14 of the project's own changes in `wine/` (Wine `development` at 5ec3c009 plus the hook split;
about 60,000 added lines in 336 files since upstream release 11.21, 2528aff2). Nothing here is changed yet: this is
the list to work from. Line numbers are of that day. Each finding says whether it was checked by hand.

Sources: the system tier server and client code by Claude; the graphics and DWM side by Qwen (spot-checked); the UWP
runtime DLLs and the prior-art search in ReactOS and Wine by Antigravity (see their sections).

## Priorities

| # | Finding | Saving | Risk | Checked |
|---|---|---|---|---|
| S1 | One reader for `/etc/wineexplorer/sysrun.conf` in the server (read in four files) | ~80 lines, one rule | low | yes |
| S2 | One "is the profile managed" helper for shell32/userenv (six copies) | ~60 lines | low | yes |
| Q1 | dcomp: four device vtables repeat the same methods | ~400-500 lines | medium | yes (sample) |
| S3 | One key-path walk in `registry.c` (three copies) | ~30 lines | low | yes |
| S4 | pidfd constants in one server header (five places, two as bare numbers) | ~15 lines, clarity | low | yes |
| S5 | One group-name check (`valid_name` in `accounts.c` and `registry.c`) | ~10 lines | low | yes |
| Q12 | uxtheme and dwmapi read the same DWM registry values | ~30 lines | medium | yes |
| A1 | windows.storage's `vector.c` is a 1,469-line copy of windows.media.speech's | ~1,400 lines | low | yes |
| A2-A3 | UWP DLLs: own map/property set beside wintypes', three `IAsyncAction` implementations | ~950 lines | low-medium | no |
| P3 | Port ReactOS userenv's profile functions (`DeleteProfileW`, `CopyProfileDirectoryW`, path queries) | new APIs | low | yes |
| P1 | `NetGetJoinInformation` through Samba's `libnetapi` (needs a new wrapper) | removes a script and a key | medium | yes |

## System tier: server (`wine/server`)

**S1. `sysrun.conf` is read and checked in four places.** `sysrun.c:92` (`SYSRUN_CONF`), `session0.c:338`
(`SESSION0_CONF`, the same path under a second name), `registry.c:592` (`admin_group_name`, the path spelled out) and
`accounts.c:280` (`read_policy` through its own `read_protected`). Each repeats the safety check (regular file, owned
by root, not group or world writable) and its own line parser; `registry.c` says in a comment that it follows "the
same rule as read_policy() in accounts.c". *Change:* one parser in `sysrun.c` (which already tracks the file's
generation, `sysrun.c:622`) that returns the parsed values (`admin_group`, `system_group`, `session0_socket`, ...);
the others call it. *Why it matters:* a rule written four times drifts; this one is security relevant.

**S3. Three copies of "walk a key path" in `registry.c`.** `find_subkey_by_path` (524), `e1s_source_path` (2791,
also refuses deleted and symbolic-link keys) and the loop inside `reset_policy_trees` (about 1275-1290, deletes a
link key it meets). *Change:* one walker taking a flag (refuse links / delete links / follow). Upstream's own
`open_key` path parsing (`get_path_element`, 756, 1440, 1712) stays as it is.

**S4. pidfd constants.** `SO_PEERPIDFD` is defined in `session0.c:641`, as `SO_PEERPIDFD_LAB` in `sysrun.c:476` and
written as a bare `77` in `named_pipe.c:2048`; `SO_PASSPIDFD` in `process.c:144`; `SCM_PIDFD` in `request.c:362`.
*Change:* one block of `#ifndef ... #define` in a server header (`sysrun.h` or a new `wex_unix.h`). The same values
are defined again in `src/dlls/wexhost/e1s6.c:40` and `tools/e1s7/pam-worker.c:34` (other programs; a shared header
in `src/include` would do for those two).

**S5. Group names.** `valid_name` in `accounts.c:82` and `registry.c:568` are the same function (the comment in
`registry.c` says so). `wex_profile_storage.c:26` has a `valid_name` too, but with a different rule (account names
with reserved words): it stays separate and should be renamed `valid_account_name` to avoid the confusion.

**S6. Two bounded no-follow tree walks.** `e1s_profile.c` `secure_tree` (168) and `wex_profile_storage.c`
`walk_tree` (546) both walk a directory tree with depth and count limits, refuse links and devices and compare
`fstatat` with the opened descriptor. Their checks differ (one sets security descriptors, one may delete). *Change:*
a shared walker with a callback per entry. *Risk:* medium (security code; both have their own tests). Lower priority.

**S7. Little-endian field helpers.** Four sets: `wex_token_put32/64` (`include/wine/wex_token.h`), `e1s6_put32/64` and
`e1s6_u32/u64` (`src/include/e1s6_protocol.h`), `e1r8_put_u32/...` (`src/include/e1r8_codec.h`),
`put32/put64` (`tools/e1s7/pam-worker.c`), and hand-written shifts in `named_pipe.c` (10 places) and `sysrun.c`
(3). *Change:* the server and the DLLs use `wex_token.h`'s; `src/` code one header. Small saving, fewer mistakes.

**S8. Two copies of the elevation helper rule.** `registry.c:549` (`elevation_helper_unset`) and ntdll's
`is_elevation_helper_unset` apply the same rule in two processes (the comment says so). They cannot share code
(server and client), but a server request answering the question would let ntdll ask instead of reading the
registry itself. Low priority.

## System tier: client DLLs

**S2. "Is this user's profile managed?" six times.** `shell32/shellpath.c` (2448, 2703, 3117) and
`userenv/userenv_main.c` (501, 542, 643) each look up `NtLoadWexProfileEx` with `GetProcAddress` and call it with
`WEX_HKU_QUERY` the same way; `userenv` 255 and `kernelbase/registry.c` 2876 do similar calls. *Change:* one inline
helper in `include/wine/wex_profile.h` (`wex_profile_managed( &generation )`). All of it is behind
`WEX_E1S13_HKU_TEST` today, so this is best done together with step 2 of `E1_PRODUCTION_PLAN.md` (the product flag).

**S9. The domain key read twice.** `wkscli/wkscli.c:52` and `dsrole/dsrole.c:82` read
`HKLM\Software\WineExplorer\Domain` the same way. Small; see P1, which would replace both.

## Graphics and DWM (Qwen, spot-checked)

**Q1. dcomp device vtables** (`dlls/dcomp/device.c` about 4400-5470). `Commit`, `WaitForCommitCompletion`,
`GetFrameStatistics` are identical in `IDCompositionDevice`, `IDCompositionDevice2`, `IDCompositionDevice3` and
`IDCompositionDesktopDevice` (checked: 4419, 4683, 4918, 5244), and so are the transform, effect, clip, surface and
target creators, apart from the `impl_from_*` cast. *Change:* forwarders from one implementation per method (a macro
per method family, the way other Wine COM code does). *Saving:* about 400-500 lines. *Risk:* medium, mechanical; the
vtable slot order must stay exact.

**Q2.** 15 three-line `dcomp_{translate,scale,rotate,skew,matrix}_{qi,addref,release}` wrappers (2637-2697): one macro.

**Q3.** Three identical animation-sampling helpers (`dcomp_clip_sample_value` 2170, `dcomp_effect_group_sample_opacity`
2204, `dcomp_animated_float_sample` 2375): one `dcomp_sample_animation`.

**Q4.** `dcomp_visual_apply_commit` (3805) and `dcomp_collect_pending` (3992) sample the same visual state: one helper.

**Q5-Q8. `server/dwm.c`.** Request header validation in `dwm_send` and `dwm_reply` (824-848, 1091-1113); the two
message-abort loops of `dwm_channel_disconnect` (262-308); the dead-channel test written four times (814, 944, 1000,
1083); reply fields filled twice in `dwm_recv`. Small helpers, about 75 lines together, low risk.

**Q9-Q10. dwmapi.** `DwmSetIconicLivePreviewBitmap` and `DwmSetIconicThumbnail` share their validation (1297-1338);
the rate-reduction loop in `DwmGetCompositionTimingInfo` is written twice (962-995).

**Q11-Q12. uxtheme and dwmapi.** `aero_glass` and `UXTHEME_NonClientFrameDraw` each load `dwmapi.dll` by hand
(`window.c` 283, 1169), as does dcomp (3746). `DwmFrameStyle`, `DwmFrameEffects` and `ColorizationColor` are read
from the registry by both uxtheme (`window.c` 268-343) and dwmapi (194-219, 368-385) (checked). *Change:* uxtheme asks
dwmapi. *Risk:* medium (the two DLLs are independent today on purpose: uxtheme works without dwmapi's broker).

**Q13.** `dcomp_surface_unknown_Unknown3` (976) prints a FIXME but is implemented (checked): drop the FIXME.

## UWP runtime DLLs (Antigravity, spot-checked)

These DLLs are the UWP session's work (`docs/uwp`); the list is for that session to take or leave. Checked by hand:
A1 (the copy differs from its source only in the first comment and the debug channel), A9 (the handler list helpers
exist as described in `twinapi.appcore/core_view.c` 36-111), A20 (`vector_hstring_create_copy` is used by
windows.media.speech, not by windows.storage).

| # | Finding | Saving | Risk |
|---|---|---|---|
| A1 | `windows.storage/vector.c` is a copy of `windows.media.speech/vector.c` (1,469 lines); only `vector_hstring_create` and `vector_inspectable_create` are used. Share it (a static library or `include/wine` helper, as Wine does for other WinRT helpers) or drop the unused types | ~1,400 | low |
| A2 | `windows.storage.applicationdata/propertyset.c` (655 lines) is its own map and property set, while `wintypes` has `multi_threaded_map_create` and `IPropertySet`; ours adds a change callback and a snapshot enumerator, and `MapChanged` is a stub | ~600 | medium |
| A3 | Three `IAsyncAction` implementations: `applicationdata.c` 60-203, `windows.ui/dispatcher.c` 119-425, `windows.storage/async.c` 857-975 | ~350 | low |
| A4 | `windows.ui/pointer.c`: hand-written forwarding for `IPointerDevice`, `IPointerPointProperties`, `IPointerPoint`; Wine's `DEFINE_IINSPECTABLE` does this | ~200 | low |
| A5 | `wintypes/datareader.c` 381-540 writes its own `IAsyncOperation<UINT32>`; `wintypes/storage.c` already has `DEFINE_IASYNCOPERATION` | ~150 | low |
| A6 | `windows.storage/async.c`: `DEFINE_IASYNCOPERATION` is a copy of `DEFINE_IASYNCOPERATIONWITHPROGRESS` without the progress methods | ~130 | low |
| A7 | `create_buffer` twice (`fileio.c`, `wintypes/datareader.c`); the `IBufferByteAccess` query-use-release pattern 8 times | ~120 | low |
| A8 | `windows.ui/window.c` 188-338 writes out what its own `ARGS_COMMON` macro (340) generates | ~90 | low |
| A9 | Event handler lists twice: `twinapi.appcore/core_view.c` 27-111 and `windows.ui/dispatcher.c` 30-117 | ~85 | low |
| A10 | Two activation factories written out in `windows.storage` (`folder.c` 1888, `fileio.c` 547) | ~70 | low |
| A11 | The `TokenWinePackage` query loop three times (`activation_manager.c`, `core_application.c`, `combase/roapi.c`) | ~50 | low |
| A12-A19 | Smaller: table-driven value encoding in `applicationdata.c`, a `DataReader` read macro, `delete_tree` twice, `START_CALL` defined in three files, the package data root computed twice, `DEFINE_IINSPECTABLE` for two more objects, a locality table | ~200 together | low |
| A20 | `vector_hstring_create_copy` unused in windows.storage (part of A1) | ~22 | low |
| A23 | A 26-entry extension-to-MIME table in `folder.c`; Wine has `HKCR` content types and `FindMimeFromData` | ~20 | low |
| A24 | `core_application.c` parses the command line by hand; `PathGetArgsW` does it | ~12 | low |
| A25 | `applicationdata.c` 957-963 waits for an async operation with `Sleep(1)` for up to 10 s: a completion handler and an event instead (correctness, not size) | small | low |

## Prior art in Wine and ReactOS

Searched in ReactOS (`references/reactos`, GPL-2.0-or-later, may be ported) and in the Wine tree, by Antigravity
and by hand. ReactOS's system side is NT executive, RPC servers (LSA, SAM, SCM over RPC) and winlogon/GINA, which do
not map onto one Unix wineserver; its user-mode DLL code often does.

**P1. Domain join through Samba's `libnetapi`.** Wine's `netapi32` loads Samba's `libnetapi` when present
(`dlls/netapi32/unixlib.c`), but wraps only `NetServerGetInfo`, `NetShare*` and `NetWkstaGetInfo` (checked);
`libnetapi` itself has `NetGetJoinInformation`. A wrapper there could answer `NetGetJoinInformation` and
`DsRoleGetPrimaryDomainInformation` from Samba instead of the key that `installer/scripts/wex-domain-state.sh` writes
from session 0. *Against:* only where Samba's library is installed, and every user process would then talk to Samba
itself. ReactOS's versions (`netapi32/wksta_new.c`, `base/services/wkssvc/domain.c`, `lsasrv/dssetup.c`) go through
RPC to services we do not have and report a standalone workstation: no reuse. Keep the key; revisit only if the
script becomes a problem.

**P2. Group Policy.** Neither ReactOS nor Wine has a `Registry.pol` (PReg) parser, `gpsvc` or policy engine (checked
by both); `wexpolicy` stays. `userenv/gpolicy.c` is already the ReactOS one; `GetAppliedGPOList` and
`ProcessGroupPolicyCompleted` are stubs in ReactOS too.

**P3. User profiles: ReactOS userenv is real prior art for the Win32 side.** `dll/win32/userenv/directory.c`
(`CopyProfileDirectoryW`, `CopyDirectory`, `CreateDirectoryPath`, `RecursiveRemoveDir`), `profile.c`
(`CreateUserProfileW/ExW`, `DeleteProfileW`, `GetProfileType`, the profile path functions) and `setup.c`
(`InitializeProfiles`, `CreateStandardProfile`) are user-mode Win32 code that Wine's `userenv` lacks or stubs. Not a
replacement for `wex_profile_storage.c` / `e1s_profile.c` (those build the profile on the Unix side with the
ownership rules session 0 needs; ReactOS's `LoadUserProfileW` uses `RegLoadKey`, which Wine routes through the server
differently), but a port of these functions would give installers and tools the standard APIs. Effort: about a day
each for `DeleteProfileW` and the path functions, more for `CreateUserProfileW`.

**P4. Well-known SIDs.** ReactOS `dll/win32/lsasrv/lookup.c` (`LsapInitSids`) has a fuller table of well-known SIDs
and names than Wine's `ACCOUNT_SIDS` in `advapi32/security.c`; the table (not the RPC engine) can be taken over.
Relevant to `LookupAccountSidW`, which the system tier already extended (`docs/STOCK_CHANGES.md`).

**P5. Consent dialog strings.** ReactOS `dll/win32/shell32/lang/*.rc` has the RunAs dialog's strings ("User name",
"Password", errors) in about 30 languages; they can seed `consent.rc` translations. The dialog code itself
(`shlexec.cpp` `RunAsDlgProc`) is a plain RunAs box without the broker: no reuse. `credui` is Wine's own.

**P6. Services and session 0.** ReactOS's SCM, smss and winlogon have no cross-session service forwarding of the kind
`session0.c` and `programs/services` do; no prior art there (checked).

## Suggested order

1. S1, S4, S5, S3 (server, low risk, together one commit; normal and lab suite).
2. Q13, Q2, Q3, Q5-Q10 (small, low risk; the DWM and dcomp tests).
3. S2 with step 2 of the production plan.
4. Q1 (large, mechanical: a good written task for Qwen, with the vtable order checked by a build and the dcomp tests).
5. A1, A3, A5-A11 offered to the UWP session (its DLLs, its tests).
6. P3, P4, P5 as small ports when the profile and consent work next touches those files.
7. P1, A2 and Q12 only after a decision.

Started 2026-11-14: S4, S5, Q2, Q3, Q13 (Qwen, branch `claude/wine-reuse`).
