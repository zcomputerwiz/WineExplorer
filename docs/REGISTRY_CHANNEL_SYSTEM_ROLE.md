<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# A session's system role and session 0's machine registry (design note)

Status: draft 1, 2026-11-27, for the owner's decision (the reviewer's request after B132). Docs only.

## The problem

Session 0 holds the machine registry (T1, docs/SYSTEM_TIER_DESIGN.md option D). Every program of a user's session gets
a registry channel (`get_registry_channel`, server/session0.c): its server describes the program's primary token, and
session 0 serves the channel as a registry-only client with that token, made by `create_bound_token`. That binds only
the user SID (the account map's SID of the server's uid, or a service account of the session's system role); groups,
privileges and integrity are the server's word, "as the registry channel has since T1" (SYSTEM_TIER_WORK_ORDERS.md,
the T5 review).

So a program of the session's **system role** (SYSTEM inside the user's own server, under the user's uid; the
session's own services) writes session 0's HKLM as SYSTEM. HKLM's descriptors give SYSTEM full control, including
`HKLM\System\CurrentControlSet\Services\<name>\ImagePath`, which session 0's service manager runs as SYSTEM: whoever
controls a session's system role controls the machine. B132 closed the same hole for `link_bound_token` (X frames, the
pipe bridge); the registry channel does not go through it.

How reachable it is depends on one open fact (the orchestrator checks it on an installed image after batch 1149, the
reviewer's item (c)): whether a user can tamper with their own session's system role. Its processes run with the system
group as their saved group (so they are not dumpable by the user while that holds), but the user's own wineserver is
the user's process; if the user can ptrace it, the user can make it describe anything. Either way the design should
not rest on it: a session's system role is, by the model B132 adopted, never session 0's SYSTEM.

## What a session's system role needs from the machine registry

Most per-session machine state already stays in the session's own server (`wex_session_key_match`,
include/wine/wex_session0.h): `Hardware`, `Enum`, `Control\Class`, `Video`, `DeviceClasses`, `Print`, `ComputerName`,
`ServiceCurrent`, `SafeBoot\Option`, `Tcpip\Parameters`, `ProfileList`, the shell folders, Wine's drives and ports. The
device and Plug and Play writes of a session's services (winedevice, plugplay) land there, not in session 0.

What remains is not known from reading the code alone, so the first step measures it:

- **M1, measure.** A session 0 log, once per key and value, of every registry write made through a channel whose
  described user is a service account (a session's system role), with the key path, value name and the program's
  image. Collected over a full suite run, the install chain (an installed image's first boot, logon, the
  safe-mode stages) and the application tests (MSI installs, Office, Edge). Read-only for behaviour: nothing changes.

The expected result, to be confirmed by M1: few or no writes. The machine-root updates (wine.inf, wineboot --init) run
in session 0 itself (wex-session0-services.service); installers' machine writes go through the elevation broker (an
administrator's elevated process, not the system role) or, for MSI's SYSTEM custom actions, through session 0's MSI
host (option B).

## The proposal

- **P1. The registry channel gets B132's policy.** `start_registry_client` builds its token through the same
  `link_policy` as `link_bound_token`: a session's system role becomes the user's limited token (the account SID,
  SYSTEM deny-only, Medium), an administrator's own description (its limited token, the broker's elevated one) is
  believed as since T1, everything else is cut by session 0's accounts record. A session's system role then writes
  HKLM with the user's rights: denied for HKLM\System and HKLM\Software as for any standard user (and for an
  administrator's limited token), as on Windows for a program that is not elevated.
- **P2. A narrow brokered path, only for what M1 shows is needed.** If M1 finds machine keys a session's service must
  write (none expected), each gets an explicit entry in a session 0 allow-list (key path prefix and value names, read
  from a root-owned file), written by session 0 on the system role's behalf after checking the caller is the system
  role of that session (the server's system-group flag, `SO_PEERCRED`, as E1's full form); everything outside the list
  is written as the user. No wildcard on `Services\*`, `Run*`, `Winlogon`, `Image File Execution Options`, policies or
  anything else that leads to code execution as SYSTEM: those stay administrator writes through consent.
- **P3. Reads stay as they are.** A session's system role reads HKLM as before (HKLM is readable by users); only writes
  change.

## Risks

- Something a session's service writes in session 0's HKLM breaks. M1 is there to find it before P1; P2 is the escape
  for what it finds, never a widening of P1.
- Programs that check "am I SYSTEM" in a session (rare under Wine) see the user's limited token in session 0's registry
  only; their own process token in the session is unchanged.

## Order of work

1. M1 (logging only), one full suite and the install chain, the list published here.
2. The owner's decision on P1 and on each P2 entry, if any.
3. P1 (and P2 entries) with a test that reproduces the hole first: a session's system role writing
   `HKLM\System\CurrentControlSet\Services\WexHoleTest\ImagePath` through its registry channel succeeds today and is
   refused after; a session's service's normal start (plugplay, winedevice) still works.
