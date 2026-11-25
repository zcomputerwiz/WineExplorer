<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Services in Computer Management: the service manager survey

Status: survey, 2026-11-24 (GPO and Domain agent). Step 2 of the Computer Management plan
([XP_SETTINGS_GAPS.md](XP_SETTINGS_GAPS.md), row Computer Management). The Services snap-in is built on what the service
manager gives a caller; this note records what it gives today and the gaps that go to the system tier as bugs.

## How it was measured

`scripts/test_sysrun_server.sh`, section `svc_table` (`WEX_SUITE_ONLY=svc_table scripts/lab/wex-suite-only.sh svc_table`):
`sysrun_roles_probe.c` mode `svc-table NAME [create]` runs every operation the snap-in needs and prints one
`row|service|operation|error|detail` line each (the rows are kept in `/var/tmp/wex-run/svc-table-*.txt`). The section is a
survey: it fails only when the probe cannot run. Services: `WexSvcTbl` (a session 0 service, demand start,
LocalSystem; `wexsvc.exe` as its image, which waits for the stop control) and `WexSvcMine` (created by the caller).
Callers: a standard user (`wexstd`), an administrator with the limited token (`wexadm`), and a system role process (the
probe in place of `winedevice.exe`, the nearest thing to an elevated administrator the suite has), on x64 and (users) x86.

## The table (session 0 service WexSvcTbl)

| Operation | Standard user | Administrator (limited token) | System role |
|---|---|---|---|
| OpenSCManager (connect) | ok | ok | ok |
| OpenService query rights (config, status, interrogate) | ok | ok | ok |
| QueryServiceConfig (type, start type, account) | ok | ok | ok |
| QueryServiceConfig2 description | ok (none set) | ok | ok |
| **QueryServiceConfig2 failure actions** | **124 (ERROR_INVALID_LEVEL)** | **124** | **124** |
| QueryServiceStatusEx (stopped: state 1, exit 1077, no pid, no controls) | ok | ok | ok |
| OpenService change config | 5 (access denied) | 5 | ok |
| ChangeServiceConfig start type (disabled, restore) | not reachable | not reachable | ok, read back right |
| ChangeServiceConfig2 description, read back | not reachable | not reachable | ok, read back right |
| ChangeServiceConfig2 failure actions | not reachable | not reachable | returns ok, **read back 124** |
| OpenService start / stop / all access / DELETE | 5 | 5 | ok |
| StartService, then status (state 4, pid, controls 0x5) | not reachable | not reachable | ok |
| ControlService interrogate, stop; status after (state 1) | not reachable | not reachable | ok |
| OpenSCManager with create right | 5 | 5 | ok |

A service the caller creates itself (`WexSvcMine`, system role only, since the others cannot create) behaves the same as
the session 0 service in every row, delete included.

## What it means for the snap-in

- Listing, properties (General: name, description, path, start type, status; Log On: the account) and the status column
  work for every caller with what exists. Descriptions for services that have none show empty, as on Windows.
- Changes (start, stop, pause, restart, start type, the failure actions page, description) work for a caller with the
  system role and are denied for the others, the same as the table. The snap-in therefore goes through the elevation
  (consent) path for its actions, as the plan says: an elevated administrator is expected to behave like the system role
  column. **Not measured:** the elevated administrator (the suite's broker stub, section `c4_elevated`, is the way in);
  to be added before the snap-in's actions are called done.
- The snap-in does not need `ControlServiceExW` (stock Wine does not implement it either): it uses `ControlService`.

## Gaps (to the system tier as general bugs)

1. **QueryServiceConfig2W with SERVICE_CONFIG_FAILURE_ACTIONS returns ERROR_INVALID_LEVEL (124)**, for every caller, on
   session 0 services and on services created by the caller, also right after a successful
   ChangeServiceConfig2W(SERVICE_CONFIG_FAILURE_ACTIONS). The Recovery page of the service properties needs it. Whether
   the change is stored is not known from the outside: `ChangeServiceConfig2W` returns success and the query cannot show
   it. The client (stock `advapi32`) or the service manager (`programs/services`) side to be found by the owner of the
   fix; the snap-in shows the Recovery page grayed with a reason until it works.
2. `ControlServiceExW` is not implemented anywhere (stock Wine too); not needed by the snap-in, listed for completeness.

Not covered here: pause and continue (the test service does not accept them), dependencies (`QueryServiceConfigW`'s
dependency list is read by the General tab's Dependencies page; to be probed with the page), the service-specific
control codes, and a service in a session (not session 0) other than one the caller creates.
