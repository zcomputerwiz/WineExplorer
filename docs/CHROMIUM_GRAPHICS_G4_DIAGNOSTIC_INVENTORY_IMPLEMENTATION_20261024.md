<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Diagnostic inventory policy: offline implementation complete

Implemented DIAGNOSTIC_INVENTORY_PLAN_20261024 under the human logging policy.
No VM/SSH/Wine/Windows/browser execution, calibration repeat, disk mutation or
new preparation. Parent's R4 native acceptance stands; device71->58 remains a
historical warning. No device research or new provenance gate is selected.

New `g4_diagnostic_inventory.py` records actual requested/resolved path,
device/inode/size/times/SHA256 separately from previous values. Differences are
structured warnings. A changed hash makes previous_version_tested=false; missing/
unreadable/timeout observations leave unknown values and explicit errors, never
invented metadata. Metadata-only observations explicitly say no hash was requested.
The bounded recorder accepts a shared operation deadline and makes no loaded-file,
version-resource or security attestation claim.

Current native collector records actual environment/PE/internal-loader inventories
instead of refusing historical hashes. Required executable/loader paths remain
regular/canonical; the known frontend alias resolves inside its intended build.
Loader pathname, entire argv tail, PID/start/prefix and all native event/full-status/
natural-helper0/cleanup assertions remain. Old Edge registration/version equality
is no longer a prerequisite for an unrelated original-child tool diagnostic;
its file observation is retained. No browser adapter assertions were changed.

Current R4 private run records host source/helpers/reference/guest environment and
loader observations without historical refusal. Remote unavailable hashes retain
the command result and unknown per-file values; they cannot claim an old version.
Current TAR checksum and deployed transfer checks remain fatal, as do selected
disk/backing paths, evidence overwrite, active ownership/PID reuse, readiness,
space/budgets, complete-empty ownership, networking/sockets and functional gates.
An overwrite check now runs before copying a consumed run's preflight.

Reusable private diagnostic_storage records current selected-chain/sibling data
and historical differences. Concrete intended disk/backings/canonical files,
normal container flags and active-owner checks stay operational checks. No storage
observation was dispatched against lab images in this task; tests use plain D:
fixtures and fake tool responses.

Consumed historical storage/preparation/source-approval functions remain only as
inactive reference/test code. CLI preparation and non-R4 legacy controls are
explicitly retired, so future controls cannot select those old refusal paths.
Consumed R4 itself refuses existing evidence. Next Chromium adapter must package
the new helper and a fresh current transfer inventory; no old16-member bundle or
manifest was rewritten/reused. APP_TEST_HARNESS/current policy now state this.

Validation through WSL Debian from C: checkout scripts:

```text
python3 -B -m unittest test_g4_diagnostic_inventory test_g4_native_loader_r4 test_g4_native_identity_isolation test_g4_rejected_identity test_g4_native_calibration_refresh test_g4_native_exit_calibration test_g4_native_exit test_g4_prefix_ownership_integration test_g4_prefix_ownership test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic test_g4_edge_owned_threads -q
Ran 176 tests in 49.880s: OK, no skips.
```

Six new policy controls plus existing controls with historical-refusal expectations
updated. Actual current hashing/unknown observations, changed metadata/hash warnings,
remote missing hashes, wrong disk/backing, active owner, overwrite, PID reuse and
transfer corruption are exercised. Functional/protocol assertions were preserved.
Historical positive fixtures remain explicitly synthetic, not new native evidence.

Private evidence is D:lab `diagnostic-inventory-offline-r1-20261024/`: before/after
procedure, patch, logs and16-file observation inventory. `evidence.json` SHA256
`476f82bcfdfb8685f0a03058830463a67bf8525f50ef3078c72c695b057df665`;
procedure after SHA b86eeac0... . No new run manifest or native/API claim. Existing
M wine/main D: master/other workers untouched. STOPPED for parent review, then
separately planned actual Chromium Windows exit observation rather than more gates.
