<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent acceptance: registration metadata and SID unit validation

Accepted Winecb1d117 for scoped registration metadata use after parent source
review and independent Luna clean-build/actual units in8809827b and63099038.
Parent checked all47 r2 sealed files, actual four cell JSONs/log summaries,
normal builtin loader/mapped-path evidence, per-arch prefix/build module hash
agreement, and COMPLETE-empty post-cleanup ownership. All init/unit observers
were reaped, private-loopback before/after equal, Xvfb exit0. Parent additionally
reviewed the actual initializer/helper diff and ran its5 focused tests: all pass,
0.214s, no skips. Actual native API units were executed by the assigned independent
Luna worker; parent audited raw evidence rather than rerunning unchanged units.

| Actual suite | x64 | i686 |
| --- | --- | --- |
| kernelbase security |49 assertions,0 failure/skip |49 assertions,0 failure/skip |
| userenv |260 assertions,1 inherited todo,0 failure/skip |260 assertions,1 inherited todo,0 failure/skip |

All actual units had WINEDLLOVERRIDES absent. Environment-only mscoree/mshtml
suppression was restricted to separate fresh-prefix initialization; source/DLLs
and unit assertions unchanged. The inherited userenv line405 todo remains
visible. R1 initialization timeout and evidence preserved. Build sourcecb1d117
plus normal committed project4aabab76 overlay fully reproduced; no opaque private
header/dependency. Core candidate modules below are now accepted for R6 metadata
sanity/consumer trial preparation, not as browser/rendering/sandbox acceptance:

/var/tmp/wex-g4-ac-parent-20261026/build/dlls/kernelbase/{x86_64,i386}-windows/kernelbase.dll
/var/tmp/wex-g4-ac-parent-20261026/build/dlls/userenv/{x86_64,i386}-windows/userenv.dll

Do not rebuild or change those owned validated inputs while preparing R6. Stage
copies read-only and record actual transferred bytes. Parent root M wine remains
untouched; no master merge. Untested defensive fault injection/process-death,
cross-user/reboot and broader security hardening remain TODO under the human's
Chromium-first priorities. No token isolation claim from these metadata APIs.
