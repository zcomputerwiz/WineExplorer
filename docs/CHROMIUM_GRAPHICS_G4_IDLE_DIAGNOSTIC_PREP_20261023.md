<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Idle-prefix diagnostic — PREPARED_NOT_RUN

Implements only the offline [plan](CHROMIUM_GRAPHICS_G4_IDLE_PREFLIGHT_DIAGNOSTIC_PLAN_20261023.md).
No VM/SSH/Wine/helper/browser command ran. No runtime download/build or guest
staging occurred. Original R1 run/preparation and M wine remain untouched.

Private procedure now has explicit `native-idle-diagnostic` control. It validates
owned launcher/child start identities and actual NEW80GiB drive, host inputs,
650 guard, guest free >=1GiB and accepted19 binary hashes, then435 guard and the
original probe. Its literal/argv/sudo/errno policy are unchanged (probe SHA256
`d36b5d1d24f0964c98491263a034e6477f7f6af26a5079cc1244e2b39327aac0`).

Successful probe stdout/stderr are captured as bytes and saved BEFORE JSON
interpretation with actual argv/code, presence/type/origin/lengths, base64 and
4096/8192-byte truncation limits. Success is not a fabricated exception. Failed
commands use accepted remote-error preservation. Empty and busy observations
both return before every guest staging/consumer path; archive remains skipped.
Malformed successful output remains saved before parsing fails. No diagnostic
result implies calibration, idle-policy repair, browser or G4 acceptance.

## Immutable private preparation / changes

All paths below are under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `native-idle-diagnostic-r1-20261023.preflight/preflight.json` SHA256
  `b084c73baac6760570dff0bd73046bccac6e0e58aed5f82c657551aa68d1d2c4`.
  Exact Windows launch, WSL observe and identity-stop commands are recorded.
  Includes five actual C: helper paths/hashes, three test-source inventories,
  two seeds/ISO,19 accepted pins, original probe/read-only argv, current overlay
  size/mtime identity,900/90 limits,650/435 gates and explicit QEMU0 AND
  Windowscontroller0/PIDs-absent final requirement. No source TAR or guest
  consumer argv exists. Full overlay content hash was NOT measured here.
- `native-idle-diagnostic-r1-20261023.change/procedure.before.py` SHA256
  `4cea0c0bfd9cb5aa49f47d864949ace0a1f007a11207009dd50f7a4b52f449a3`.
- Same directory `procedure.after.py` (actual ignored procedure) SHA256
  `e663e8f5bf6924b069738acac39dd677705de9399c120c114bce4c62230b4445`.
- Same directory `procedure.patch` SHA256
  `cf36faf3ac1212203ba3391d69983e4d5528782161cc3ee6a1625cf6e7003fbe`.

Host-only audit passed all helper/seed/ISO/source hashes, prior preflight
reference,19-pin count and probe digest. Future run directory is absent.
One initial host preparation failed before creating its directory because AST
selection also found the new preparer's nonliteral `probe` assignment; narrowed
selection to the actual constant, then preparation succeeded. No old artifact
was replaced. No guest observation was made.

## Actual tests

WSL Debian, owned C: checkout `scripts`:

```text
python3 -B -m unittest test_g4_idle_diagnostic test_g4_remote_failure test_g4_virgl_budget -q
Ran 31 tests in 2.614s — OK; exit0, no skips
```

Five new executable tests drive the actual private control with fake dispatch:
empty/busy success, actual-shaped failure bytes, each mandatory guard failure,
malformed output retained before parse, and no consumer command. Every control
path asserts the exact read-only dispatch list (zero stage/consumer/archive).
Existing26 controls remain passing, including their one bounded original local
Python failure (not Wine/guest). New temporary artifacts use D: storage.

STOP for parent review; **no live authority this handoff**. Usage88% five-hour /
14% weekly at checkpoint; no credits used. Any future live attempt requires
separate approval and fresh ownership/hash/usage/space checks plus clean owned
shutdown; no automatic calibration follows this observation.

## Independent parent acceptance / ONE short observation

Parent read actual current private control/formatter/preparer/early-return and
all new tests. Independently31 PASS/no skips in2.716s; audited all five helpers,
three test sources, two seeds+actual ISO, prior preparation reference,19 accepted
pins, original probe hash/syntax and top size/mtime. Manifestb084c73b... matches,
run absent, consumer argv empty. No TAR/staging/consumer path in this control.
Repaired failure capture and raw success-before-parse behavior remain intact.

Known cosmetic metadata limitation: captured_output's byte_origin still says
CalledProcessError captured bytes even on success. Success has no exception_type
and records actual returncode0; it is NOT an exception or product acceptance.
This label is not a guard and should be clarified in a later fresh preparation,
not by silently changing this immutable helper pin. Archive-skipped's generic
preflight wording likewise does not imply a successful observation failed.

At90% five-hour/14%weekly ONE exact prepared read-only observation is authorized
NOW only if fresh launch usage <=92%. If higher, stop without boot and preserve
preparation for after reset. Exact manifest commands; no extra SSH/staging/Wine/
helper/browser/calibration/retry. Always immediate exact-identity SIGINT/QMP,
QEMU0 AND Windows0/owned identities absent. Retain raw decoded command error and
all bounds/pins/output, concise committed AND pushed report, STOP. At95% finish
only this bounded operation/cleanup/checkpoint. No follow-on fix this window.
