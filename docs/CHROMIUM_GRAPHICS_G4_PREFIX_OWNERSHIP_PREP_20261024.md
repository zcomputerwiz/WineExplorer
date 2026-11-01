<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Prefix-ownership integration — PREPARED_NOT_RUN

Implements only [PREFIX_OWNERSHIP_INTEGRATION_PLAN](CHROMIUM_GRAPHICS_G4_PREFIX_OWNERSHIP_INTEGRATION_PLAN_20261024.md).
No VM/SSH connection, guest file/runtime/staging, image mutation, service change,
browser or calibration. Launcher, accepted scanner semantics and10652-byte PROBE
remain unchanged (PROBE SHA ce5fcd5097173dd1b3651a0f6424f1c533f3d6c37d1cf8b868da8b252bcd737c).
M wine/D:master/other workers and all historical evidence/preparations preserved.

## Bounded implementation

Host-only `validate_response()` added AFTER PROBE assembly; it never executes
the scanner. Strict bytes/<=65536/newline/nontruncated/exit0, UTF-8, duplicate-free/
finite JSON, exact schema/version/request/nonce, COMPLETE only; validates typed
owners/busy, unique PID/start identities, flag classifications, limits/timings,
read-byte/empty/task counters and owner-to-observation consistency. UNKNOWN or
an omitted request cannot become idle. Complete busy/empty are distinct results.

Explicit private `prefix-ownership` control returns no consumer and ends
unconditionally after the validated observation. Authenticated readiness remains
BEFORE df;674/cap180/650/free1GiB/19actual hashes/435/remote10s/scanner8s retained.
Remote command is exactly sudo -n python3 -B -c <acceptedPROBE> <prefix> <nonce>,
no deployment. Raw stdout/stderr/code saved BEFORE parsing; owner list recorded
ONLY after validation. New control alone retains stdout up to65536 losslessly
on success/nonzero/timeout, with original lengths/caps/truncation. Timeout keeps
partial bytes and returncode=null, never fake0. Oversize/truncated/error/incomplete
responses stop with no followup SSH; busy/empty also never stage/archive.
Legacy4KiB/8KiB capture defaults and original idle modes/records remain intact.

## Actual tests

WSL Debian, owned C: checkout `scripts`:

```text
python3 -B -m unittest test_g4_prefix_ownership_integration test_g4_prefix_ownership test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic -q
Ran 102 tests in 9.190s — OK, exit0, no skips
```

Prior91 plus11 methods: strict parser negatives, actual empty/busy/large (>4KiB)
structured success and failure capture, timeout/oversize/nonbytes/malformed/
unknown/nonzero/wrong nonce/bad owner, guards/host and guest hash failures,
no-followup dispatch, sealed-current storage negatives and fresh preparation
without process dispatch. Current fixture records, not a mutable future disk,
drive missing/changed/backing negatives. Existing native-child/socket controls
still pass; no actual SSH or guest scanner invocation. Scanner/PROBE unchanged.

## Fresh immutable preparation and evidence

Under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `prefix-ownership-r1-20261024.preflight/preflight.json` SHA256
  `6a013513c6688bfe21d1e7f6ab2ba73a97459688f7ced873dc02e814b5b8dd17`.
  Exact WindowsC: launch/diagnostic/80s stop argv,8 helper pins,11 sources/tests,
  2 seeds/ISO/prior/credential observer,19 EXPECTED binaries; source hashes are
  authoritative, source_commit is only provenance. Durable credential source/
  seed-public match and native restrictive ownership/modes passed, no secrets.
  Requested prefix `/home/debian/wex-g4/edge-install-r1-v2-20261022/prefix`,
  pinned nonce `77ffba7d6416ac50b38913258f4510d2`; exact PROBE bytes/argv retained.
- Existing previously booted safety top35323904bytes/SHAcb1aaaa91a4b27ac34be6619f36f63b628c52490a9fba7b39e2530f787ce89b7
  matches sealed r2 after identity. Normal-lock4chain/frozen3 IDs/idle snapshots
  pass; former-top fullSHAc2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936
  matches in10.349359879s (<120), no65GB rescan. D:free1924284932096bytes.
  Run absent, no source TAR/consumer/create argv. No live19-pin/readiness claim.
- `prefix-ownership-r1-20261024.change/`: before/after/patch and preparation
  stdout/stderr (exit0, stderr empty), with sealed hashes of7 final files.

| Private change artifact | SHA256 |
| --- | --- |
| procedure.before.py | 8b54e69c60d4e874216b0061122431737e45538333b99036a79e4c47e17c8907 |
| procedure.after.py / actual private source | 385dfaf0ede70bd33282daaca470a22bfafe77c61624b2e06052afe371c88c35 |
| procedure.patch | a63573fee358e7fd7ea3b1d6fd7a22f1bb363dbdd71ac5971173901f13ff0e58 |

Current ownership module SHA538e14f2e06a3bfab3242175b5f7bdcaf3a285f6b282e351ae57fcc1ecb23a3e.
Future caller must still prove owned QMP guest event/ACKs/QEMU0/no fallback/
children absent/Windows0 within900/90 bounds. No filesystem-clean, guest PID2
classification, original-failure cause, idle barrier or graphics claim.
STOP parent review BEFORE any live control; no authority follows preparation.
