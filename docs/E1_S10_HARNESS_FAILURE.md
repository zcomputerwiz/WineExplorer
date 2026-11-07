<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S10/S11 token harness failure of 2026-11-07 09:35 (input for the fix task)

State: `scripts/test_e1s6_transport.sh` now passes (E1S6_TRANSPORT OK, 302 PASS, twice in a row). The S10/S11 harness
`scripts/test_e1s10_token.sh` (run as root in clean wexbuild with WINE_BUILD_DIR=/root/owned-e1s6/build/wow64 and
WINE_NORMAL_BUILD_DIR=/root/owned-e1s11/build/normal-wow64) gets through: probe builds for both ABIs, the lab build with the
S10/S6 defines (compile lines verified), the normal-runtime exclusion check, the S2 machine-root setup and the S6 service start.
Harness bugs already fixed this session: protocol assertion counting REQ_NB_REQUESTS; EXTRA_CFLAGS (Wine makefiles ignore it, use
CFLAGS); compile-line checks over continuation lines; named_pipe.o missing from the lab objects; stop_prefixes treating
`wineserver -k` with no server as a failure and not passing WINEPREFIX.

## First failing case: lab_accept_wexadm_64

```
FAIL: peer exit/20s timeout: lab_accept_wexadm_64
```

Retained evidence (copy): the broker log `case-1-broker.log` is

```
E1S10 READY
FAIL line 617: FALSE
```

i.e. the broker probe (scripts/e1s10_token_probe.c) printed READY, then reached its `done:` label at line 617 (`check(FALSE)`)
through a `goto done` that printed no message of its own, and exited with failure. The peer (32-bit probe, mode `peer PIPE`,
run as the standard user with `timeout -k 2 20`) produced an EMPTY log: nothing printed within 20 seconds, then killed.
`bootstrap-1.log` holds only `FSCTL_PIPE_IMPERSONATE: impersonating self` fixme lines. `wexsys-s10-setup.log` is a normal setup
(machine root bootstrap, wex-accounts generation 1). Xvfb log has only harmless 'server already running' retries.

Questions for the fix: (1) which `goto done` in the broker's accept path was taken (every `goto done` must print its own
distinct FAIL message with the failing call and GetLastError/NTSTATUS; add that); (2) why the peer printed nothing: does it block
in pipe connect/WaitNamedPipe because the broker's pipe security descriptor (the `sd` in the probe) denies the standard user, or
because the broker never calls ConnectNamedPipe before the peer's wait, or a wrong pipe name quoting (`\.\pipe\e1s10-$$-N-arch`
built in run_case with doubled backslashes and passed through a service command line that is iconv-converted: check the name
round-trips); (3) the peer should print a line when it starts and at every stage so that an empty log cannot happen; (4) the
harness should copy the peer log and the wine server stderr of the prefix into the retained fixture on failure.
