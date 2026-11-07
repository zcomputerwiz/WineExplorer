<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S6 / S10 run failures of 2026-11-07 09:20 (input for the fix task)

Full logs: D:/DeepThought/ChromiumLab/e1s-baseline/run4-s6.log and run4-s10.log.
Run: lab tree /root/owned-e1s6/build/wow64 rebuilt from the current sources with the test defines, clean wexbuild, root.
Every c2/c3/c4 case passed, including all e1s5 cases and the S6 transport cases accept/stale/replay/late/owner/disconnect/cancel/child-impostor for 64-bit, then:

## S6 (scripts/test_e1s6_transport.sh)

```
c4_e1s5_version_32=PASS
c4_e1s5_redemption_32=PASS
c4_e1s5_mutated-request_32=PASS
grep: /var/lib/wexsys-c2/e1s6-service.log: No such file or directory
c4_e1s6_accept_64=PASS
c4_e1s6_stale_64=PASS
c4_e1s6_replay_64=PASS
c4_e1s6_late_64=PASS
c4_e1s6_owner_64=PASS
c4_e1s6_disconnect_64=PASS
c4_e1s6_cancel_64=PASS
c4_e1s6_child-impostor_64=PASS
c4_e1s6_pipe_impostor_64=PASS
FAIL: S6 pipe impostor/64: server authority denial missing
--- /var/lib/wexsys-c2/e1s6-wine.log
0328:fixme:file:NtFsControlFile FSCTL_PIPE_IMPERSONATE: impersonating self
0108:fixme:file:NtFsControlFile FSCTL_PIPE_IMPERSONATE: impersonating self
0058:fixme:file:NtFsControlFile FSCTL_PIPE_IMPERSONATE: impersonating self
```

Last lines:

```
E1S6_STAGE native phase=open
E1S6_STAGE native phase=child
E1S6_STAGE native phase=cancel
E1S6_STAGE native phase=open
E1S6_DENY native site=child-identity function=message line=405
E1S6_DENY native site=exception function=require line=36 origin=require:36 errno=0 detail={"type":"ValueError","text":"transport denied","dbus_name":"","call":null}
E1S6_DENY native site=exception function=call_blocking line=696 origin=live:313 errno=0 detail={"type":"DBusException","text":"org.freedesktop.DBus.Error.NameHasNoOwner: Could not get PID of name ':1.1761': no such name","dbus_name":"org.freedesktop.DBus.Error.NameHasNoOwner","call":{"bus_name":"org
E1S6_TRANSPORT FAIL
S6 rc=1 2026-11-07T09:20:30-06:00
```

## S10 (scripts/test_e1s10_token.sh)

```
S10 start 2026-11-07T09:20:33-06:00
results=/var/tmp/e1s10-results.MtmUvCbd
probes_both_abis=PASS
lab_build_and_normal_exclusion=PASS
grep: /var/tmp/e1s10-results.MtmUvCbd/lab/e1s6-service.log: No such file or directory
userdel: wexret mail spool (/var/mail/wexret) not found
FAIL: prefix shutdown failed
userdel: wexstd mail spool (/var/mail/wexstd) not found
userdel: wexadm mail spool (/var/mail/wexadm) not found
userdel: wexcar mail spool (/var/mail/wexcar) not found
FAIL: lab fixture/logs retained where cleanup failed: /var/tmp/e1s10-results.MtmUvCbd/lab
E1S10_TOKEN FAIL
S10 rc=1 2026-11-07T09:21:00-06:00
```
