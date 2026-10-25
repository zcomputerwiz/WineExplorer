<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native registration oracle parent review

Reviewed Luna c0d8e2ae74a1618a66e581d03503a17de35356fa on pushed branch
codex/chromium-appcontainer-registration-oracle. ACCEPTED as bounded valid-input
contract evidence, not a complete API/security conformance suite.

Parent read all255 source and194 test lines, lifecycle and allocator ownership,
original report and actual native JSON. Independently ran17 mocked tests on
native Windows (all pass,0.006s, no skips), submitted diff check and remote HEAD
verification. All13 private seal entries match retained bytes. The native test
was performed by Luna, not independently repeated by parent; parent examined
its actual events/child/output/cleanup rather than only its summary.

Actual valid matching ASCII package SID/moniker lifecycle:

- Missing lookup:0x80070002, NULL output.
- Fresh registration: S_OK, exact moniker returned by lookup.
- Duplicate same SID:0x800700b7; readable mapping intact.
- Independent child26488 exited0/reaped and read both mappings, establishing
  cross-process visibility in this user/session, not reboot or cross-user proof.
- Unregister:S_OK; subsequent lookup/second unregister:0x80070002.
- A unregister left B intact; finally removed B and confirmed absence.
- Successful registrations left LastError122, which is not an HRESULT failure.
- Lookup strings paired with AppContainerFreeMemory; derived SIDs with FreeSid;
  converted SID strings with LocalFree. Cleanup complete, native completed=true.

No blocking code finding in the exercised path. Default does not load/mutate;
fresh random fixed-prefix identities only; collision/permission failure refuses
mutation; only successful fresh registrations become cleanup-owned. Mock tests
cover partial failure/child timeout/reaping/cleanup. This is a small local oracle,
not an adversarial execution supervisor; do not generalize its per-step deadline
or synthetic tests into a hard bound on every synchronous Windows API.

Unobserved contracts: NULL/invalid input exact HRESULT/output semantics,
Unicode/length/mismatched SID, duplicate display metadata, concurrency/reboot,
allocation failures, access-denied/persistence faults. The new implementation
work order defines meaningful tests and explicit limitations without blocking
valid Chromium functionality on a comprehensive security framework.

Private evidence remains D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/
appcontainer-registration-oracle-r1-20261025/; no vendor payload included.
Next assignee is Antigravity/Gemini, implementing genuine Wine metadata APIs
with tests; no new guest trial before parent review.
