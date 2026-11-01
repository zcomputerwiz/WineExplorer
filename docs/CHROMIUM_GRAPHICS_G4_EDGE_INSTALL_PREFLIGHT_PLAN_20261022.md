<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edge installation prerequisite: one private-prefix attempt

Parent independently verified dd87d264's 215257088-byte MSI, official metadata
SHA256 324d226c5ade08c2776645cfc2a7b12af9c08214abf18c6499410cd8ed92d1b3 and
Valid Microsoft Authenticode signature/thumbprint. Human expressly accepted the
Edge terms and private disposable-VM testing. No installation/execution yet.

Before the full graphics fixture, establish whether the unmodified vendor MSI
installs in the current Wine stack. This is an installation prerequisite only,
NOT browser startup, graphics, sandbox enforcement or G4 acceptance. The existing
full-Edge plan's tested fixture/pixel requirements still precede any later live
graphics smoke. Parent plans/reviews; Luna implements.

## Offline implementation/preparation milestone

Prepare a small original private-prefix installation runner using existing owned
Wine/compositor/isolation helpers, plus executable positive/negative tests for
package/path/hash/command/environment/deadline/cleanup behavior. Prefer minimal
reuse, no general installer framework or production Wine change. Stage/hash its
complete dependency inventory and vendor MSI before any installer invocation.
Validate consent/provenance inputs and the exact approved MSI digest; no arbitrary
installer download/command input. No host msiexec/setup, other user prefix or
existing VM/session/default changes.

Use the existing build and fresh output/prefix under the owned D:-backed VirGL
guest. Verify guest free space >=5 GiB before transfer/creation; if not, stop and
report without deleting evidence. Keep logs and the installed prefix for later
separately reviewed browser testing. No prefix/runtime cleanup is authorized here.

The vendor's documented [VDI deployment command](https://learn.microsoft.com/en-us/deployedge/edge-for-virtualized-desktop-infrastructure)
uses msiexec /i PACKAGE /qn /norestart /l*v LOG. Invoke it THROUGH the owned Wine
binary, only with the verified private MSI and a private verbose log path.
Do not add undocumented launch/update/policy/capability/security properties or
disable the sandbox. No silent retries, administrative extraction substitution,
repackaging or extra component installation if normal install fails.

Keep the consumer's complete maximum below its existing 300-second SSH bound:
e.g. prefix initialization <=90 seconds, installer <=120 seconds, bounded owned
display/Wine cleanup and overhead. Timeout is a preserved failure, not evidence
of a product rendering defect. Mandatory host guards 650/435 and 900-second
launcher/90-second shutdown reserve remain enforced. No automatic reboot.
If return is reboot-required, record it distinctly and stop for review, not pass.

Capture installer return status, full MSI/Wine stdout/stderr, any setup helper/
installer-created product processes and available logs. A successful return alone
does not establish installed payload: require actual installed msedge.exe in the
fresh prefix, record resolved location, SHA256/PE architecture/version information
and product registration evidence. Do not execute the browser explicitly. If the
installer starts product processes itself, record and clean only that owned prefix;
do not misdescribe them as an authorized graphics-smoke attempt or success.

Commit/push runner/tests and concise prepared-not-run note, return for independent
parent code/test/manifest review. Only then dispatch ONE installer attempt. On
failure preserve logs/state and return for diagnosis, no bypass or repeat. Verify
exact-identity graceful QMP/QEMU/Windows shutdown and private-prefix cleanup of
processes (not files). Keep all payload/art/screenshots private on D:.
At95% five-hour/99% weekly used checkpoint; never consume reset credits.
