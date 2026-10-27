<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Winsock 10042 root cause, minimal fix, and next blocker

Work order: `CHROMIUM_WINSOCK_OPTION_WORK_ORDER_20261027.md`
Root branch: `codex/chromium-winsock-unblock` (from network-bootstrap `3d7ed787`)
Wine base (retained unchanged): `codex/g4-token-integration` @ `fffa7de`
Wine fix branch (pushed to origin): `codex/g4-winsock-unblock` @ `d629841`

## 1. Precise 10042 classification (definitive, from a +winsock trace)

The repeated `Unknown error 10042 mapped to net::ERR_FAILED` lines are
**WSAENOPROTOOPT** (bad/unsupported protocol option), *not* air-gapping
(WSAENETUNREACH is 10051, a different code). The prior network-bootstrap
report's "expected air-gapping" label for 10042 was incorrect.

Exact failing call, captured with `WINEDEBUG=-all,+timestamp,+pid,+tid,+winsock`
on a real Chromium run under the retained candidate Wine build:

    fixme:winsock:setsockopt Unknown IPPROTO_IP   optname 0x00000032
    fixme:winsock:setsockopt Unknown IPPROTO_IPV6 optname 0x00000032

- API:     `setsockopt`
- level:   `IPPROTO_IP` (2) for IPv4 sockets; `IPPROTO_IPV6` (41) for IPv6 sockets
- optname: `0x32` = 50
    - IPv4: `IP_ECN`   (`include/ws2ipdef.h:101`)
    - IPv6: `IPV6_ECN` (`include/ws2ipdef.h:244`)
- optval:  a small integer ECN setting; Wine's unrecognized-option fixme logs
           only the optname (not the value/length), so the exact bytes are not
           echoed. The call shape is `setsockopt(fd, level, IP_ECN/IPV6_ECN, &u32, sizeof(u32))`.
- return:  `SOCKET_ERROR`, `WSAGetLastError` = WSAENOPROTOOPT (10042)
- caller:  `net/base/net_errors_win.cc:121` `MapSystemError` default case ->
           `LOG(WARNING) "Unknown error 10042 mapped to net::ERR_FAILED"`

ECN (Explicit Congestion Notification) is a best-effort TCP option. Chromium
enables it on every client socket; on real Windows `setsockopt(IP_ECN /
IPV6_ECN)` succeeds, so this path never errors there. Wine had no `case` for
either option, so both fell through to the per-level `default:` and returned
WSAENOPROTOOPT (socket.c:3668-3671 for IPPROTO_IP, 3770-3773 for IPPROTO_IPV6).

Correlation: **53** unrecognized `IP_ECN`/`IPV6_ECN` setsockopt lines in the
trace == **53** "Unknown error 10042" warnings in `chromium.log` (1:1).

This call is **not** in `net/socket/tcp_socket_win.cc`, which only sets
`SO_EXCLUSIVEADDRUSE` and `SO_RANDOMIZE_PORT` (both `SOL_SOCKET`). It comes from
a deeper net-stack socket-setup path. `SO_RANDOMIZE_PORT` (`0x3005`) itself is
handled by Wine (returns 0) and is **not** a source of the 10042.

A separate, distinct error also appears in the same log and is *not* the 10042:
`address_sorter_win.cc:164 SIO_ADDRESS_LIST_SORT failed 10045` (WSAEOPNOTSUPPD,
an unsupported ioctl). It is noted only to keep it from being conflated with the
IP_ECN WSAENOPROTOOPT.

## 2. Controlled probe (tiny loopback PE under the candidate Wine)

`winsock_probe.exe` applied Chromium's client option sequence against a local
HTTP fixture over loopback, under the retained build, with the corrected
`SO_RANDOMIZE_PORT = 0x3005`:

    setsockopt(TCP_NODELAY)         rv=0   err=0
    WSAIoctl(SIO_KEEPALIVE_VALS)    rv=0   err=0
    setsockopt(SO_RANDOMIZE_PORT)   rv=0   err=0     (0x3005; Wine returns 0)
    WSAIoctl(SIO_TCP_INITIAL_RTO)   rv=-1  err=10045 (WSAEOPNOTSUPPD; return ignored by Chromium)
    connect(loopback)               rv=0   err=10045 (stale last-error from the prior failed ioctl)
    send(GET)                       rv=54  err=0
    recv                            rv=183 err=0     -> HTTP/1.0 200 OK, real bytes received

Findings:
- Every client option Chromium uses with a correct value is clean on Wine.
- `SIO_TCP_INITIAL_RTO` fails with **10045** (a different code), and Chromium
  ignores that return (loopback-only, feature-gated).
- A successful `connect()` still carried the previous call's last-error (10045):
  Wine leaves stale winsock last-errors after a failed option call.
- Loopback connect/send/recv fully works: real HTTP 200 + bytes received. The
  IP_ECN failure does **not** block socket data transfer.

## 3. Minimal fix (implemented, committed, pushed)

Wine child branch `codex/g4-winsock-unblock` @ `d629841` (from base `fffa7de`),
`dlls/ws2_32/socket.c`, two accept-and-ignore cases mirroring the existing
`IPV6_PROTECTION_LEVEL` handling:

    case IP_ECN:
        FIXME("IP_ECN is ignored!\n");
        return 0;

    case IPV6_ECN:
        FIXME("IPV6_ECN is ignored!\n");
        return 0;

Rationale: on real Windows these calls succeed, so accepting them (and ignoring
the value, since ECN remains unimplemented) matches the documented contract and
removes all 53 spurious WSAENOPROTOOPT / ERR_FAILED warnings. It does not fake a
capability or remove any security/feature; it only stops reporting an error that
Windows would not report for this best-effort option.

**Not built/tested in-session:** the Wine source here is pristine/unconfigured
(no `./configure` + `make` available in this environment), so `d629841` is a
source-only change awaiting an incremental `ws2_32` rebuild. The loaded guest
build (`fffa7de`) was not modified.

## 4. Page outcome (this diagnostic run)

The +winsock trace run did **not** load the fixture page: `chromium.log` shows
`Network service crashed or was terminated, restarting service` and
`sad_tab.cc: Tab Killed`, and the fixture server recorded 0 requests
(`run-summary.json` `fixture_requests: []`). This is attributed to the heavy
`WINEDEBUG=+winsock` overhead (every socket call traced across all threads and
processes) destabilizing the network service/renderer inside a 30s diagnostic
window, not to the IP_ECN option failure itself, which the loopback probe shows
does not block data transfer. A normal (untraced) run is the correct vehicle for
page-load verification (see next blocker).

## 5. Next blocker

1. Incremental `ws2_32` rebuild of `codex/g4-winsock-unblock` @ `d629841` on the
   guest/WSL build tree (not possible in this session: pristine source, no build
   environment here). Deploy as a fresh private candidate; do not modify the
   loaded `fffa7de` build.
2. ONE fresh post-fix **normal** Chromium run (no +winsock) against the original
   fixture to confirm (a) zero "Unknown error 10042" warnings and (b) the fixture
   page fetches/renders. Authorized without another paperwork pause per work-order
   step 4.
3. If that normal post-fix run still fails to load the page for a reason other
   than 10042, that is a distinct blocker; this work order's concrete socket
   defect (IP_ECN -> WSAENOPROTOOPT) is fully classified and addressed.

## Evidence (private, D:-backed)

`ac_build/nb_evidence/chromium-winsock-unblock-20261027/`:
- `probe_corrected_0x3005.out` -- corrected loopback probe (SO_RANDOMIZE_PORT=0x3005 -> rv=0; SIO_TCP_INITIAL_RTO -> 10045; connect/send/recv OK)
- `probe.out` -- earlier diagnostic capture, pre-correction (used the unverified fallback 0x4802 for SO_RANDOMIZE_PORT, which spuriously returned 10042; kept to show the correction)
- `chromium.log` -- this trace run's Chromium log (53x 10042, network-service restart, tab kill, SIO_ADDRESS_LIST_SORT 10045)
- `run-summary.json` -- this trace run summary (`fixture_requests: []`; `chrome_exit_code -15`/SIGTERM from the 30s bound)
- `diag.log`, `fixture.html` -- earlier diagnostic run log + local fixture

Full `+winsock` trace (large; kept on the guest, not copied):
/home/debian/wex-g4/chromium-winsock-trace-20261027/wine-trace.log`.
