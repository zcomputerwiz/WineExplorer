# Changes to stock Wine / third-party code

## Independent token-security correctness repair (2026-10-18)

`codex/token-security-correctness` retains deny-only groups in the deny-ACE
matching path even when ENABLED is clear, while excluding them from allow ACEs.
Shell elevation preserves helper cancellation versus actual startup/unsupported/
timeout errors and never returns a target handle on helper rejection. Duplicate
fork `programs/consent` sources were removed (recoverable in Git); the owned
`src/programs/consent` module is the single assembly source. It has no shipping
fixture/direct-password-helper bypass and fails unsupported credential requests
closed. This is functional repair, not complete host authorization/containment.
See TOKEN_SECURITY_REPAIR_REVIEW.md and the checkpoint-B work order.

## Checkpoint B: elevation helper audit and token restriction (2026-10-22, corrected)

- `server/token.c`:
  - Added an explicit `is_restricted` flag in `struct token`.
  - Replaced the synthetic Null SID (`S-1-0-0`) inserted for a disjoint restriction with an empty restricting
    set that stays restricted, so every access check is denied.
  - `filter_token` fails with `STATUS_NO_MEMORY` when allocating a restricting SID fails, without publishing
    a partial token.
- `dlls/shell32/shlexec.c`:
  - Three-state helper status: unset (key or value absent, or an empty string), configured, invalid (wrong
    type, odd or oversized data, whitespace, control characters, read error). Invalid fails closed with the
    exact error and no process handle.
  - With the helper unset, `ERROR_ELEVATION_REQUIRED` from `CreateProcess` returns `ERROR_BAD_FORMAT` as before.
- `dlls/kernelbase/process.c`: `is_elevation_required` reads the helper first. Unset returns before the
  manifest is parsed; anything else falls through to the manifest check.
- `dlls/ntdll/loader.c`: `is_elevation_helper_unset` replaces the boolean check; only an unset helper lets the
  loader elevate silently.
- `programs/conhost/window.c`: `update_window` skips the maximum window size computation when the console
  font cell size is zero (no usable console font), which raised an unhandled division by zero.
- `programs/explorer/systray.c`: no change. The checkpoint B edits were reverted.
- `dlls/advapi32/tests/security.c`: `test_restricted_sid_intersection` expects 0 restricting SIDs for a
  disjoint intersection.


## Elevation boundary, plan steps 2 and 3 (2026-10-22)

Everything below applies only while `HKLM\Software\Wine\Elevation\Helper` (64-bit view) is configured, using
the same unset rule as the ntdll loader. With the helper unset nothing changes. Native values come from
`TOKEN_SECURITY_REFERENCES.md`.

- `server/registry.c`: `elevation_helper_unset()` reads the helper from the in-memory registry;
  `object_is_registry_key()`.
- `server/security.h`: `elevation_enforced()` and the declarations below.
- `server/token.c`:
  - tokens carry a `lineage_id` shared by a token and its duplicates and filtered copies;
  - `token_check_assign_primary()`: a non-primary token below `SecurityImpersonation` is refused
    (`STATUS_BAD_IMPERSONATION_LEVEL`); a token outside the caller's lineage needs an enabled
    `SeAssignPrimaryTokenPrivilege` or `SeTcbPrivilege` (`STATUS_PRIVILEGE_NOT_HELD`), re-implementing ReactOS's
    child/sibling rule;
  - `token_duplicate` will not raise an identification-level token to a primary token;
  - `check_object_access` fails every open made while impersonating an identification-level token
    (`STATUS_BAD_IMPERSONATION_LEVEL`, a registry key `STATUS_ACCESS_DENIED`).
- `server/process.c`: `new_process` and `set_process_info(SET_PROCESS_INFO_TOKEN)` call
  `token_check_assign_primary()`; `grant_process_admin_token` needs an enabled `SeTcbPrivilege`.
- `dlls/advapi32/security.c`: `CreateProcessWithTokenW` needs `SeImpersonatePrivilege`
  (`ERROR_PRIVILEGE_NOT_HELD`).
- `dlls/shell32/shlexec.c`, `dlls/kernelbase/process.c`, `dlls/ntdll/loader.c`: read the helper from the 64-bit
  registry view, as the server does. 32-bit processes previously read `Wow6432Node`.
- `dlls/advapi32/tests/security.c`, `dlls/shell32/tests/shlexec.c`: conformance tests for the above, and the
  approved-helper cases now expect `ERROR_BAD_IMPERSONATION_LEVEL` until the elevation broker exists.

## Elevation boundary, plan step 2b: the limited token (2026-10-22)

Wine commits `4fced06b`, `839c0f31`, `4fa6f4ac` (hashes of the working branch; the pushed ones differ).

- `server/token.c`, `server/security.h`: the `LUA_TOKEN` and `DISABLE_MAX_PRIVILEGE` bodies of
  `DECL_HANDLER(filter_token)` moved into `token_apply_filter()`; `filter_token` behaves as before.
  `token_make_filtered()` applies the filtering to the root token. No behaviour change on its own.
- `server/process.c`: while a helper is configured, the root token (the one every process inherits) goes through
  `token_make_filtered()`: Administrators deny-only, `SeChangeNotifyPrivilege` only, Medium integrity, elevation
  Limited. Helper unset: unchanged. The registry is loaded in `init_registry()` before the first client can
  connect, so the setting is known when the first process is created.
- `server/token.c` (`token_make_filtered`): the filtered root token keeps the native filtered-admin privilege set that
  Wine defines (D1): `SeChangeNotifyPrivilege` enabled, `SeShutdownPrivilege` and `SeUndockPrivilege` disabled.
  `filter_token` with `DISABLE_MAX_PRIVILEGE` still keeps only `SeChangeNotifyPrivilege`, as on Windows.
- `server/token.c` (`create_linked_token`): the linked (full) token is the same token, but its handle is now granted
  with the access the caller's integrity level allows (`check_object_access` with `MAXIMUM_ALLOWED`) instead of an
  unconditional `TOKEN_ALL_ACCESS`. A Medium caller otherwise cannot get a handle to a High-labelled token (error 5).
  For a High caller the granted mask is the same as before.
- `server/registry.c` (`key_get_sd`): the default key descriptor now has two variants. Keys under `\Registry\User`
  grant the user, SYSTEM and Administrators full control; other keys grant Users read on the key itself, SYSTEM and
  Administrators full control, and keep the old inherit-only Users ACE last (`advapi32` test `test_GetNamedSecurityInfoA`
  inspects the last Users ACE). Unconditional. **Note:** `check_object_access` treats a key with no explicit
  descriptor as unprotected, and `key_get_sd` is only used when a descriptor is queried, so this change alters
  what `GetSecurityInfo` returns, not what a limited process may do. See the step 2b work order, "Results".

## Elevation boundary: registry and service manager access checks (2026-10-22)

Applies only while `HKLM\Software\Wine\Elevation\Helper` is configured (`elevation_enforced()`), except where
marked unconditional. Work order: `ELEVATION_REGISTRY_ENFORCEMENT_WORKORDER.md`. Wine commits (working branch
`ccr-work`): `91b53ca8`, `c28aafba`, `437525d3`, `2487739e`, `9bb835d1`, `a252e52b`, `d668329f`.

- `dlls/win32u/sysparams.c` (`reg_empty_key`): stops at the first failed delete instead of enumerating index 0
  forever. **Unconditional**, upstreamable.
- `server/token.c` (`check_object_access`): a registry key without an explicit descriptor is checked against its
  default one (`key_get_sd`) instead of being granted.
- `server/registry.c`:
  - `create_key` checks `KEY_CREATE_SUB_KEY` on the parent before it creates a new key (a lookup of the parent
    first, so a denied create leaves no key behind). The handle of a new key is still granted without a check;
    opening an existing key already checked the requested access.
  - **Deviation, accepted by the user (work order item 2):** the keys of the display device cache default to
    Users full control (`display_cache_paths`, `key_in_display_cache`): `Enum\DISPLAY`, `Enum\PCI`, the display
    and monitor `Control\Class` GUID keys, `Control\Video`, `Control\DeviceClasses`, `Hardware Profiles\Current`,
    `HARDWARE\DEVICEMAP`, `Software\Microsoft\DirectX`, with their subkeys. win32u's `prepare_devices` rewrites
    them in every GUI process, where Windows has SYSTEM components do it. A new key above one of them (for example
    `HARDWARE\DEVICEMAP`) may be created too. Effect: a limited process can change the cached display configuration.
  - **Deviation, accepted by the user, temporary:** `Control\ServiceCurrent` (the control pipe number services.exe
    writes for each service process it starts) and `Control\ComputerName\ActiveComputerName` (written by
    `wineboot --init`) are in the same table. Both writers run on the limited token until they get the system
    role; that step removes the two entries (`shortcut:` comment in `server/registry.c`).
- `server/named_pipe.c`: the client end of a named pipe records the token of the thread that opened it, and
  `FSCTL_PIPE_IMPERSONATE` impersonates a copy of that token (stock Wine impersonates the caller itself, with a
  FIXME). Needed so a service can check the access its client really has.
- `programs/services/rpc.c`: `OpenSCManager`, `OpenService` and `CreateService` check the requested access against
  the documented default descriptors of the service control manager and of a service, with the RPC client's token
  (`check_client_access`). The checks are skipped while no helper is configured.
  The default service descriptor gives LocalSystem Wine's `SERVICE_SET_STATUS` (0x8000) too: plugplay in
  `winedevice.exe` (SYSTEM) opens the driver service of `ZwLoadDriver` (for example `winehid`) with it, and no process
  record of `services.exe` names that process for a driver, so the own-process rule did not apply and the load failed.
- `server/named_pipe.c`, `server/token.c` (`token_impersonation_level`): the level a pipe server gets is capped like
  Windows' SeTokenCanImpersonate. Without `SeImpersonatePrivilege` in the server's process token it is
  identification, unless the client has the server's user, is not more trusted (integrity) and is not elevated
  while the server is not. The client's requested SQOS level is not sent to the server (`shortcut:` comment), so
  the cap is the client token's own level. `open_token` with `OPEN_TOKEN_AS_SELF` checks access against the
  process token, so an identification-level caller can still read its client's identity.
- `programs/services/rpc.c`, `dlls/rpcrt4/rpc_transport.c`, `dlls/sechost/service.c`: `OpenService` also grants
  the process the SCM started for a service the rights it uses to report status
  (`SERVICE_SET_STATUS`, query, `READ_CONTROL`), found by comparing `I_RpcBindingInqLocalClientPID` with the
  recorded process id. rpcrt4 implements that call for `ncacn_np` (stock has it for `ncalrpc` only). sechost opens
  the second handle of its own service with `SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG` instead of
  `GENERIC_READ|GENERIC_WRITE`. `RpcSs` is openable for start and status by anyone, because Wine starts it on demand
  (`shortcut:` comment).
- Callers that asked for `KEY_ALL_ACCESS` on `HKLM` only to read: `dlls/ntdll/version.c` (Windows version values,
  `KEY_READ`), `dlls/kernelbase/locale.c` (NLS and time zone keys, `KEY_READ`). Upstreamable.
- `dlls/shell32/shellpath.c` (`_SHOpenProfilesKey`): when the create with `KEY_ALL_ACCESS` is denied, opens
  `ProfileList` for `KEY_READ`, so shell folders resolve for a limited token.
- `programs/explorer/desktop.c`: the display driver key is created with `MAXIMUM_ALLOWED`; creating a missing
  parent chain opens the existing parents with the requested access, which `KEY_SET_VALUE` is not on `System`.

Anything under `wine/` that is not new project code, and what we changed. Keep this list short: every entry makes
rebasing on upstream Wine harder. Refactor baseline = the real `wine-11.19` tag
(`455e3509b98a6919fd4ad1def4803e08c41c03b2`); see `REFACTOR_MAP.md` for the audit.
Commit `40c17e1` is the imperfect upstream import, which also includes post-tag upstream changes.

P4 established Wine as a gitlink to d0a0af0681 on wineexplorer/11.19.
Current pin is ed5fe96, integrating the L2 topic through the real-program
checkpoint in WAYLAND_SESSION.md, visual-style support, prefix-split S2/S3
and the independent Fluent-theme changes. The native tray bridge is owned
wexhost/wexshell code and adds no stock-Wine patch.
Fork-only ed5fe96 keeps hosted groups active through logical client-rectangle
changes and orders the layout request before frame commits. Layout replies
do not replay first-adoption activation. The private protocol wire format is
unchanged. Together with compositor acc46fe6 and the owned shell's stale-scale
observation change, maximized foot passes 100%→150%→100%: hosting,
focus/input, client alignment, work area and taskbar. Restored-window scaling
remains intermittent and open; the initial passing run is not a complete
fix. Two follow-up owned-shell trials were unsuccessful and removed. Both
Wine architectures build; fresh-ISO installs, application and tray checks
pass with the restored-scale failure recorded in WAYLAND_SESSION.md.
The polish topic `upstream/win32u-surface-shape` (bcf842a) fixes win32u's
single-byte surface-shape spans: intersect the left mask with the complement
of the right mask, matching the multi-byte path. This fixes volumeicon
Preferences' missing side borders under Wayland. The full 32/64-bit build and
real-program frame matrix at 100%/150% pass. It is also on wineexplorer/11.19.
The earlier "topic, not production" descriptions below are historical: those
commits are now included. Fresh ISO installation acceptance passed: default-X11
Alice login, selected-Wayland Bob login, 32/64-bit applications, real native
applications and the prefix-split assertions (see WAYLAND_SESSION.md).
See WAYLAND_L1.md;
X11 remains the default.
The final C/D same-ISO regression also passes at b1d3ab0 in fresh default-X11
and selected-Wayland installations, including tray and X11 effects/dialog
checks. See WAYLAND_SESSION.md review point 2 for exact image and evidence.
Wine topic 0f25ee1 fixes the existing shell-view default icon spacing call on
64-bit Wine: MAKELPARAM(-1,-1) zero-extends to 0xffffffff, so comctl32 treats
it as literal 65535x65535 spacing. Passing (LPARAM)-1 restores the intended
default. The actual installed Wayland Control Panel's 13 entries now draw;
both architectures build.
The L2 topic c49ebd8 keeps pointer motion outside the old surface bounds during
explicit private compositor capture. A real foot window on the installed
Wayland session could otherwise never grow past its original right/bottom edge.
Ordinary uncaptured motion retains Wine's rounding clamp. Physical QEMU edge
grow/shrink, corner grow and subsequent typing pass. This is still on the L2
topic, not the production pin; see WAYLAND_SESSION.md for the active work order.
Topic 6b3435c coordinates real foot work-area maximize/restore using private
scalar capture and maximize requests in the existing Wayland driver path.
Topic cc59531 draws caption buttons within Wine's metric hit slots: real foot's
drawn maximize glyph previously hit HTMINBUTTON. Topic ac2cc1e restores held
modifiers from wl_keyboard.enter before native xterm's Alt+Tab reaches Wine,
and keeps private-shell topmost overlays independent so Explorer's switcher
is not clipped into the hosted client hole. No driver ABI hook or protocol
version was added. These remain topic commits, not the production Wine pin.
Topic 4fb0940 publishes the existing preferred wl_surface scale as a private
scalar property with an owning-thread notification, so native inventory's
logical rectangles can be reconciled into Wine pixels. It refreshes zone
placement and reserved edges at a scale change. Actual foot resize,
maximize/restore and typing at 150% pass; live maximized scale change remains
unresolved. The owning bridge uses the same scale conversion as the driver.
Topic 055abb3 reuses the existing transparent dummy-content helper when an
all-client hosted CSD proxy changes size without GDI damage. Actual gedit at
150% otherwise loses its taskbar item after startup work-area clamping;
typing and taskbar minimize/restore pass with the fix. Visible SSD pixels and
minimized proxy geometry do not use this path.
The 6b3435c requests preserve the compositor's physical focus/capture checks. Controlled
hosted views acknowledge xdg state without allowing an obsolete
NOSENDCHANGING configure size to overwrite the native-ready frame, and flush
pending frame content after acknowledgement. No generic Wine hook or private
protocol version was added. Exact work-area maximize, taskbar minimize/restore,
menu Restore, and post-restore typing pass on the installed test overlay at 100%.
The L2 topic's cf365e5 binds private protocol v10 and adds a scalar resize
request/commit/cancel transport. Each compositor transition is queued separately
and delivered on the HWND's owning UI thread with a scoped property snapshot;
no Unix pointer enters WoW64 PE and no application call holds win_data_mutex.
Surface generations protect destruction/replacement during reentrant delivery.
Accepted transactions preserve hosting while the frame catches up; only matching
serial completion updates the accepted client hole. Protocol availability is
distinct from native-client eligibility and from enabled sizing controls.
This is fork-only and does not add a driver ABI hook or enable interactive
resize/maximize. See WAYLAND_L2_RESIZE_REVIEW.md for the next boundary review.
The subsequent owned wexhost WH_MSGFILTER handles pending transaction dispatch
and terminal-input deferral without another stock Wine change. The unhosted
64/32 keyboard-loop baseline reproduces production 71572805's first-timer exit;
existing L2 topic commits 445b580 and 903ac75 already fix the generic pre-press /
capture-cancellation loop behavior. Post-release pending publication is handled
in the owned hook, not by adding private shell checks to generic win32u.
Wine topic 07eb6e0 adds a private RESIZE_ENABLE lab property and routes SC_SIZE
to the Win32 sizing loop only for an explicitly opted-in v10 hosted frame.
Stock/old peers and ordinary Wine windows retain their existing xdg handling.
The owned wexhost controller records desired rectangles separately, applies
native-ready frames and publishes only after terminal input. Production controls
do not set this property; live drag previews, DPI and broader-client acceptance
remain pending. No generic win32u hook or new cross-architecture ABI was added.
The next topic commit a061cea queries SPI_GETDRAGFULLWINDOWS for top-level
size/move loops as it already did for child windows, instead of forcing live
dragging. It introduces no private-property check or driver hook in win32u.
The owned controller now publishes matching live previews, retains loop-original
Escape geometry, quarantines FAILED hosting and latches constrained final
geometry in drag-off mode. All of this remains opt-in; DPI and remaining
cancellation/regression acceptance are required before enabling controls.
Compositor 8cb6e315 adds only delayed/ignored/client-selected test publishers;
the compositor runtime is unchanged at e8e5a083.
Wine 7efbcbb subsequently aligns event client rectangles to the existing
surface-local frame grid when absolute rounding would produce a different hole
or origin. It keeps the usual physical rectangle when that maps exactly.
Compositor bea783b6 allows a one-logical-pixel outer-border quantization at
fractional scales only, while keeping native/client-hole dimensions exact,
requiring the hole inside the frame and preserving strict integer-scale
extents. It also reasserts retained pixels' visible primary output before frame
callbacks. No request, ABI or protocol version was added. Actual small-step foot
acknowledgement-only buffer reuse remains an acceptance blocker; review it before
changing readiness semantics or exposing controls. See WAYLAND_L2_RESIZE_REVIEW.md.
Project CPLs/programs, wexhost and theme definitions live under src.

The L2 topic merged production d92d7e4 in 0607ece without changing the shared
registry implementation. Compositor eca5e619's rollback fix preserves the
forward content-response requirement: only an acknowledged rollback surface
commit may confirm the identical retained original buffer at its original
dimensions. Bare acknowledgements, unacknowledged commits and different buffers
cannot use this exception. This restores safe cancellation for foot's unchanged
buffer responses; it does not declare them successful forward resizes or enable
production controls. See the 2026-10-16 section in WAYLAND_L2_RESIZE_REVIEW.md.
L2 opt-in topic codex/wayland-l2 (07eb6e0, including merge 2c3be3b of production
859a1b9, not the production pin) extends the
private protocol through v10, starting with v4 native/controller atom properties and an
asynchronous reservation status. winewayland serializes requests and maps owned
native proxy HWNDs as independent toplevels while keeping ordinary owned-window
behavior. The standard wlr XML is vendored only to satisfy private protocol
interface definitions, with its original notice. Reservation leaves native
composition/focus unchanged. Explicit HOST opt-in now uses v7 numbered group
configuration and focus acknowledgements, preserving logical foreground during
native input and guarded temporary frame focus. Client size changes detach
before retry; callers must exclude the client area from Win32 hit testing.
Private hosting changes are fork-only. Compositor topic d859bdf1 provides v5 active scene grouping
with frame-ring render/input exclusion and disconnect recovery, plus v6 observed
physical focus acknowledgements and guarded frame/native restoration, plus v7
numbered configuration and native size/policy recovery. Driver fallback for old
servers remains. Owned wexhost now binds explicit SSD hosts through the v7
properties with UI-thread lifetime, region and focus-loop management; Explorer
adoption is now gated by WEX_WAYLAND_NATIVE_WINDOWS=1 for isolated tests; AUTO
remains disabled. The compositor also publishes replacement decoration policy
atomically, avoiding a false CSD notification during activation. It now observes
mapped xdg-toplevel parent changes with a lifetime-managed listener and publishes
them through the standard foreign-toplevel interface. Explorer detaches native
child proxies before destroying their owner frame, preserving native lifetimes.
These owner-lifetime fixes add no further Wine fork changes. See WAYLAND_L2.md
for 32/64 verification and worktree paths; L2 remains separate from production.

L2 dd18609 adds a fork-only in-process uxtheme/bridge region contract. A borrowed
outer HRGN and a scoped mask-update flag preserve automatic theme-region ownership
while wexhost excludes the native client. The HRGN is read only on the owning UI
thread; neither property is serialized to the driver/compositor. Existing explicit
application regions keep precedence. Owned code retains compatibility definitions
so master still builds at its production Wine pin. That pin does not gain this
runtime feature until the L2 topic is integrated. In 3fd4865, hosted minimized
frames retain normal content instead of uploading iconic-caption buffers; group
geometry ignores iconic dimensions, readiness can retry after a buffer-size
change, and activation clears the preceding epoch's focus observation. Restore
is explicit before requesting native focus. Compositor e7be9560 rejects focus
requests for minimized groups, preventing a delayed request from reopening them.
An active group's state is driven once through its Wine frame; wexhost's separate
control connection remains the fallback outside an active group. No new protocol
version, WEX_NATIVE_WINDOW field, Unix-call index or public Windows API is added.

The subsequent b971d96 synchronizes the v9 constraint XML. V9 adds a maximum-size
event to native observation, not native resize requests; the driver keeps binding
v8. Compositor 2e88585f includes maximum dimensions in shared size hints, reports
them only to v9 observers, and detects committed XDG constraint changes without
requiring a move/repaint. For wlroots 0.18's missing WM_NORMAL_HINTS notification,
XWayland uses the public XWM event hook and a per-view deferred notification after
wlroots parses the property; pending notifications are removed on destruction.
No X event is consumed or parsed independently, and existing window-size policy
is unchanged. Owned wexhost fills existing max_size fields, preserves state
transitions when coalescing geometry events, and wexshell applies independent
axis limits in WM_GETMINMAXINFO. See WAYLAND_L2.md for compatibility checks.

180d405 synchronizes the v10 private XML across Wine, wexhost and compositor.
Wine's driver still binds v8 and bridge observers bind v9; there is no new Wine
runtime behavior at this checkpoint. Compositor dd65ccef adds serial-specific
XDG hosted resize requests, constraint acceptance, configure acknowledgement,
native-buffer readiness and matching-frame publication. Public scene/buffer
APIs retain old root buffers until atomic handover; supersession and bounded
rollback retain the original geometry. Focus/capture loss starts rollback;
failed rollback restores ordinary native composition. Snapshot listeners and
references are released on either window's destruction. XWayland resize requests
are explicitly unavailable, pending a separate native completion path. The
production pins, WEX ABI, adoption gate and sizing affordances remain unchanged.
The following synchronization merges Gemini's production 71572805 into the L2
topic as c6dac92; it adds no further Wayland behavior. Master keeps the incoming
prefix-split S0/S1 merge and its new pin. Those installer/session/stock changes
are synchronized from their owner rather than edited by this L2 work.
Compositor e8e5a083 requires a content-bearing native commit after acknowledgement
(buffer, damage, viewport, scale or transform); committing only acknowledged xdg
state cannot incorrectly report native readiness. A deterministic before/after
probe covers that separation. b41b65f fixes shared-header dependencies in the
compositor test-client build so window-layout changes rebuild every client.

Wine ac4a1b3 fixes an observed win_data_mutex/USER lock inversion during themed
frame creation/readoption. The private Wayland handle and association callbacks
update scalar state, unlock and post an internal WM_WAYLAND_WEX_UPDATE; the
owning UI thread refreshes current styles/z-order/hosting. The new message is
appended without changing existing message numbers and carries no surface pointer
or stale geometry. Focus/group notifications also post after releasing window
data. Epoch guards and private protocol versions remain unchanged. A CSD/SSD
stress probe fails before the fix, passes 100 cycles in each Explorer architecture
afterward. Compositor 9e4e3d4a only increases the test fixture's stable record
capacity for long stress runs; it adds no runtime compositor behavior.

The L2 move checkpoint adds fork-only v8 frame pointer capture and its Wine
SetCapture hook, guarded by the current focus epoch and active HOST state.
Hosted Move uses Wine's loop only with v8; zone placement echoes are ignored
for these proxies because native observation owns reconciliation. Generic
Wine fixes retain keyboard/menu move loops before a button press, terminate
them on capture loss, and commit pure zone moves without waiting for repaint.
Those generic fixes can be extracted for upstream review; no MR is opened.
The compositor routes pointer warps through normal hit testing with xdg surface
geometry offsets. Explorer activates task-menu Move/Size targets and separates
requested geometry from older observations. Production 2e8bcfa carries only
the shared property definitions needed to compile the existing owned bridge;
the L2 topic merges that header fix. Wine's consumption of resize negotiation
remains pending; the compositor's XDG transaction is implemented separately.
Display Properties, wireless UI and media player replace stock stub modules
through explicit assembly entries, with pristine originals preserved.
Those implementations are project modules rather than source patches in
the retained fork. VM sync resolves the gitlink separately from owned source.

P3b project modules now include wexres (shared resources), themeui (shared theme
engine, used directly by desk.cpl) and msgina (Turn Off/Log Off, called directly
by logonui), plus stobject's registry-driven COM volume/power tray service.
Netshell now lives under src/dlls, owns network tray/status/properties/backend,
and exposes the shared UI to ncpa.cpl. Wireless selection lives in wzcdlg and
is loaded through a typed export by the shell/netshell. Wine's boot DLL registration list includes
stobject and netshell; the shell enumerates
ShellServiceObjectDelayLoad and tolerates missing optional DLLs/services.
Their implementation and interfaces live under src/. Configure
and header installation entries are retained on wineexplorer/11.19 through
d0a0af0681. Search now lives in optional wexsearch with an opaque UI interface;
its class/resource ownership, file search, cancellation and absent-module
checks pass. Browser windows now live in optional wexbrowse behind one
string/flag entry point; taskbar toolbars remain core. See refactor/P3B.md
for extraction acceptance. The complete repository/build/install acceptance
is in refactor/P4.md; P5 assessment keeps unsupported extension replacements
as X commits. The separately pushed P6 candidate passes full build/runtime
smoke without changing the production pin (REBASE.md).

## Elevation boundary: administrator identity (C1, 2026-10-22)

Decision S3 of `ELEVATION_SYSTEM_ROLE_WORKORDER.md`. Applies only while `HKLM\Software\Wine\Elevation\Helper` is
configured (`elevation_enforced()`); with the helper unset nothing changes. Wine commit (working branch
`ccr-work`): `f180417289b`.

- `server/registry.c` (`user_is_administrator`, `admin_group_name`): the server's user is an administrator when the
  group database says so (`getpwuid(getuid())`, `getgrouplist`) for the group `sudo`, or the group named by
  `admin_group=` in `/etc/wineexplorer/sysrun.conf`. That file is read only if it is a regular, root-owned file that
  is not group- or other-writable (checked on the open descriptor, `O_NOFOLLOW`); otherwise `sudo`. The environment
  is never consulted: the user controls it and could name a group they are in. Cached for the server's lifetime, so a
  membership change applies at the next logon.
- `server/process.c` (`create_process`), `server/token.c` (`token_make_standard`): the first token of a session
  made while enforced is filtered as before; for a user outside the group it also loses the Administrators group
  and becomes `TokenElevationTypeDefault` (Medium integrity and the filtered privileges are the filtered token's).
  `standard_user_session` is then set for the server's life.
- `server/token.c` (`create_linked_token`): fails with `STATUS_NO_SUCH_LOGON_SESSION` once `standard_user_session`
  is set (provisional: the native error of `GetTokenInformation(TokenLinkedToken)` on a token without a split is not
  documented; Microsoft only says `TokenElevationTypeDefault` means no linked token).
- `server/process.c` (`grant_process_admin_token`): `STATUS_ACCESS_DENIED` once `standard_user_session` is set.
- The `wex-sysrun` launcher (`tools/wex-sysrun`) ignores the `admin_group` key instead of refusing the file.
- Not done here: the local Administrators group view of netapi32 (`NetLocalGroupGetMembers`, a stub that reports the
  current user), see the work order's "Results, C1".

## Elevation boundary: system role server and launcher wiring (C2, 2026-10-22)

Constraints a to d and f of `ELEVATION_SYSTEM_ROLE_WORKORDER.md`. Active only when the root-owned, not
group/other-writable `/etc/wineexplorer/sysrun.conf` (regular file, checked on the open descriptor, `O_NOFOLLOW`)
names the machine root, and, for the server, only in a process that the setgid launcher started (real gid differs
from the saved gid, effective gid equals the saved gid, and the saved gid is the configured group). Otherwise nothing
changes. The launcher is installed by default (since 2026-10-23; `WEX_SYSRUN_ENABLE=0` opts out). Wine commits (branch `ccr-work`): `0e98e935c36`,
`71fe07ea021`.

- `server/sysrun.c`, `server/sysrun.h` (new), `server/Makefile.in`, `server/main.c` (`sysrun_init`): the server drops
  its effective gid to the real one at once and keeps the system group as saved gid, and sets the process
  non-dumpable (`PR_SET_DUMPABLE 0`) after every `setegid`, so that `fs.suid_dumpable` cannot make it dumpable.
  `sysrun_raise` and `sysrun_drop` raise and drop the effective group.
- `server/process.c`, `server/process.h` (`create_process`): `SO_PEERCRED` of the connection (pid, uid, gid) is
  recorded per process when the system role is active. The `unix_pid` the client sends is not used for this.
- `server/fd.c`: every open, create, mkdir, rename, link and unlink that the server does for a client goes through
  `sysrun_open`, `sysrun_mkdir`, `sysrun_rename`, `sysrun_link` and `sysrun_unlink`, which raise the effective group
  only when the requesting client has the system group (its connection credentials: uid equal to the server's and
  gid equal to the system group). The unlink or rmdir of delete-on-close and disposition runs raised only for a file
  that such a client opened (`closed_fd::sysrun`, inherited by `dup_fd_object`). Everything else, including `ftruncate`
  and `fchmod` on an already opened descriptor, runs with the user's group.
- `server/registry.c`: the machine hives (`system.reg`, `userdef.reg`) come from the fixed machine root of the
  configuration, not from the prefix: `O_NOFOLLOW` on the open, `fstat` must show a regular file of root and the
  system group that others cannot write, else the server refuses to start. They are saved with the raised group
  (`faccessat(AT_EACCESS)` only there) into the existing file, in place (`save_hive_in_place`; a new file would belong
  to the user's uid); a symlink at the hive is never followed on save; a missing file is not created.
  The group is raised for the save only if elevation was enforced by the machine hive loaded at start
  (`machine_hive_saved`, not the live helper value, which a user could set in memory); otherwise the hive keeps the
  stock rule (saved only if the real user can write it), so with the launcher but no helper nothing persists.
- `dlls/ntdll/unix/virtual.c` (`kernel_writewatch_init`): a failed open of `/proc/self/pagemap` with `EACCES` or
  `EPERM` in a non-dumpable process (`PR_GET_DUMPABLE` is 0: the system group of the launcher) logs at WARN, not ERR;
  the fallback is unchanged. `/proc/self/*` of a non-dumpable process belongs to root.
- `server/registry.c` (`key_in_user_tree`): while elevation is enforced and the system role is active, only the
  subtree of the token user's own SID and its `_Classes` count as the user's tree. `HKU\S-1-5-18`, `-19`, `-20` are
  created at start and, like `.DEFAULT`, get the machine default descriptor.
- `dlls/ntdll/unix/sysrun.c` (new), `dlls/ntdll/Makefile.in`, `unix_private.h`: reads the configuration, spawns the
  launcher, and checks a server for a client whose own effective gid is the system group
  (`sysrun_client_is_system`, `sysrun_server_ok`). Such a client refuses a server whose saved gid (from
  `/proc/<pid>/status`) is not the system group, whose ids are not the user's, or whose connection uid differs.
  The image path (`/proc/<pid>/exe`) cannot be compared: it is not readable for the non-dumpable server.
- `dlls/ntdll/unix/server.c` (`server_connect`): the check above; `dlls/ntdll/unix/loader.c` (`start_server`): with the
  configuration and the launcher the server starts through `wex-sysrun wineserver`; if the launcher refuses (exit
  77) a warning is printed and the stock start follows.
- `dlls/ntdll/unix/env.c` (`run_wineboot`): with the launcher a `wex-sysrun wineboot-init` runs first (machine root
  prefix, system group, connected to the user's server by the launcher), then the normal wineboot still does the
  user's part; a refusal prints a warning and only the normal one runs. A client with the system group does not start
  wineboot itself. `programs/wineboot/wineboot.c` (`start_services_process`): does not start `services.exe` again when
  the `SVCCTL_STARTED_EVENT` is already signaled.

## Elevation boundary: the system role, SYSTEM and service tokens (C3, 2026-10-22)

Phases C and D and constraint g of `ELEVATION_SYSTEM_ROLE_WORKORDER.md`. Everything below is active only while the
system role is active (the server was started by the setgid launcher, `sysrun_active`) **and** elevation is enforced
(`HKLM\Software\Wine\Elevation\Helper` set); otherwise stock behavior. Exception: `programs/services` also checks
whether its own token is SYSTEM, which only the system role gives it.
Wine commits (branch `ccr-work`): `edf575a2260`, `9ab7dee645e`, `bd22d5ee0d9`.

- `server/token.c`, `server/security.h` (`token_create_service`, `token_service_account`): primary tokens of
  LocalSystem (S-1-5-18: groups Everyone, Users, SERVICE, CONSOLE LOGON S-1-2-1, Authenticated Users, This
  Organization S-1-5-15, LOCAL S-1-2-0, Administrators (enabled, owner and primary group, not mandatory)),
  LocalService (S-1-5-19) and NetworkService (S-1-5-20: the same without Administrators; the account owns what it
  creates), System integrity (S-1-16-16384), `TokenElevationTypeDefault`, no
  linked token. Default DACL: SYSTEM and Administrators for LocalSystem, SYSTEM and the account for the others. The
  privilege and group lists are the native ones measured in docs/ELEVATION_LOCAL_VALIDATION_WORKORDER.md; SeImpersonate
  stays enabled on the service accounts (documented default) and CONSOLE LOGON is provisional (possible task
  artifact); per-task SIDs and the unknown S-1-5-32-* SIDs are not copied (docs/TOKEN_SECURITY_REFERENCES.md).
- `include/winternl.h`, `dlls/advapi32/security.c`, `server/token.c`: privileges 31 to 36 are known (names and LUIDs
  of SeIncreaseWorkingSet 33, SeTimeZone 34, SeCreateSymbolicLink 35, SeDelegateSessionUserImpersonate 36;
  `SE_MAX_WELL_KNOWN_PRIVILEGE` is 36; 31 and 32 have no name), so LookupPrivilegeNameW and LookupPrivilegeValueW
  resolve them. `token_make_filtered` adds SeIncreaseWorkingSet and SeTimeZone (disabled), as the native filtered
  token has. `LookupPrivilegeNameW` fails with ERROR_NO_SUCH_PRIVILEGE for the unnamed LUIDs 31 and 32 instead of
  dereferencing a NULL name. `token_create_admin` is unchanged.
- `server/token.c` (`token_duplicate`): a token whose owner is its user keeps that owner in a copy.
- `server/protocol.def`, `server/token.c` (`create_service_token`), `include/winternl.h`,
  `dlls/ntdll/unix/process.c` (`ProcessWineCreateServiceToken` = 1003, input account number 0 to 2, output the
  token handle): creates such a primary token. Refused with `STATUS_PRIVILEGE_NOT_HELD` without an enabled
  SeTcbPrivilege, with `STATUS_ACCESS_DENIED` for a caller that is not the SYSTEM process of the system role (Wine's
  administrator token holds SeTcbPrivilege too), `STATUS_NOT_SUPPORTED` without the system role.
- `server/token.c` (`create_token`): while elevation is enforced, `NtCreateToken` needs an enabled
  SeCreateTokenPrivilege, which no Wine token has (native: the same privilege, held only by the LSA). Without this
  check a limited process could make an administrator token, impersonate it, and pass every access check.
- `server/sysrun.c`, `server/process.c`, `server/process.h`, `server/thread.c`, `server/mapping.c`
  (`sysrun_process_created`, `sysrun_thread_created`, `sysrun_main_image`): a root process whose connection has the
  system group is protected from the start (process and thread descriptor as below, so a limited
  or standard token cannot open it with any dangerous right) and gets the SYSTEM token when its main image is verified: the
  mapped file's device and inode (`fstat` of the server's descriptor) must equal `services.exe`, `wineboot.exe` or
  `winedevice.exe` of the machine root's `system32` (or `wineboot.exe` and `winedevice.exe` of `syswow64`). The NT
  path is never used, it goes through the user's prefix links. Any other image: its raised file access ends at once and
  the process is killed from the main loop (a timer, 10 ms later: killing it inside its own request crashed the server). A child gets its parent's token or the one it is given and a
  descriptor from it when that token is SYSTEM, LocalService or NetworkService. The connection credentials of a
  child are those of the process that made the socket pair, so the server clears `peer_gid` for a child with a
  service-account token: it never gets the system group's file access.
- `server/token.c` (`set_token_integrity_level`): while elevation is enforced, raising a token's integrity level
  needs an enabled SeTcbPrivilege in the caller's process token (`STATUS_PRIVILEGE_NOT_HELD`), lowering does not (as
  on Windows; measured natively, docs/TOKEN_SECURITY_REFERENCES.md).
- `server/sysrun.c` (`protect_object`): the descriptor of a system role process or thread is SYSTEM (and the service
  account) full control, Administrators query + read the descriptor, Users and Authenticated Users the limited query +
  SYNCHRONIZE (thread: `THREAD_QUERY_LIMITED_INFORMATION`), as Windows does for service processes. Full access needs
  an enabled SeDebugPrivilege.
- `server/token.c` (`check_object_access`): an enabled SeDebugPrivilege opens any process or thread whatever its
  descriptor says (Windows does the same), while the system role is active. A filtered or standard token has no
  SeDebugPrivilege; an elevated administrator token has it disabled until the program enables it.
- `server/sysrun.c` (`sysrun_check_process_access`), `server/hook.c` (`set_hook` with a thread or process),
  `server/queue.c` (`attach_thread_input`), `server/console.c` (`AttachConsole`): the requests that name another
  process or thread by id instead of by handle are checked against its descriptor.
- `server/registry.c` (`boot_state_paths`): `Control\ServiceCurrent` and `Control\ComputerName\ActiveComputerName` are
  Users-writable only while the system role is **not** active (no launcher: services.exe and `wineboot --init` run
  limited); with it they are written as SYSTEM and the grant is gone.
- `dlls/ntdll/unix/sysrun.c`, `process.c`, `loader.c`, `unix_private.h` (`sysrun_must_drop_group`,
  `sysrun_prepare_child`, `sysrun_early_init`): a client with the system group that starts a process with a token
  other than SYSTEM (or none) drops the group in the forked child (`setresgid(gid, gid, gid)`, the child cannot get
  it back) before the loader is executed. The child marks itself not dumpable first thing (`WINESERVICENODUMP`), and
  so do its children, so the user cannot ptrace it. Shortcut: a short window between the exec and the flag.
- `programs/services/services.c` (`has_system_role`, `get_service_token`, `service_start_process`,
  `process_create`): with the SYSTEM token services.exe starts every service with `CreateProcessAsUserW` and the token
  its `ObjectName` names (empty or `LocalSystem`; `NT AUTHORITY\LocalService`; `NT AUTHORITY\NetworkService`).
  Any other name fails with `ERROR_SERVICE_LOGON_FAILED` for now (a user account needs a logon). The control pipe of a
  service has a descriptor for SYSTEM and the account. services.exe sets its own token's default DACL to
  SYSTEM, Administrators and Authenticated Users (read, write, execute): objects with no descriptor (the
  `\pipe\svcctl` endpoint, `__wine_SvcctlStarted`) are created from it. Without the system role nothing changes.
- `programs/services/rpc.c`, `services.c`, `services.h` (`load_service_security`, `check_client_access`,
  `svcctl_QueryServiceObjectSecurity`, `svcctl_SetServiceObjectSecurity`): the descriptor of a service is the
  binary `Security` value of its registry key, else the native default; `SetServiceObjectSecurity` needs `WRITE_DAC`
  on the service handle, takes the DACL only (shortcut) and writes the value; `QueryServiceObjectSecurity` returns
  owner, group and DACL (it was `ERROR_CALL_NOT_IMPLEMENTED`). `dlls/sechost/service.c`
  (`QueryServiceObjectSecurity`, `SetServiceObjectSecurity`) call these over RPC; they were a semi-stub (an empty
  DACL) and `return TRUE`. **Ungated**: this is the documented behavior, for every services.exe, with or without the system role or enforcement.

## In place of stock behavior (deliberate)
| Area | Change | Gate |
|---|---|---|
| `dlls/winewayland.drv` xx-zones-v1 | optional shared per-socket coordinate zone; volatile registry token supports independently launched processes; map/move positioning and feedback with surface scaling and tracked xdg geometry offset | only when compositor advertises xx_zone_manager_v1; upstream/winewayland-zones aed8b9599c + 01607e166c; retained 853c5b02c7 + 1bab2dda87 on codex/wayland-integration; seven placements match X11 exactly at 100% and within 1 px at 150% |
| `dlls/winewayland.drv`; `include/wexwayland.h` | privileged layers, shared-handle cross-process relative stacking, native activation hook and minimized restore; generic scalar role/edge/size properties, no class checks | optional wex_window_manager_v1; ce019d67c7 + header correction 996f87f33f; activation/configure a83412b0ed; maximized work-area borders e3be0174be; shared atoms 97eb62958c; focus acknowledgement 89735ec1ef; background restore/logical stacking 624720e2b6; native focus loss with stale-event guard ea0654efdb; v1 same-connection fallback; v2 compositor 9a47d74 passes 26/26 |
| `dlls/win32u/message.c` | skip implicit click activation of a WS_EX_NOACTIVATE root | generic Win32 style fix 8f1717c8f3; verified private Wayland and X11; upstream baseline incorrectly activated it |
| `programs/explorer/desktop.c`, `explorer.c` | /shell without /desktop starts the plugin on the driver's desktop and skips display mode changes | explicit /shell only; d7d46937a3 included in accepted 05286f7c90; native Wayland shell passes |
| `programs/explorer/desktop.c`, `explorer.c`; `include/wexshell.h` | load the versioned wexshell.dll plugin for the Windows-style shell and route its browser/applet commands; implementation lives in project modules under `src/dlls` (wexshell, wexres, themeui, msgina), while the stock host retains desktop creation, clipboard, COM registration and its message loop | shell startup only when `EnableShell=1` (`HKCU\Software\Wine\Explorer\Desktops\<name>`), named `shell`, or requested with `/shell`; missing/incompatible plugin uses stock behavior |
| `programs/explorer/desktop.c` | `/shell` switch (before the program to run) enables the shell for that desktop; used by the Winlogon-style session script | only when given |
| `programs/explorer/explorer.c` | `/wlan`, `/netstatus`, `/netprops` helper switches; `::{CLSID}` paths and `/e` accepted | token-based, only when given as arguments |
| `dlls/desk.cpl` | theme/background/appearance/settings pages added in front of Wine's Settings page; installed theme discovery, isolated style previews, direct shared themeui Apply, host-supported modes and keep/revert confirmation (see DISPLAY.md); themeui now persists Classic colors before cross-process reload | always |
| `dlls/uxtheme` (`msstyles.c`, `msstyles_v4.c`, `draw.c`, `property.c`, `msstyles.h`) | Binary Vista-format VARIANT parsing, typed properties, WIC PNG decoding, DPI variants and atlas regions; ApplyTheme reads typed SysMetrics | always; reviewed fork through f0108dc; standard-only four-commit upstream candidate c16f0bda39..43c139255b, including fixes; full MIT notice, no unused generated tables; source-built tests pass both architectures |
| `dlls/uxtheme/system.c` | MUI/filename documentation and fabricated scheme display-name fallbacks for installed styles lacking localized strings | fork-only convenience behavior; excluded from the cleaned upstream series and its assertions |
| `dlls/uxtheme` (`system.c`, `msstyles.c`, `metric.c`) | implement private ordinal 16 `OpenThemeDataFromFile`; metrics follow the supplied theme handle; activating a preview applies metrics even when its classes are already parsed | always |
| `dlls/uxtheme/msstyles.c` | convert non-client theme font heights from points to 96-DPI pixels; trim token whitespace before a comma (`MSSTYLES_GetNextToken`) | always; token trimming is an independent upstream candidate |
| `dlls/uxtheme/system.c`, `uxthemedll.h` | watch ThemeManager registry changes and reload the active theme in other processes without reapplying global metrics | theme queries/opening theme data |
| `dlls/winex11.drv/window.c` | virtual desktop resize does not wait for fullscreen hint acknowledgements; standalone shell sessions have no host window manager to supply them | virtual desktop root window only |
| `programs/wmplayer` | Wine's stub `main.c` replaced by ReactOS mplay32 | always |
| `dlls/shdoclc` | icon 190 (Wine's `iexplore.ico`): `iexplore.inf` names it as the Internet Explorer desktop icon, stock Wine has none | always |
| `programs/calc`, `programs/mspaint` | new programs (ReactOS calc; own mspaint) | always |
| `dlls/ntdll/unix/system.c` | `SystemPowerCapabilities.SystemBatteriesPresent` uses Wine's existing `fill_battery_state` helper with zero-initialized state; `SystemBatteryState` and its sysfs implementation are already stock in wine-11.19 | always (replaces the hard-coded FALSE capability; no-battery state remains FALSE) |
| `server/fd.c` | recompute the DST-adjusted timezone bias when DST starts/ends (a long-running session never saw it before) | always (bug fix, upstreamable) |
| `dlls/ntdll/unix/system.c` | drop the cached zone info when `/etc/localtime` changes | always (bug fix) |
| `dlls/kernelbase/locale.c` | preserve a locale selected through Regional and Language Options while the remembered Unix locale remains unchanged; a changed Unix locale becomes the new default | always; upstream suitability requires review in P0 |
| `dlls/user32` (`cursoricon.c`, `user_private.h`, `sysparams.c`, `defwnd.c`) | load system cursor schemes from `HKCU\Control Panel\Cursors` and cache the file-backed cursors, falling back to built-ins; `reset_scheme_cursors()` flushes cached cursor handles on `SPI_SETCURSORS` and `WM_SETTINGCHANGE` for live scheme updates | system cursor resource loads; cache/refresh semantics require review in P0 |
| `dlls/win32u` (`window.c`, `input.c`, `defwnd.c`), `dlls/user32/misc.c` | `RegisterShellHookWindow`/`DeregisterShellHookWindow` implemented: the desktop's taskman window gets the registered `SHELLHOOK` message for window created/destroyed/activated/redraw/flash | always (no cost without a registered window except one server call per event) |
| `dlls/mmdevapi` (`audiovolume.c`, `unixlib.h`), `dlls/winepulse.drv` | `IAudioEndpointVolume`: master volume and mute of the default device through the driver (PulseAudio/PipeWire); two new unix calls `get/set_endpoint_volume`; other drivers (alsa, oss, coreaudio) return E_NOTIMPL; the wow64 table of winepulse has stubs, so 32-bit callers get E_NOTIMPL for now | always |
| `dlls/winex11.drv/window.c` (`sync_window_position`), `event.c` (`set_input_focus`) | in a virtual desktop, a window that is not the top one is restacked in X directly below the window it follows in the Win32 z-order (stock only ever raises, for the sake of window managers), and focusing a window no longer raises it in X; without it an activated window covers topmost ones such as the taskbar | only in a virtual desktop, unmanaged windows |
| `dlls/winex11.drv/event.c` (`X11DRV_ActivateWindow`) | in a virtual desktop the window that becomes active is raised to the top of the Win32 z-order, as Windows does on activation (Wine leaves it to the X window manager, and the row above removed the X raise); the X stacking follows, below topmost windows. Before, clicking a window or its taskbar button activated it without bringing it to the front | only in a virtual desktop, unmanaged windows |
| `dlls/win32u/sysparams.c` (`SPI_SETWORKAREA`/`SPI_GETWORKAREA`) | the work area is the monitor's and every process sees it, like on Windows and ReactOS (stock kept it per process, so windows maximized in other processes covered the taskbar): it is written to the monitor's registry key, the monitor serial is bumped so other processes reload, and a volatile copy in `HKLM\Software\Wine\WorkArea` (by monitor path) is applied again whenever the monitors are rebuilt from the driver, which every process entering a virtual desktop does; it is dropped once it no longer fits the monitor. `SPI_GETWORKAREA` reads the primary monitor every time. As on Windows, the rectangle has to lie on one monitor, and a non-zero uiParam (the taskbar passes TRUE) fits the maximized windows of that monitor to the new work area | always (only matters once something sets a work area) |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`, `shell32.rgs`) | Windows XP names in the namespace: string 8964 "Recycle Bin" (was "Trash", with the delete prompts worded like XP), new string 9227 "My Documents" as the `LocalizedString` of CLSID_MyDocuments (string 46 stays the folder name "Documents") | always |
| `dlls/shell32/shell32.rc` | icon 32 (`IDI_SHELL_EMPTY_RECYCLE_BIN`, Wine's own `trash_file.ico`): stock Wine defines the id and returns it for `SIID_RECYCLER`, but has no such icon, so an empty Recycle Bin was drawn blank | always (bug fix, upstreamable) |
| `dlls/shell32/iconcache.c`, `shellord.c` (`FileIconInit`) | shell32's own icons can be replaced through `HKLM\...\Explorer\Shell Icons` like on Windows and ReactOS (stock Wine only did it for overlays): value `n` replaces icon index n, an icon asked for by resource id stands for index id - 1 for the first 54 icons (no gaps there), and beyond that a value named after the negative id (`-137`) is used, which Windows has no use for. A replacement that cannot be loaded falls back to shell32's icon. WineExplorer addition: the icons of a module in the system directory (Control Panel applets such as `desk.cpl`) are taken from the file named by the value of that module's name (no extension) in `HKCU\Software\WineExplorer\Module Icons`, with the module's own icon ids; other modules are never replaced. `FileIconInit(TRUE)` (was a stub) reloads the cached icons of shell32 and of system modules in place so a theme change shows without new image indexes. Explorer writes both keys from the theme's icon pack (`src/dlls/wexres/shellres.c`) | always (only matters when Shell Icons or Module Icons has values) |
| `server/winstation.c` (`remove_desktop_user`), `include/ntuser.h` | new Wine desktop flag `DF_WINE_SHELL_DESKTOP`: a desktop with it is not timed out when only its owner is left (stock wineserver unlinks the desktop's name and posts `WM_CLOSE` then; with a shell that happened whenever the shell was alone and one of its threads ended, and new programs could no longer open the desktop). Explorer sets it in shell mode | only on desktops that have the flag |
| `dlls/shell32/folders.c` (`IExtractIconW::GetIconLocation`) | a shortcut without an icon of its own shows the icon of its target, as on Windows and ReactOS (stock Wine showed the blank file icon, e.g. for the Start menu shortcuts in search results); shortcut targets use the default fallback to prevent recursive icon lookup through cycles | always (bug fix, upstreamable) |
| `dlls/comctl32/propsheet.c` (`PROPSHEET_SetWizButtons`) | when the focused wizard button ends up disabled or hidden (Next swapped for Finish), the focus moves to the default button; before, nothing had the focus on a wizard's last page and Enter did nothing | always (bug fix, upstreamable) |
| `dlls/comctl32/imagelist.c` (`add_dib_bits`) | an image without alpha clears the item's alpha flag; before, replacing a 32-bit image with one without alpha (`ImageList_ReplaceIcon`) kept the flag and the new image drew fully transparent | always (bug fix, upstreamable) |
| `dlls/shell32/shellord.c` (`SHFindFiles`) | opens the Search Results folder (`explorer.exe ::{E17D4FC0-...}`, whose window shows the Search Companion) like Windows and ReactOS, honoring `REST_NOFIND`; was a stub returning FALSE. The folder to look in and saved searches are not passed yet | always |
| `dlls/user32/user32.rc` (`SYSMENU`) | the window menu ends with Close, as on Windows: Wine's "About Wine" item and its separator are removed (the `SC_ABOUTWINE` handler stays, unused) | always |
| `dlls/shell32` (`shlview_cmenu.c`, `new_menu.c`, `shlfileop.c`, `shlview.c`, `shell32.rc`) | Windows XP folder background menu (View, Arrange Icons By, Refresh, Paste, Paste Shortcut, New, Properties); New lists Folder and the ShellNew file types and starts renaming the new item; `FOF_RENAMEONCOLLISION` names copies "Copy of X"; Create Shortcut / Paste Shortcut implemented; items added by path notifications are read again for sizes and dates; notifications for items outside the folder (they are recursive) are ignored and a move in or out shows as an add or a removal; the background menu takes the "pastelink" verb | always |
| `dlls/shell32` (`shlfolder.c` `SHELL32_BindToChild`, `folders.c`) | folder shortcuts work as on Windows: a folder whose desktop.ini names `CLSID_FolderShortcut` opens the folder its `target.lnk` points to; a desktop.ini `IconFile` is used (it was read and dropped) | always |
| `dlls/shell32` (`shfldr_fs.c` display names, `shlview_cmenu.c` Send To) | a folder is shown by its desktop.ini `LocalizedResourceName` (not for parsing or editing), as Windows shows "Documents" by another name; the item menu has Send To (Desktop (create shortcut), My Documents, SendTo-folder shortcuts, removable drives) | always |
| `dlls/shell32` (`brsfolder.c`) | SHBrowseForFolder returns the target of a folder shortcut unless `BIF_NOTRANSLATETARGETS`, as Windows does | always |
| `dlls/shell32` (`shlview.c` keys, `shlfileop.c`, `shell32.rc` confirmations) | Ctrl+A, Ctrl+C, Ctrl+X and Ctrl+V in the shell view; the delete confirmations use the XP captions (Confirm File Delete, Confirm Folder Delete, Confirm Multiple File Delete, Confirm File Replace) and name the item by its display name | always |
| `dlls/shell32` (`shfldr_mycomp.c`, `shell32.rc`) | drives without a label are named "Local Disk (C:)", "CD Drive (D:)" and so on, with that Type; columns "Total Size" and "Free Space"; `HideMyComputerIcons` (set for Control Panel in `wine.inf`) hides an item from the My Computer window but not from folder trees | always |
| `dlls/shell32` (`pidl.c`, `shell32_main.c`) | file system folders have the type "File Folder" (Windows wording) | always |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`) | string 21785 "Shared Documents" (the Windows XP id), the LocalizedResourceName Explorer gives Public\Documents | always |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`) | strings 0x7143-0x7145 "Shared Music", "Shared Video", "Shared Pictures" (XP ids), the names Explorer gives Public Music, Videos and Pictures | always |
| `dlls/shell32` (`undo.c`, new; `shlfileop.c`, `shfldr_fs.c`, `recyclebin.c`, `shlview.c`, `shlview_cmenu.c`) | Undo as on XP: SHFileOperation with FOF_ALLOWUNDO and renames in the view record their items; the background menu shows "Undo Rename/Move/Copy/Delete" (verb "undo", Ctrl+Z in the view); undoing a delete restores the item from the trash. The view finds notified items by name, resets edited labels and appends notified items at the end | always |
| `dlls/comctl32` (`listview.c`, also built as comctl32_v6) | LV_VIEW_TILE (v6 only): LVM_SET/GETTILEVIEWINFO (tile size, lines), LVM_SET/GETTILEINFO (per item columns); a tile has the normal icon at the left and the name with its detail lines (gray) next to it, laid out like the small icon view | always |
| `dlls/comctl32` (`listview.c`) | groups (v6) in the icon, small icon and tile views: LVM_ENABLEGROUPVIEW, LVM_INSERTGROUP, LVM_REMOVE(ALL)GROUP(S), LVM_HASGROUP, LVM_SET/GETGROUPINFO, LVM_MOVEITEMTOGROUP, LVIF_GROUPID; each group is laid out in rows under an XP header (blue bold name over a fading line). Not in the details view | always |
| `dlls/comctl32` (`listview.c`) | type-ahead: a space continues a search in progress ("my p" finds My Pictures) | always |
| `dlls/comctl32` (`listview.c`) | LVM_SETBKIMAGE offsets: the image stays at its place in the window (Explorer's folder watermarks in the bottom right corner) and the list draws it again when it scrolls or is resized | always |
| `dlls/comctl32` (`listview.c`) | a selected item keeps its highlight box when the text background is CLR_NONE, as on Windows | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`, `shell32.rgs`, `shfldr_fs.c`, `shfldr_mycomp.c`, `shellpath.c`, `shell32_classes.idl`) | My Computer lists XP's Files Stored on This Computer (Shared Documents, a new namespace folder with XP's CLSID {59031A47-...}, and My Documents named "<user>'s Documents"), then its drives in XP's groups: Hard Disk Drives, Devices with Removable Storage, Network Drives, Other; drives and folders have no detail lines in Tiles | always |
| `dlls/shell32` (`shlview_cmenu.c`, `shell32.rc`), `dlls/comctl32` (`comctl32.rc`) | file Properties as XP: "test Properties" title (comctl32's PSH_PROPTITLE caption "%s Properties"), Type of file, Opens with (registered program name) and Change... (Open With, always), Size and Size on disk with exact bytes, XP date format, Read-only; the page keeps its icons | always |
| `dlls/shell32` (`shfldr_fs.c`, `changenotify.c`) | drag and drop: a folder item is its own drop target (files take none); Drop moves, copies or makes shortcuts (CF_HDROP from the shell or other programs) with XP's default effect (move on the same drive, else copy; Ctrl copies, Shift moves, Ctrl+Shift links), undoable; change notifications of a rename or move reach the watchers of either item's folder | always |
| `dlls/shell32` (`shlview.c`) | the view watches its folder on disk (FindFirstChangeNotification) and takes the folder's items again when other programs change it: new items at the end, gone ones removed, the selection kept | always |
| `dlls/shell32` (`new_menu.c`), `loader/wine.inf.in` | New > Shortcut: ShellNew Command handlers (HKCR\.lnk\ShellNew "rundll32.exe appwiz.cpl,NewLinkHere %1") come after Folder, before the separator | always |
| `dlls/appwiz.cpl` (`createlink.c`, new; `appwiz.rc`, `appwiz.cpl.spec`, `shortcut.bmp`) | NewLinkHere(A/W): Windows XP's Create Shortcut wizard (location with Browse..., then the name; .lnk for items, .url for Internet addresses); the picture is XP's from the resource pack, else ReactOS' | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`) | View > Tiles: 48 pixel icons (SHIL_EXTRALARGE) with the type and size of files (none for folders) under the name; icons and tiles are laid out in rows (no LVS_ALIGNLEFT), as on XP | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`) | View > Thumbnails: 96 pixel framed pictures of images (GDI+), the 48 pixel icon for other items, folders as XP's large folder (the Shell Icons folder icon) holding up to four of their pictures; the list is laid out again after it is filled; a selected thumbnail keeps its picture and gets XP's thick frame of the selection color | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`) | View > Filmstrip (FVM_THUMBSTRIP) in picture folders: the selected picture large above one row of thumbnails, with previous/next and rotate buttons; rotating saves the picture in its own format (GDI+) | always |
| `dlls/shell32` (`openwith.c`, new; `dialogs.c`, `shlexec.c` stubs removed) | Open With dialog (`SHOpenWithDialog`, `OpenAs_RunDLL`), ReactOS-based: programs from `HKCR\Applications` (FriendlyAppName or version FileDescription), recommended from the type's open command, OpenWithList and OpenWithProgids; "Always use" sets the type's default. The item menu of one file has Open With ▸ (or Open With... when nothing else is recommended) | always |
| `loader/wine.inf.in` (file types) | `Folder\ShellNew`, Bitmap Image (`.bmp` → `Paint.Picture`, opened by mspaint) and Rich Text ShellNew entries; `HideFileExt=1` (no-clobber) as on Windows XP | prefix install/update |
| `programs/iexplore/iexplore.inf` | CLSID_Internet gets its display name "Internet Explorer" (Windows' default value); Wine had none | always |
| `loader/wine.inf.in` | file-type associations (.txt/.bat/.cmd/.log, folder open command); `HKCR\Applications` Notepad, Paint, WordPad and OpenWithList entries for .txt/.bmp/.rtf; the Windows icons of text, configuration, setup and batch files (shell32 -152, -151, -153) | always |
| `programs/control/control.c` | `printers` opens the Printers folder; no argument opens the Control Panel folder in Explorer like Windows (Wine's own Control Panel window only if that fails) | always |
| `programs/shell32`-visible: none | Run dialog placement is done by explorer (CBT hook), not by shell32 | - |
| `loader/wine.inf.in` | `.msc` files open with `mmc.exe` (`MSCFile`); new `[MscFiles]` copies `devmgmt.msc` (a data file of `programs/mmc`) into system32 | always |
| `dlls/ntdll` (`unixlib.h`, `loader.c`, `ntdll.spec`, `unix/unix_private.h`, `unix/loader.c`, `unix/file.c`) | `wine_create_symlink` internal unix call with target canonicalization (`realpath`) to support fake DLL symlinks; client-side copy-on-write symlink break (`check_cow_break_symlink`) in `NtCreateFile` gated on Wine DLL directories (`is_in_wine_dll_dirs`) resolving relative symlink targets against the link directory for data writes, truncate, overwrite, or supersede, and adding `S_IWGRP` to the replacement file mode when `S_IWUSR` is set on the target so POSIX default ACLs in shared machine roots retain group write access | internal call / writes to built-in DLL symlinks |
| `dlls/setupapi` (`fakedll.c`) | symlinked fake DLL generation replacing copied fake DLLs with symlinks to Wine's installation tree while preserving native DLLs and WinSxS assemblies; WOW64 path redirection in `get_real_link_dest()` | `WINELINKFAKEDLLS=1` env var or `HKLM\Software\Wine\LinkFakeDlls` (DWORD 1) |
| `server/fd.c` | leaf symlink preservation in `dup_fd_name` / `open_fd()`: ensures `fd->unix_name` retains the leaf symbolic link when opening a symlink, so closing with `FILE_DELETE_ON_CLOSE` unlinks the link rather than the package target | symlinks opened with delete-on-close |
| `loader/wine.inf.in`, `programs/wineboot/wineboot.c` | `[UserInstall]` section (`UserRegisterDlls` for `shell32.dll`, `UserRegistry` for 19 per-user `HKCU` settings, and `ThemeManager`) in `wine.inf.in`; when `C:\windows` is a reparse point (`FILE_ATTRIBUTE_REPARSE_POINT`) pointing to a shared machine root, `update_wineprefix()` runs only `[UserInstall]` and `update_user_profile()` so per-user `wineboot --init` / `wineboot -u` skips machine-level DLL creation and `HKLM` device installation | only when `C:\windows` has `FILE_ATTRIBUTE_REPARSE_POINT` |
| `server/registry.c` | Phase S3 shared machine hive handling: (1) `save_branch()` checks target write permissions (`EACCES`/`EROFS`), clearing dirty state, resetting `mod_seq` and freeing tombstones, returning success without modifying the file or replacing symlinks (Option a1 in-memory writes for limited users); (2) saves to temporary `reg<pid><count>.tmp` in target hive's directory, fsyncs, and executes atomic `rename()` over realpath target; (3) Administrator merge-on-save under `flock(LOCK_EX)` on `<target_dir>/.reg.lock` replaying session dirty keys/values (`mod_seq > 0`) and tombstones (`reg_tombstone` deleted nodes) onto on-disk hive base loaded into `tmp_root` without resurrecting deleted keys; (4) resets branch `mod_seq` and frees tombstones after each successful save so past mutations are not replayed indefinitely; (5) limits per-session filtering and merge strictly to branches loaded as symbolic links into the shared machine root (`info->is_shared_machine`), preserving stock Wine behavior for ordinary unlinked prefixes; (6) limits `OBJ_OPENLINK` in `create_key_recursive()` strictly to merge operations (`in_merge`) | shared machine registry hives (`system.reg`, `userdef.reg`) |

## D2 compositor effects and capture

- `dlls/winex11.drv/window.c`, `x11drv.h`: a private `_WEX_DROPSHADOW` CARDINAL on class-shadow windows carries
  `SPI_GETDROPSHADOW` to the compositor at creation/show. Explorer's out-of-context hook cannot reliably read another
  process's XID. The hint is only written when `_NET_WM_CM_S<screen>` has an owner; no input/shape/rendering changes
  without one. The shared internal `X11DRV_has_compositor` queries ownership on the actual display's default screen.
- `dlls/user32/win.c`: `AnimateWindow(AW_BLEND)` steps temporary `WS_EX_LAYERED` uniform alpha at roughly 16 ms
  intervals for `dwTime`, then restores the layered bit/attributes and the requested visibility. As allowed by the
  D2 spec this path runs without an X11 selection query in user32; intermediate alpha is visible only with a
  compositor (Wine's X11 driver only publishes opacity otherwise). Other animation flags retain the partial-stub
  behavior. Existing per-pixel layered windows fall back to immediate show/hide to preserve their bitmap; uniform
  alpha and color-key layered windows retain their attributes. Automatic popup menu fades are deferred: win32u's
  `show_popup` cannot call user32's `AnimateWindow`, and a shared fade/compositor gate would broaden the stock change.
- `dlls/winex11.drv/bitblt.c`: direct `X11DRV_StretchBlt` source copies and `X11DRV_GetImage` readback substitute the
  real X root for a virtual-desktop source drawable and add its root offset to the source DC rectangle. Gate:
  source equals Wine's `root_window`, that window differs from `DefaultRootWindow`, and `_NET_WM_CM_S<screen>` has
  an owner at read time. The DC is copied locally; destination rendering/GCs and ordinary window reads stay as before.
  `dlls/winex11.drv/xrender.c`: under the same gate, scaling/format conversions fall back to that core path (and GDI
  image readback if needed), avoiding a cached desktop picture that lacks redirected children. Ownership is queried
  per read, so already-open screen DCs recover immediately after composition is disabled.

## Backported from upstream Wine (drop when rebasing past them)

| Upstream | Change |
|---|---|
| MR !12231 (merged 2026-10-08) | `dlls/user32/lstr.c`: `CharToOemBuffW` wrote more bytes than asked for with a multi-byte OEM code page (UTF-8); untranslatable characters become `_` |
| MR !7746 (open upstream when taken) | `dlls/shell32/shfldr_fs.c`, `shfldr_desktop.c`: sizes of files of 4 GiB and more (the pidl keeps only 32 bits); checked in Explorer's Details view |

## Reverted (were changed earlier, now stock again)
- `dlls/comctl32/monthcal.c` (today-circle removal): timedate.cpl now creates its calendar with
  `MCS_NOTODAY | MCS_NOTODAYCIRCLE`, which is what the XP applet looks like.
- `include/commctrl.h`, `include/prsht.h` (`SNDMSG` redefinition).
- `dlls/ntdll/unix/sync.c` (`NtSetSystemTime` running `sudo date`): clock changes go through the applet's elevation
  prompt only.
- `dlls/shell32/dialogs.c` (Run dialog placement).

## Token Security, Elevation & Chromium Compatibility (gemini/token-security)
- `include/winnt.h`: Added definition for `SECURITY_CAPABILITIES`.
- `dlls/kernelbase/process.c`: Added attribute length check for `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES`; zeroed output buffer in `GetProcessMitigationPolicy` stub.
- `server/protocol.def`:
  - Extended `filter_token` with `disable_sids_size` and VARARG `restrict_sids`.
  - Added request `create_lowbox_token` with `package_sid` and `capabilities`.
  - Extended `get_token_groups` with `which_groups` selector (0=groups, 1=restricted SIDs).
  - Added request `set_token_integrity_level`.
  - Extended `get_token_info` reply with `integrity_rid`, `is_app_container`, `is_restricted`.
- `server/token.c`:
  - Added `integrity_rid`, `restricted_sids`, `app_container`, and `capabilities` to `struct token`.
  - Implemented token filtering flags `DISABLE_MAX_PRIVILEGE`, `LUA_TOKEN`, and restricting SIDs storage.
  - Implemented `create_lowbox_token` handler setting AppContainer SID, Low integrity (`0x1000`), and capabilities.
  - Implemented `set_token_integrity_level` handler for setting token integrity labels.
  - Implemented restricting SID intersection check in `token_access_check`.
  - Implemented Mandatory Integrity Check (`SYSTEM_MANDATORY_LABEL_NO_WRITE_UP`, `NO_READ_UP`, `NO_EXECUTE_UP`) in `token_access_check` and `check_object_access`.
- `dlls/ntdll/unix/security.c`:
  - Implemented `NtCreateLowBoxToken` wineserver call.
  - Extended `NtFilterToken` passing `restrict_sids` and `disable_sids_size`.
  - Implemented `NtSetInformationToken(TokenIntegrityLevel)`.
  - Implemented `NtQueryInformationToken` for `TokenIntegrityLevel`, `TokenAppContainerSid`, `TokenIsAppContainer`, `TokenIsRestricted`, `TokenHasRestrictions`, and `TokenRestrictedSids`.
- `dlls/wow64/security.c`: Added WOW64 thunks for `NtCreateLowBoxToken` and 32-bit queries for `TokenIsRestricted`, `TokenHasRestrictions`, `TokenRestrictedSids`, and `TokenAppContainerSid`.
- `dlls/kernelbase/process.c`:
  - Added `is_elevation_required(HANDLE token, const WCHAR *filename)`: checks PE manifest runlevel (`RunlevelInformationInActivationContext`). If `requireAdministrator` (or `highestAvailable` on limited token) and elevation helper is configured in `HKLM\Software\Wine\Elevation\Helper`, `CreateProcessInternalW` fails with `STATUS_ELEVATION_REQUIRED` (`ERROR_ELEVATION_REQUIRED` / 740).
- `dlls/ntdll/loader.c`:
  - Added `is_elevation_helper_configured()`: prevents silent in-process token elevation (`elevate_token()`) when helper is configured, maintaining stock behavior when unconfigured.
- `dlls/shell32/shlexec.c`:
  - Added `get_elevation_helper()` and `run_elevation_helper()`: invokes configured helper on verb `"runas"` or upon `ERROR_ELEVATION_REQUIRED`.
  - On helper approval (exit 0), spawns elevated process with `CreateProcessAsUserW(get_admin_token(), ...)` and returns genuine `hProcess`.
  - On helper refusal or cancellation (exit non-zero), sets `GetLastError() = ERROR_CANCELLED` (1223) and returns `SE_ERR_ACCESSDENIED`.
  - Handles working directory normalization, quoted helpers with arguments, and token closing safely.
- `dlls/shell32/tests/shlexec.c`:
  - Added `test_elevation()`: tests stock unprompted behavior, direct `CreateProcess` 740 failure with `requireAdministrator` manifest, `ShellExecuteEx` approval with genuine `hProcess`, `runas` approval, `ShellExecuteEx` cancellation with `ERROR_CANCELLED` (1223) and `hProcess == NULL`, and registry rollback.
- `programs/consent/`:
  - User Account Control Consent Dialog (`consent.exe`), resource script, 4-quadrant golden shield graphics, Authenticode verification, consent and credentials modes with automated test overrides (`WINE_CONSENT_AUTO_APPROVE` / `WINE_CONSENT_AUTO_DENY`).
- `include/winnt.h`:
  - Added `CLAIM_SECURITY_ATTRIBUTES_INFORMATION` structure definition for token security attribute queries.
- `dlls/ntdll/unix/security.c`:
  - Implemented `TokenUserClaimAttributes` (33) and `TokenSecurityAttributes` (39) returning valid empty claim blocks (`Version = 1, AttributeCount = 0`). Updated `info_len` table.
- `dlls/wow64/security.c`:
  - Implemented 32-bit thunking in `wow64_NtQueryInformationToken` for `TokenUserClaimAttributes` and `TokenSecurityAttributes` (12-byte layout).
- `dlls/advapi32/security.c`:
  - Handled `REVOKE_ACCESS` in `SetEntriesInAclW` first pass to cleanly remove matching ACEs without error.
- `dlls/advapi32/tests/security.c`:
  - Added unit tests `test_claim_security_attributes` and `test_revoke_access_acl` (3,665 tests passing, 0 failures).
- `dlls/kernelbase/process.c`:
  - Supported `PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER` (26), `PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY` (15), `PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY` (18), `PROC_THREAD_ATTRIBUTE_MITIGATION_AUDIT_POLICY` (24), and `PROC_THREAD_ATTRIBUTE_WIN32K_FILTER` (16) in `validate_proc_thread_attribute` and `CreateProcessInternalW`.
  - Silenced `GetProcessMitigationPolicy` stub logging to TRACE.
- `server/protocol.def`:
  - Added `@REQ(get_process_handle_table)` with `count` reply field.
  - Added `@REQ(get_thread_page_priority)` and `@REQ(set_thread_page_priority)`.
- `server/handle.c`:
  - Implemented `DECL_HANDLER(get_process_handle_table)`: queries handle table, validates buffer capacity, sets `STATUS_INFO_LENGTH_MISMATCH` with required length on undersized buffers.
- `server/thread.h`, `server/thread.c`:
  - Added `page_priority` in `struct thread` (default 5).
  - Implemented `DECL_HANDLER(get_thread_page_priority)` and `DECL_HANDLER(set_thread_page_priority)` enforcing priority range 0..5 and `THREAD_SET_LIMITED_INFORMATION` access checks.
- `server/token.c`:
  - Implemented two-pass DACL evaluation in `token_access_check`: Pass 1 checks user and group SIDs; Pass 2 checks restricting SIDs in DACL order, honoring `ACCESS_DENIED_ACE_TYPE` before allow ACEs.
  - Implemented strict intersection semantics in `filter_token`: intersecting existing restrictions with new restrictions, retaining `null_sid` (`S-1-0-0`) on disjoint sets.
  - Wired LowBox AppContainer package and capability SID checks in `token_sid_present`.
- `dlls/kernelbase/process.c`:
  - Wired `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` in `CreateProcessInternalW` to create lowbox tokens via `NtCreateLowBoxToken` and assign them to child processes.
- `dlls/ntdll/unix/process.c`:
  - Handled `ProcessHandleTable` (58) in `NtQueryInformationProcess`, querying `server_get_process_handle_table` and setting required buffer length on `STATUS_INFO_LENGTH_MISMATCH`.
- `dlls/ntdll/unix/thread.c`:
  - Implemented `ThreadPagePriority` (24) query and set in `NtQueryInformationThread` and `NtSetInformationThread` via wineserver requests.
- `dlls/advapi32/tests/security.c`:
  - Added unit tests for Case C deny on restricting SIDs in `test_restricted_token_access_check`, and `test_restricted_sid_intersection` (3,692 tests passing, 0 failures on x86_64 and WOW64).
- `dlls/ntdll/tests/info.c`:
  - Enhanced unit tests `test_process_handle_table` and `test_thread_page_priority` (2,798 64-bit and 2,729 WOW64 tests passing, 0 failures).
- `dlls/shell32/shlexec.c`:
  - Added 30-second bounded wait in `run_elevation_helper` with `TerminateProcess(pi.hProcess, 1223)` on timeout.
- `programs/consent/consent.c`:
  - Removed `WINE_CONSENT_AUTO_APPROVE` and `WINE_CONSENT_AUTO_DENY` environment bypasses.
  - Implemented genuine PAM shadow password verification via `/sbin/unix_chkpwd <username> nullok`.
  - Added root-only opt-in fixture `/etc/wineexplorer/test-elevation.conf`.
- `dlls/shell32/shell32_main.c`, `shellpath.c`, `shell32_main.h` (always; upstreamable): `DllInstall(TRUE, L"U")` (was a stub) does the per-user setup, as Windows' Active Setup does at first logon (`regsvr32 /s /n /i:U shell32.dll`): `_SHRegisterUserShellFolders(FALSE)` (HKCU, creates the user's folders) and `create_extra_folders(TRUE)` (TEMP/TMP, Temp, Themes, AccountPictures, Screenshots; not the COMMON_APPDATA step). It writes no HKLM, so it works when the elevation helper denies machine writes, where `DllRegisterServer` stops at its first HKLM write and never creates the folders. `DllRegisterServer` and other `DllInstall` arguments are unchanged.

## License headers

`wexhost` now also provides `WexAppsList` / `WexAppsLaunch` through runtime-loaded GIO. `apps.c` handles freedesktop discovery and launching with the shell's Wine variables removed; both Start menu styles expose the entries. The X11 WM checks its running state under the connection lock so requests arriving during shutdown cannot use the closed connection. These are project additions, not changes to Proton or a shared prefix.

Native focus adds `WexWmFocus`, current-focus validation for queued X events, and foreground-event synchronization. Explorer attaches the old foreground thread to its own input queue so Wine's detach operation does not recreate Explorer's queue and discard its active window/input time. Alt+Tab defers completion until after the keyboard hook. These fixes stay in Explorer and `wexhost`; the diagnostic X11-driver experiments were removed.

Rounded captions add `user_api_hook.pSetWindowRgn`, with the default user32 callback forwarding to `NtUserSetWindowRgn` and uxtheme tracking explicit application regions. The theme renderer combines the caption's transparent background region with its body, avoids recursive region repainting, and clears only its own region when styles are disabled. Start menu bitmap callbacks and its theme-derived panel shape stay in Explorer; ordinary application menu bitmaps retain upstream Wine behavior. See [THEMES.md](THEMES.md) for verification and references.
The project is GPL-2.0-or-later as a whole (see `LICENSE.md`). New project files carry the GPL-2.0-or-later header (`scripts/add-license.py` adds it to anything missing it); older project files keep their LGPL-2.1-or-later header, which is compatible.
Modified Wine files keep their original header. ReactOS-derived programs keep theirs:
- `programs/calc` - ReactOS Calc, LGPL-2.1+ (header in each file); `copying.txt` is the GPL text shipped with it.
- `programs/wmplayer` - ReactOS mplay32 (ReactOS license: GPL-2.0-or-later).
- `programs/explorer/alttab.c` - ReactOS-derived Alt+Tab implementation; retains its GPL-2.0-or-later header and attribution.
- `dlls/ntoskrnl.exe` - stock Wine.

## Themed non-client frame (visual phase)
- `include/ntuser.h`, `include/winuser.h`: new `DrawNonClientFrame` user32 callback, `struct draw_non_client_frame_params`,
  `user_api_hook.pNonClientFrameDraw` and `NCFRAME_*` flags.
- `dlls/win32u/defwnd.c`: `nc_paint` and the caption-button redraws ask the hook first; if it draws, only the rectangle is advanced.
- `dlls/user32/{user_main.c,hook.c,nonclient.c,user_private.h}`, `dlls/wow64win/user.c`: callback plumbing and default (declines).
- `dlls/uxtheme/{window.c,system.c,uxthemedll.h}`: `UXTHEME_NonClientFrameDraw` draws `Window` caption, frame and buttons from the active msstyles.
- `dlls/uxtheme/window.c`: the themed caption title takes its colour and shadow from the restored caption part (`WP_CAPTION`/`WP_SMALLCAPTION`) when the maximized part lacks those properties; the open style defines them only on the restored part, so a maximized title was drawn white on white.
- `dlls/uxtheme/{window.c,system.c,uxthemedll.h}`: the themed frame draws classic when the application cleared `STAP_ALLOW_NONCLIENT` (`SetThemeAppProperties`) and opens its theme through `UXTHEME_OpenNonClientTheme`; `OpenThemeData` returns no theme when `STAP_ALLOW_CONTROLS` is cleared, as on Windows (stock Wine ignored both; the `OpenThemeData` part is upstreamable). Used by GUI-mode setup for XP's classic look.

## Explorer window (file browser)
- `dlls/shell32/shell32.rc`: shell view column titles "File"/"Modified" are now "Name"/"Date Modified" (Windows wording).
- `programs/explorer/explorer.c`: window class is `CabinetWClass`; the window chrome moved to `browser_ui.c` (menu bar, Standard
  Buttons, Address bar with Go, activity logo, status bar) and `browser_taskpane.c` (File and Folder Tasks / Other Places /
  Details, drawn with the ExplorerBar class).  Toolbar and logo pictures come from ReactOS browseui (`res_browser/`).

## Menu accelerator underlines (Windows XP behavior)

* `dlls/user32/text.c`: `DrawText` honors `DT_HIDEPREFIX` (the `&` is removed, the letter is not underlined).
* `dlls/win32u/menu.c`: menus hide the accelerator underlines until a key is used while the menu is open (or
  `SPI_GETKEYBOARDCUES` is set), like Windows XP; menu items with a 32 pixel picture are exactly as high as the
  picture (the classic Start menu).

| `dlls/wexhost` (`unixlib.c`), `include/wexhost.h` | WEX_POWER_LOGOFF (login1 TerminateSession of our session) and WEX_POWER_SWITCHUSER (GDM CreateTransientDisplay) | always |
