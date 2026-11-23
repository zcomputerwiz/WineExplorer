<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# Session 0 and the user prefix split: architecture review

Date: 2026-11-23. Read at WineExplorer `262bda60` and Wine `83dbab38`, plus Wine `6a6c7536` (`claude/session-services`,
not merged). Nothing is changed by this review. It asks one question: should the structure of the session 0 / user
split be refactored? It does not cover the test harnesses.

Method: four read-only code surveys (registry, cross-server links and trust, services and process identity, machine
root and modes), an independent review by Codex that was given the code and the question but not these findings, and
hand checks of the findings marked "checked" below. Line-level clean-ups are already listed in
[WINE_REUSE_REVIEW.md](WINE_REUSE_REVIEW.md) (2026-11-14). This note is about structure and does not repeat them.

## Verdict

**Keep the shape. Refactor in targeted steps. Do not rewrite globally.**

The design itself is sound, and Codex reached the same view independently:
- Session 0 is a machine server that holds the registry. Programs reach it directly over a registry-only channel.
  Services and the elevation decision move there behind bridges.
- It is the shape Windows has. Its measured cost is nil (ST-17: ratios 0.93 to 0.99).
- It has passed a Codex security review twice.
- A single wineserver for all users was rejected for good reasons ([SYSTEM_TIER_DESIGN.md](SYSTEM_TIER_DESIGN.md),
  option A), and nothing found here reopens that.

What needs work is how the shape was reached. It was built in layers: the prefix split (S1 to S4), then session 0
(T1, T2, T4a), then services (T5) and the broker (T6). Each layer added its own mode, list, link message and copy of
a check, and none removed what it replaced. The result:

1. **Too many modes are alive at once.** There are five ways to persist a hive and five ways a server can start, and
   the code does not compute either choice in one place.
2. **Placement policy is written as lists in C, copied by hand.** Two of the copies already disagree.
3. **The links to session 0 have no common layer.** Every feature added its own message, send code and trust check.
4. **Process and handle identity stops at the server's edge.** The bridges work around this case by case, and that
   is where Office (B76) broke.

None of this calls for a different architecture. Each point has a contained fix, listed below in the order of
value for the cost.

## Findings

### 1. The mode matrix

**Hive persistence.** The code has these models (checked: `registry.c` 396-440, 884-900, 4380-4500):

| Model | Used by |
|---|---|
| Stock Wine: temporary file, then rename | plain prefixes; session 0 in development mode |
| S3 merge-on-save under `.reg.lock`: per-session keys, tombstones, replay of dirty keys | servers whose machine hive is a link into the machine root; **also session 0 itself** (its machine branch counts as shared, so `is_per_session_key` and the merge run inside session 0) |
| Sysrun in-place save (`save_hive_in_place`, not atomic) | launcher-started servers without session 0 |
| Session 0 owns the machine hives (5 s lazy flush, `#sd` lines) | session 0 |
| T4a: session 0 holds the user's hive, the user's server writes it | user servers with session 0 |
| E1 S12-S14 managed profile hives (`e1s_hive`, about 1,090 lines) | lab builds only; its hooks are compiled into every build |

About 650 lines of `registry.c` exist only for merge-on-save and about 430 only for session 0. Each write path
(`set_value`, `delete_key`, `rename_key` and six more) asks `is_per_session_key`, which builds and frees a path
string on every call.

**Server start.** The startup profiles are decided in `session0_init` (`session0.c` 667-706):
1. stock;
2. launcher-started without session 0;
3. session 0 itself;
4. a launcher-started user server with session 0;
5. a server the launcher did not start, which drops session 0 even when the configuration names it;
6. development mode on either side (`WEX_SESSION0_DEV`, `WEX_SESSION0_SOCKET`).

The elevation setting adds an axis (enforced or not; R8 or R9 by `HKLM\Software\WineExplorer\Elevation\R9`), and
the lab build flags add more. The choice is a set of global booleans (`session0_server`, `session0_client`,
`sysrun_active`, `forward_mode`) tested at about 50 sites in the server, ntdll and `services.exe`.

**Stale second copies.** A user's server with session 0 still loads the machine hives and its own `user.reg`
(`registry.c` 4054-4093; ST-14 was deliberately left open). Some readers still use those copies:
- `elevation_enforced()` reads the "enforced" switch from the server's local snapshot (checked: `registry.c` 551).
  ntdll reads the same switch through session 0 (`loader.c` 4259). The switch therefore has two sources, which
  agree only until the machine hive changes during a session.
- `uwp_compatibility_enabled` reads the local copy of HKCU, which is stale once session 0 holds the hive.

Since 2026-11-18 session 0 is the default for new installs. Profiles 1, 2 and 5 are still needed: for plain prefixes,
for installs from before that date, and for the out-of-box setup account. Only profile 2 and merge-on-save as a
multi-user mechanism are candidates for retirement, and that is a decision for the owner (decision 2 below).

### 2. Placement policy as hand-written lists

| Policy | Copies |
|---|---|
| Keys that stay per session | `WEX_SESSION_MACHINE_KEYS` in `wex_session0.h` (25 entries, used by ntdll's routing) and `hklm_ephemeral_prefixes` in `registry.c` (20 entries, used for persistence). **They disagree** (checked): the server copy lacks `Hardware`, both `ServiceCurrent` keys and both `SafeBoot\Option` keys. `is_per_session_value` (the `PROCESSOR_*` values) has no counterpart in ntdll at all. |
| Services that stay in the session | `is_session_service` (`services.c` 1499-1514: driver types, `Winedevice*`, 16 names). The `SessionServices` registry value (6a6c7536) is a second source. Related lists: the SafeBoot lists in `wine.inf`, `always_started` (SamSs), and the bridge's exclusion of PID-bound pipe names (`named_pipe.c` 3273-3303). |
| Pipes allowed over the bridge | `pipe_bridge_allowed` (`named_pipe.c` 3361); the names are repeated as literals in `forward.c` and `e1s18_broker.c` |
| Trusted images | four lists in `server/sysrun.c` (`system_images`, broker, dialog, and the lab copies); the same "stat and compare device and inode" loop is written five times |
| Matching "this path or below it" | three matchers: `match_ascii_prefix_ci` in the server, `regsrv_match` and `regvirt_match` in ntdll. The code that turns a SID into a `\Registry\User\...` path is written three times in ntdll. |

Service placement is by name, but what it really encodes is a capability: whether the service needs the caller's
processes, handles or desktop. A service that programs install (Office's ClickToRunSvc) defaults to session 0
whatever it needs. That is how B76 reached its `E_INVALIDARG` layer.

### 3. The links to session 0

Session 0 and the user servers exchange one versioned link protocol (`wex_session0.h`, version 5) with nine message
types: CHANNEL, USER_HIVE, HIVE_SAVE, HIVE_UNLOAD, SESSION_ID, HIVE_SAVED, PIPE, PIPE_WAIT and orders. That is a
good base. Around it:
- **Send and receive code is copied.** The code that sends a header, a token description and SCM_RIGHTS is copied
  four times (`session0.c` 1046, 1432, 1539, 1632), and the receive loop twice (880, 1333).
- **Fields mean different things per message.** For example, `machine` is the program's architecture in CHANNEL,
  "written or not" in HIVE_SAVED, and the name size in PIPE.
- **The launcher-started-server test exists three times:**
  - `peer_is_launched_server` / `peer_trusted` in `session0.c` (717, 740), the only copy that pins the peer with a
    pidfd and checks the process name;
  - `sysrun_server_ok` in `ntdll/unix/sysrun.c` (144);
  - `server_ok` in `tools/wex-sysrun` (241).

  The last two are the same parser, without the pidfd hardening.
- **The SYSTEM check is repeated.** The test "SYSTEM, privileged and SeTcbPrivilege" is written out by hand five
  times (`token.c` 1270, 2713, 3269; `named_pipe.c` 2239; `sysrun.c` 711).

Codex rates this as "a shared base without clean typing and lifecycle". The fix is a library under the existing
protocol, not a new protocol.

### 4. Identity across servers

Each wineserver numbers its own processes and handles. The bridges deal with this one case at a time:
- **Bridged clients have no PID.** For a bridged pipe client, session 0 reports PID 0 (`named_pipe.c` 3468), so a
  service cannot open the wrong process by number.
- **Some pipes are not bridged.** Pipes named after a PID or OXID (`LRPC<pid>`, `OLE_<oxid>`, `lsasspirpc`) are
  excluded from the bridge.
- **Session 0's PIDs leak into sessions.** In the other direction nothing is done (checked: `rpc.c` 1370):
  `QueryServiceStatusEx` and the merged enumeration hand session 0's process ids to programs in a session, where
  they name another process or none.
- **Several registry handle cases fail.** Session 0 key handles cannot be inherited by a child process, duplicated
  into another process, loaded, saved or unloaded. Virtual-store keys cannot be duplicated at all.
- **B76 is the first real casualty.** Office C2R's App-V manager takes its client's PID; the fix so far moves the
  service into the session (`SessionServices`).

This is the one finding that cannot be fixed by moving code; it needs a design decision (decision 4 below).

### 5. Smaller structural points

- **The upgrade writes outside session 0** (checked: `setup-machine-root.sh` 251-257, 295-331; `package-wine.sh` 491).
  `--update` runs `wineboot -u` and two `regedit` imports as root with their own wineserver on the machine root,
  while session 0 keeps running. That breaks the owner's rule of 2026-11-14 that every machine-registry write goes
  through session 0. Session 0's next save merges the file once, but its in-memory tree never gets the changes, so
  its following save very likely writes them away. Not reproduced yet; filed with the orchestrator as a bug.
- **Stored key permissions can be dropped.** `#sd` lines are written only when `session0_server` is set (checked:
  `registry.c` 889). A launcher-started server that falls back to its own copy (no session 0 at its start) would drop
  every stored key permission the next time it saves the machine hive.
- **User prefixes are built by shell scripts in several places:** `setup-machine-root.sh` 801-824 (the template),
  `migrate-prefix-split.sh` 82-99 (a second copy of the link list), `setup-greeter.sh` and
  `wine-explorer-session-common.sh`. No C code does it.
- **S1's copy-on-write is no longer per user.** Since S2 links whole folders, a copy-on-write break happens inside the
  shared machine `system32` (`ntdll/unix/file.c` 4347-4434). It is still needed while fake DLLs are links, but it is
  now "a machine-root write that replaces a link", and PREFIX_SPLIT.md describes it the old way.
- **Lab code lives inside stock files.** The lab-only blocks are about 990 lines of `named_pipe.c`, 820 of
  `registry.c`, 470 of `token.c` and 320 of `sysrun.c`. The R8 frame format is coded three times: the C codec, the
  Python mirror, and a hand decoder in `named_pipe.c` 2106-2155. R9's broker repeats R8's steps
  (`e1r9_broker0.c` 230), and both are `#include`d into `wexelevate/main.c`.
- **The brokers' pipe code is copied too** (found 2026-11-23, reviewing the DWM broker's move to a pipe). The token
  broker (`tokenbroker/main.c`: `create_instance`, `pipe_io`, `check_caller`) and `wexelevate` (`main.c`
  `create_instance`, the client in `wexelevate.h`) each write their own pipe server and client; the DWM broker would
  be the third. What differs per broker is only the caller policy (the token broker refuses AppContainer callers; the
  DWM broker must accept them) and the name. None of the clients checks that the pipe's server is the broker's image:
  the token broker's client checks only the server's user, which an AppContainer process of that user also has.
- **Docs have drifted from the code.**
  - `SYSTEM_TIER_T5_SERVICES.md` still describes the relay that was not built.
  - `SYSTEM_TIER_WORK_ORDERS.md` 395 and 427 are out of date: the bridge allows more than `svcctl`, and service
    status notifications now work by polling.
  - `forward.c` has four wrappers that nothing calls.

## Recommended refactors

In order. Sizes are planning estimates from Codex and the surveys; most of the work is moving code. The rule for
stock Wine stays: WineExplorer's logic goes into its own files (`session0*.c`, `wex_*`), and stock files keep only
the hooks, each listed in [STOCK_CHANGES.md](STOCK_CHANGES.md).

| # | Refactor | What it removes | Size; risk |
|---|---|---|---|
| R0 | **Fixes that need no refactor:** run the upgrade's Wine steps through session 0 (`MachineWine`, as `wex-components.py` does) or stop session 0 around them; one per-session key list (below); write `#sd` in every save of a machine hive; correct the docs | a likely lost update, a list that disagrees, a dropped permission | days; low |
| R1 | **One policy table per decision:** <br>- a single per-session table in `wex_session0.h` with separate flags for routing (ntdll) and persistence (server), used by both; <br>- service placement as data, not names: a protected per-service value (scope, and what it needs: the caller's processes, handles, desktop), with defaults from `wine.inf` and `SessionServices` folded into it; <br>- the bridge allowlist and trusted images as one table each; <br>- the server's role computed once at start into a read-only record, with the impossible combinations refused there | two disagreeing lists; the name-based guess behind B76; scattered globals | 1-3 weeks; low to medium |
| R2 | **A session 0 link library:** <br>- one exchange helper (header, token description, descriptors, bounded receive, descriptor ownership); <br>- typed message bodies instead of shared fields; <br>- explicit link states; <br>- one peer test, with the pidfd pin, shared by the server, ntdll and the launcher; <br>- one `sysrun_process_is_tcb_system()` | four send copies, two receive loops, three launcher tests, five SYSTEM tests | 2-3 weeks; medium (security code: Codex review) |
| R3 | **Registry handles in ntdll:** <br>- one synthetic handle table with local, session 0 and virtual-store back ends (today two tables); <br>- one path matcher and one SID-path helper; <br>- duplication and inheritance handled once | two handle managers; three matchers; three SID-path builders | 1-3 weeks; medium |
| R4 | **Hive storage back ends:** <br>- `save_branch_info` gets an explicit storage kind (stock, session 0 owner, remote user hive, lab profile) with operations per kind; <br>- ST-14 finished: a user's server gets its start-up values over the session link and stops loading machine hives and its own `user.reg`; <br>- merge-on-save and the in-place save retired as multi-user mechanisms if decision 2 is taken | about 650 lines of merge machinery, the stale copies, the two sources of the elevation switch | 2-4 weeks; medium to high |
| R5 | **Lab code out of stock files and one elevation engine:** <br>- the E1 lab blocks move to their own files behind thin hooks; <br>- one authorization state machine with a local (R8) and an agent (R9) transport; <br>- R8's runtime selection removed once R9 ships | about 2,600 lab lines inside stock files; two broker flows | 2-4 weeks; medium |
| R6 | **Identity across servers:** a design note first, then the change (decision 4) | the per-service workarounds (PID 0, excluded pipes, PIDs leaking into sessions) | 4-8 weeks or more; high |
| R7 | **Small:** one shell helper for the prefix link list; drop or wire `forward.c`'s four unused wrappers; one helper for the `forward_mode` checks in `rpc.c`; one static-inline pipe header (`wine/include/wine/wex_pipe.h`: server instance, client connect, transact with a timeout, a check of the server's image) used by the DWM broker first, then the token broker and `wexelevate`, each keeping its own caller policy; a Windows-like descriptor on the tokens of the system role (SYSTEM full control, Administrators query only), after which `open_token` can take Windows' rule (the limited query right for any access, the token's descriptor deciding) instead of requiring the full query right for everything but reading (2026-11-23) | copied link lists, dead code, three copies of the broker pipe code, a token-open rule that differs from Windows | days; low (the token descriptor: medium, security code) |

R0 and R1 give the most for the least, and R1 also removes the cause of the next B76-like surprise. R2 and R3 are
independent of each other and can run in parallel. R4 waits for decision 2. R6 waits for decision 4 and should not
block anything else.

Not recommended:
- **One wineserver for all users** (option A): months of work, and every server path becomes a confused deputy.
- **A second, general bridge protocol:** the existing link protocol is the right base. It needs a library, not a
  replacement.
- **Merging configuration parsers again:** the C readers already share `wex_sysrun_conf.h`. The remaining parsers are
  test and lab scripts, plus one setup script that runs after the C reader has validated the file.

## Progress

The owner approved R0 and R1 on 2026-11-23. The fixes in R0 that are bugs (B81, B83, B84, B85) went to the
orchestrator; B82 is part of R1a.

| Item | State |
|---|---|
| R0 docs | done: T5 design marked superseded as built; bridge allowlist and service status notifications in `SYSTEM_TIER_WORK_ORDERS.md`; the copy-on-write note in `PREFIX_SPLIT.md` |
| R1a | done (Wine 9df8f18d, B82): one list of the keys of each session, used by ntdll's routing and the server's persistence; one path matcher instead of three. A table with separate routing and persistence flags turned out not to be needed: once the server's missing entries were added, every entry needs both |
| R1b | done (Wine 3a1fb8ba): `WexSessionService` in a service's own key places it; it replaces the `SessionServices` list. The built-in names stay as the defaults in C: moving them to `wine.inf` would only take effect when `wine.inf` is applied again, and a service of the session placed in session 0 in the meantime (RpcSs, PlugPlay) breaks the session. What a service needs (processes, handles, desktop) is not recorded: one value covers the case met so far (B76) |
| R1c | done for the trusted images (Wine 2a2b2946: one table with roles). Not done for the bridge's pipe names: the server writes them as WCHAR arrays, `services.exe` and `wexelevate` as `L""` strings, so one shared definition would need a macro per name for little gain |
| R1d | not done, on purpose: `session0_init` already decides the role once at start and refuses impossible combinations with a fatal error. What remains are the fallback modes, which R4 retires; renaming about 50 flag checks before that would be churn |

| R2 | done (approved 2026-11-23; Wine `claude/r2-session0-link` f19545fe, WineExplorer acccd7ef; full suite passed, Codex review): R2a one check of the system role's authority (`sysrun_process_is_tcb_system`, four copies replaced; the lab endorsement keeps its per-condition audit labels); R2b one launcher-started peer test (`include/wine/wex_sysrun_peer.h`) for session 0, ntdll and the launcher, now all with the pidfd pin; R2c one exchange in `session0.c` (`link_send`, `link_receive`, `link_request`), typed request fields (a union, the same wire word, protocol 5), explicit link kinds. One tightening: a cut hive message is ignored |
| R3 | in progress in its own session (Wine and WineExplorer `claude/r3-registry-handles`) |

Branches: WineExplorer `claude/session0-split-review` (docs and the T1 test, Wine pinned to `claude/r1-policy-tables`),
Wine `claude/r1-policy-tables` (on `claude/session-services`, with B85 merged).

## Decisions for the owner

1. **Targeted refactor (R0 to R7) rather than a rewrite.** Recommended.
2. **Retire the launcher mode without session 0 for installed systems:** an upgrade turns session 0 on for an install
   from before 2026-11-18, after which merge-on-save is no longer a multi-user mechanism (R4). Plain prefixes and the
   setup account keep stock behaviour. Recommended. The cost is one migration on upgrade.
3. **R9 as the only elevation flow** once the production verifier exists, with R8's runtime selection removed (R5).
   Recommended. Until then both stay.
4. **How identity crosses servers (R6).** Two options; I recommend (a), with (b) used only where (a) is not enough.
   - **(a) Machine-unique process ids.** Each server allocates PIDs in a range derived from its session id, and
     session 0 can ask the owning server, over the session link, about a process (image, session, token user)
     without holding a handle to it.
   - **(b) Origin-qualified object references** (Codex's proposal): an opaque reference naming the owning server, its
     generation and a capability. Operations on the object run at the owning server.

   Until either exists, the rule stays as it is now: a service that needs its callers' processes or handles runs
   in the session (R1 makes that rule data).

## Bugs found (sent to the orchestrator for `KNOWN_BUGS.md`)

| Bug | State |
|---|---|
| Package upgrade writes the machine hive with its own wineserver while session 0 runs; the changes are likely lost at session 0's second save | read in the code, not reproduced |
| Per-session key lists disagree: `Hardware`, `ServiceCurrent` and `SafeBoot\Option` are routed as per-session by ntdll, but the server's persistence treats them as shared | checked in the code |
| A launcher-started server that falls back to its own machine hive drops the stored `#sd` key permissions when it saves | checked in the code |
| `elevation_enforced()` reads the switch from the user server's machine snapshot, ntdll reads it from session 0 | checked in the code |
| `QueryServiceStatusEx` and service enumeration report session 0's process ids to programs in a session | checked in the code |
| The token broker's pipe can be taken by an AppContainer process of the user before the broker starts (it starts on the first request); the client checks only the server's user, so it would send its requests there and take forged replies | checked in the code (`broker.c` `server_is_own_user`, `connect_broker`) |
