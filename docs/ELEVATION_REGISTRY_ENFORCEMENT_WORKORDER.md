<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: registry and service manager access checks under enforcement

Date: 2026-10-22. Status: scoped. Part of `ELEVATION_BOUNDARY_PLAN.md`; prerequisite of the system role (step 2b
work order, item 5 and decision D2).

## Why

With a helper configured, a limited process now has a real filtered token (Administrators deny-only, Medium,
three privileges). The registry ignores it: the server skips the access check for any key without an explicit
descriptor (`check_object_access` returns success when `obj->sd` is NULL, `server/token.c`), and `create_key`
grants the new handle with `alloc_handle_no_access_check`. So a limited process can write `HKLM`, including
`HKLM\System\CurrentControlSet\Services`, and the service manager only checks handle access masks
(`programs/services/rpc.c`), which it grants as requested. A limited process can therefore register a
LocalSystem service. Until both are closed, no process may be given the system role.

## Native expectations (`TOKEN_SECURITY_REFERENCES.md`, D1 section)

Filtered token on Windows 10.0.26300:

| Operation | Result |
| :--- | :--- |
| Create a key under `HKCU` | succeeds |
| Create a key under `HKLM\SOFTWARE` | `ERROR_ACCESS_DENIED` |
| `reg add HKLM\SOFTWARE\Microsoft /v X ...` (set value) | `ERROR_ACCESS_DENIED`, `reg` exit 1 |
| Open `HKLM\SOFTWARE` for `KEY_READ` | succeeds |
| `OpenSCManager(SC_MANAGER_CREATE_SERVICE)` | `ERROR_ACCESS_DENIED` (documented SCM descriptor: Authenticated Users connect, enumerate, query lock status, read; LocalSystem the same plus modify boot config; Administrators all access) |
| `ChangeServiceConfig` on an existing service | `ERROR_ACCESS_DENIED` (documented default service descriptor: Authenticated Users only query, enumerate dependents, interrogate, user-defined control and read control) |

The SCM rows come from Microsoft's documented default descriptors ("Service Security and Access Rights"); a
worker run can confirm the codes later.

## Scope

Everything is gated on `elevation_enforced()`; with the helper unset, behavior must stay byte-for-byte what it
is today (the unset-helper suites prove it).

1. **Diagnose the earlier hang first.** The trial patch (`/tmp/wexout/registry-enforce-trial.patch` in the
   build container; reproduce it from the description in the step 2b work order if missing) used `key_get_sd`
   for keys without a descriptor in `check_object_access`. `wineboot -u` then never finished and `wineserver`
   spun at 40% CPU. Reapply it on a scratch branch, run `wineboot -u` with a helper configured and
   `WINEDEBUG=+reg,+server` (or `+reg` alone if too large), and record: which process loops, which key and
   access it is denied, and why it loops instead of failing. Expected cause: `wineboot -u`, `services.exe` and
   the `rundll32 setupapi` INF pass all run on the limited token and must write `HKLM`, as a Windows setup
   process running as SYSTEM or TrustedInstaller would. Write the finding into "Results" before item 2.
2. **Decide how the prefix update runs** (main session decides from item 1's data, not the agent). The prefix
   update writes `HKLM` by design. Options to evaluate and report, not implement yet:
   - (a) `wineboot -u` started from Unix gets the full token for its process tree. Weakness: a limited
     process can run Unix programs (`start /unix`), and a fresh Unix-launched `wine` is a new root process, so
     the server cannot tell the user's shell from a limited process; this would hand out the full token.
   - (b) Prefix updates only in the machine root, run by its root-owned setup (`setup-machine-root.sh
     --update`), which already writes the registry files as Unix root outside any Wine session; user-writable
     prefixes are updated with the helper unset.
   - (c) The prefix update asks for elevation through the broker once it exists (plan step 4).
   Recommendation to check against the data: (b) now, (c) later.
3. **Key access checks.** With the agreed design from item 2:
   - `check_object_access` uses the key's effective descriptor (`key_get_sd`, explicit or default) for keys
     when enforced;
   - `create_key` checks `KEY_CREATE_SUB_KEY` on the parent before creating a new key, and checks the
     requested access on an existing key it opens (today both use `alloc_handle_no_access_check`);
   - value writes and deletes already go through handle access masks, so they follow once the handle access
     is checked; prove it with the probe.
   Native rows above become probe checks (new `--registry-enforcement` mode in
   `scripts/test_token_security_regressions.cpp`, run in the limited-shape prefix by `check-token-security.py`,
   marker `registry_enforcement=PASS|FAIL`), plus `HKCU` writes still succeed and `HKLM` reads still succeed.
4. **Service manager access checks** (`programs/services/rpc.c`): give the SCM and each service a security
   descriptor matching the native defaults, and check the requested access with `AccessCheck` against the
   caller's token (impersonate the RPC client) when opening the SCM, creating a service, and opening a service.
   Probe: a limited process gets `ERROR_ACCESS_DENIED` for `OpenSCManager(SC_MANAGER_CREATE_SERVICE)` and for
   `OpenService(SERVICE_CHANGE_CONFIG)`; `OpenSCManager(SC_MANAGER_CONNECT)` and enumeration still succeed.
   Wine-side unset-helper behavior unchanged. Every change goes into `STOCK_CHANGES.md`.
5. **Breakage sweep under enforcement:** the step 2b item 6 list (`wineboot -u` under the item 2 design,
   `regedit` reading `HKLM`, `notepad`, `taskmgr` where scriptable), the B4 smoke test with `WEX_B4_HELPER=1`,
   and a 32-bit program writing `HKCU`.

## Acceptance

- Unset helper: full harness (`--all`, run from a directory the test user can write) with the known counts:
  `advapi32 security` 0 failures, `ntdll info` 0, `shell32 shlexec` the known 2, consent PASS; runner sentinel
  PASS; B4 smoke test PASS.
- Helper configured, both architectures: `limited_token_shape=PASS`, `linked_token_routes_closed=PASS`,
  `registry_enforcement=PASS`, the service manager probe PASS.
- No hang: every run finishes within the harness timeouts.

## How the work is split

Planning and review: the main session. Implementation: a Sonnet agent, item 1 first, then stop and report;
items 3 to 5 after the main session records the item 2 decision here.

## Results

(none yet)
