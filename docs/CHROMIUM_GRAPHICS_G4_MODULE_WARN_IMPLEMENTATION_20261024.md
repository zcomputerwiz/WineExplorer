<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Output-only R2 native diagnostic — offline implementation

Completed bounded `MODULE_WARN_PLAN_20261024` implementation. **NOT_PREPARED /
NOT_RUN**: no manifest, VM, SSH, Wine/native PE/browser execution, image or
production build. Static candidates remain unproven as the failing call.

Only `WEX_EXIT_OUTPUT` in the shared native header is now overrideable through
`#ifndef`; its original default, image, manifest marker, calibration paths and
ordinary API/held-handle sequence are unchanged. Original native C++ source was
not edited. The private R2 build force-includes one header defining output
`Z:\home\debian\wex-g4\chromium-native-module-r2-20261024`.
Original helper366c6928… was observed unchanged before/after compilation.

Python runner's explicit `--module-warn-r2` selects only fixed R2 source stage
`/home/debian/wex-g4/chromium-native-module-r2-20261024-stage`, its
`original/native-exit.exe`, and fixed R2 output/profile/log/events. R1 remains
the default. Browser image/runtime, original HTTP fixture, eight normal browser
arguments, prefix, guards, parser, timeouts, quota and reap-before-prefix cleanup
remain. No executable/flag passthrough was added. Only Wine logging changes:
baseline `-all,+timestamp,+pid,+tid,+winediag,+process` plus
`,warn+module,trace+seh`; no trace+module. Existing1MiB helper stderr quota remains
fatal and preserves the primary failure. R2 helper bytes are observed without
mislabeling them as original366c. Diagnostic exit remains1, never acceptance.

Private build at
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-module-r2-20261024.build/`:

- `native-exit.exe`:306,537 bytes, AMD64 machine0x8664,
  SHA`b34ed44a3a819ff2a0d3e39428d08f3419e1fc79f668e7135fe51c571cb11fdf`.
- `build-results.json`:SHA`d861ffabd9f9b69f6dd1a7a25d1f56a681c820a665112d92243456995116d360`;
  exact compiler argv/version/hash, source/header/override hashes, timings,
  PE data and original-helper before/after observations. Build-time source
  snapshots and final CLI-test snapshot are distinguished and retained.
- Actual MinGW C++11/O2/static/municode crosscompile with
  `-Wall -Wextra -Werror`:exit0, empty stderr. Native PE was not executed.
  Logs, file identity/hash observations and final18-file seal remain private.

WSL Debian scripts cwd: `python3 -B -m unittest test_g4_native_exit
test_g4_chromium_native_exit test_g4_native_exit_calibration -v`:
**36 PASS,18.135s,no skips**. Actual host C harness compiles/runs BOTH default
and output-override contracts with warnings as errors, exercising schema,
quoting, status259/full32 and decision failures. Python tests execute actual CLI
selection, fixed paths/logging, parser and fake runner/cleanup callbacks in both
modes. `git diff --check` passes. These do not prove Win32 APIs/runtime behavior.

Stop for parent review. No private dispatcher/launcher change or fresh transfer
preparation was made; no live calibration repeat is needed by this plan.
Any later preparation/run requires separate authority and fresh guards. Source
remains in the C: graphics branch; M wine, D: master and other workers untouched.
