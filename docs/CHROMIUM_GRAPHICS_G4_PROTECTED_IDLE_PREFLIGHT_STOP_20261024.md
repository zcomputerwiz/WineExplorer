<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Protected idle run — stopped BEFORE boot, credential absent

Under [PROTECTED_IDLE_RUN_PLAN](CHROMIUM_GRAPHICS_G4_PROTECTED_IDLE_RUN_PLAN_20261024.md),
the fresh host-only gate exited1 before any launch/SSH/guest command.
Actual error: `RuntimeError('Expected private lab key600 unavailable')`.
Separate read-only path metadata confirmed
`/tmp/wex-visible-recovery-r1/vm_key`: exists=false, is_file=false, mode=null.
No private key contents were printed or copied. No credential repair/retry.

The immutable prep SHA,27 audited helper/source/seed/ISO/prior/change/tool
hashes,19 expected pins, exact original probe, idle owner/open-chain snapshot
and frozen3 identities had passed. Unused new top identity/hash and absent
run/controller outputs were checked before the missing-key gate. Fresh host
reserve/normal-lock info/full frozen-top hash were NOT reached; do not imply
the whole prelaunch gate passed. Prior accepted creation remains preserved.

Evidence directory:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-idle-ready-r1-20261024.host-audit/`.

- `before.json`, status FAILED_PRESERVED, finished02:02:12UTC; SHA256
  `bb609118f478126c39c590cd71bdbe0a10d786cfe2576a2692228dbdfa2d5d81`.
- `observe-host.py` one-shot read-only audit source; SHA256
  `b2a542c01ee3eca530ccd476993384e219519b36a82a1bb9ccdc0f49bfd60fe8`.
  It AST-extracts ONLY the sealed creation script's read-only audit functions,
  never executes its module/create body. No source/procedure pin was changed.

No VM was started, so there is no readiness/idle/19-actual-pin/QMP/QEMU/controller
result or shutdown to perform. Future run remains absent; new disk/backing chain
was not written. No tests rerun. M wine/D:master/other workers untouched.
Stopped for parent review; no boot retry, service fix, staging, calibration or
browser action. Original idle-command failure remains unexplained.
