# Password elevation for standard users: from the lab to the product

Written 2026-11-13, after enforced elevation ran on an installed machine for an administrator
([SYSTEM_TIER_WORK_ORDERS.md](SYSTEM_TIER_WORK_ORDERS.md), "Enforced elevation on an installed machine"). This
note is the inventory and the proposed order of work for the part that does not exist in the product: a standard
user who elevates by typing an administrator's password (R8, R9). Nothing here is enabled.

Line numbers are those of 2026-11-13 (WineExplorer 09f5c938, Wine 2ebf65d5).

## Where it stands

- An administrator's click-to-consent elevation is in the normal build and works on an installed machine.
- A standard user gets "not supported" from the normal broker (`src/programs/wexelevate/main.c:509-519`).
- Everything for the password path is built only by the lab (`scripts/lab/r8-lab-build.sh`, "Never for a shipped
  image") and passes there: lab suite 420 checks, 79 R8 and 11 R9 cases.
- The owner's gate is still closed: `E1_DECISIONS.md:199` says "DO NOT ENABLE" (review R1 to R15), and `:202`
  says R13 needs a production broker identity gate and R14 still gates enablement.

## What the lab has that the product needs

| Part | Lab form | Product form needed |
|---|---|---|
| Verifier | `tools/e1s6/e1s6-lab.py` as a root process started by the test fixture; bus name `org.wineexplorer.E1S6Lab`; a policy file installed temporarily; state in memory only | an installed service: its own bus name and policy, a systemd unit, an activation rule, fixed and verified paths, lockout counters that survive a restart, an audit log with a rotation rule |
| Password worker | `tools/e1s7/pam-worker.c` with `WEX_E1S7_LAB`, PAM service `wex-e1s-test` or `wex-r8-lab` (`pam_unix` only) | the worker without the lab flag and without `WEX_E1S7_TEST`, and a real `/etc/pam.d` service: this is a decision (below) |
| Profile provisioning | `tools/wex-profile/wex-profile-provision.c` with `WEX_E1S12_PROFILE_TEST` and a test seed | the helper with the product seed (`/etc/skel/.wine/user.reg`), installed by setup |
| Server | S6, S10, S13 code under `WEX_E1S6_TRANSPORT_TEST`, `WEX_E1S10_TOKEN_TEST`, `WEX_E1S13_HKU_TEST` | the same code without the flags, minus the hooks named below |
| `wexhost` | `src/dlls/wexhost/e1s6.c` under `WEX_E1S6_TRANSPORT_TEST` | the transport without the flag and with the product bus name |
| Profile paths in client DLLs | kernelbase, ntdll, userenv, shell32 under `WEX_E1S13_HKU_TEST` | the same without the flag |
| Broker and dialog | `WEXELEVATE_E1S18` (`e1s18_broker.c`, `e1r9_broker0.c`, the R8 part of `consent.c`) | the same without the flag, minus the hooks named below |
| Switches | registry values `EnableStandardUserElevation`, `R9`, written by the test | set by setup on an image that opts in |

## Test hooks that must never ship

These are inside the flagged code today, so removing a flag would ship them. Each has to be cut out, or moved
behind a flag that only tests set, before its flag goes:

- `consent.c`, about 1226 to 1295: `ElevationTest\R8AutoSelect`, `R8AutoPassword`, `R8AutoPassword2` type the
  password for the tests.
- `e1s18_broker.c`: the test setters (249, 331, 356-380, 415-447, 471, 506, 540-558, 726, 941, 4361), the
  `r8_trace_*` output (731-770), `accounts.map` read through `Z:\etc\wineexplorer` (560), the fixed uid and
  `/tmp` scaffolding (3770-3783), the lab frames 28 and 29 (1995); `e1s18_broker.h:326` (`e1s18_test_sha256`).
- `wine/server/sysrun.c`: recognition of `wexelevate_test.exe` (391, 402) and of an unpinned lab broker
  (442-446); the `E1S6_DENY` audits on stderr.
- `wine/server/token.c`: `WEX_TOKEN_TEST_INSTALL` / `e1s_install_fixture` (about 2752, 3072-3077).
- `src/dlls/wexhost/e1s6.c`: the `E1S6_TEST_*` operations (782-842) and the lab bus name
  (`src/include/e1s6_protocol.h:14-15, 22-25`).
- `wex_profile_storage.c`: the `checkpoint` / `test_stop_step` block (101-107).
- `WEX_R8_SERVER_TEST` traces, `WEX_E1S14_CRASH_TEST`, `WEX_E1S7_TEST`, `WEXELEVATE_TEST_APPROVAL`,
  `WEXCONSENT_TEST_HOOK`, `WEX_CRED_DIALOG_TEST_HOOK`: test only, they stay flags that only tests set.
- In the verifier script: `--lab-service`, `--impostor`, `WEX_E1S6_LAB_NO_RECEIPT`, the fallback to the source
  tree's worker, the legacy fixture phases, the stdout traces.

The suite's marker check (`scripts/test_sysrun_server.sh:787-797`: a normal build must not contain
`wexelevate_test.exe`, `org.wineexplorer.E1S6Lab`, `E1S6_DENY`) stays, and grows with each hook that is cut out.

## Known deviations the lab still has

From `E1_S18E_WORK_ORDERS.md:1054-1172` and the R9 spec; each is either fixed or accepted in writing before
enablement:

- the verifier is the lab script; no installed service exists;
- lab frames 28/29, DONE before commit, the working-directory rule not applied, `accounts.map` through `Z:`;
- the dialog's deviations; the broker's waits block inside the transport call;
- R10 open points: a revocable transaction owner, NEGOTIATE before redemption, the receipt's root generation;
- the provisioning helper runs synchronously; lockout counters do not survive a restart; no manual unlock;
- B0 serves one request at a time; the limit per session is not built (R9 spec 58-59);
- the 5 s setup bound and the per-fixture root generation "are not production policy" (`E1_S6_TRANSPORT.md:265`);
- still to do by the work orders: the whole-chain review, an interactive check, "R4 rest, R5, R7, R14, then S19
  and S20".

## Decisions that are the owner's

From `E1_S_STANDARD_USERS.md:224, 255-258` and `E1_S7_PAM_WORKER.md:330-331`:

1. **The PAM service.** Which stack checks the password: `pam_unix` alone, or the system's `common-auth` (which
   brings whatever the machine has: fingerprint, smart card, a domain through Winbind or SSSD). General
   multi-factor stacks need a decoy transcript policy first.
2. **Lockout.** Threshold and duration defaults, whether the counters are the host's (faillock) or the
   verifier's own, and how an administrator unlocks.
3. **Audit.** Where the verifier's record of attempts goes and how long it is kept.
4. **The administrators.** The group that makes an account an administrator (`sudo` today) and whether the
   built-in Administrator account can be used to elevate.
5. **Lifting the gate.** `E1_DECISIONS.md:199` stays "DO NOT ENABLE" until the owner lifts it after the reviews.

## Proposed order of work

Each step leaves the normal build and the lab suite passing, and nothing is enabled by default at any step.

1. **Split hooks from product code** (mechanical, large): for every "mixed" site above, move the hook behind a
   test-only flag and leave the product code under its current flag. The lab build sets both; the result is
   identical for the lab suite. Good work for Qwen and Antigravity with a reviewed list, file by file.
2. **A product build flag in place of the lab flags**: one switch, off by default, that turns on the product
   code (server, `wexhost`, client DLLs, broker, dialog) without any hook. The suite gains a second lab variant
   built this way, with the password still typed by a hook that lives only in the test variant.
3. **The verifier as a service**: the script's product subset under a product name (bus name, policy, unit,
   paths verified at start, persistent lockout state, audit), with the lab script reduced to a test wrapper
   around the same code. The password worker and the provisioning helper built without their lab flags and
   installed by setup.
4. **Opt-in image**: a packaging switch like `WEX_ELEVATION`, on top of it, that installs the service and sets
   `EnableStandardUserElevation` and `R9`. One elevation by a standard user on an installed machine, with the
   password typed on the screen.
5. **The open deviations and the reviews** (R10 points, R14, the whole-chain review), then the owner's decision
   on the gate, then the default.

Steps 1 and 2 need no decision. Step 3 needs decisions 1 to 3; step 4 needs decision 4.
