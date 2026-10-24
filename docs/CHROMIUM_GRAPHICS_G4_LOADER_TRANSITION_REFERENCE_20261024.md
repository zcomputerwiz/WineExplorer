<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Pinned loader source: candidate transitions, not a diagnosis

Parent read Git objects at the deployed Wine4315be83 revision, without modifying
the main D:checkout or rebuilding anything. Rejected R2 cmdline bytes remain
unavailable; none of the branches below is proven to have occurred in that run.

`dlls/ntdll/unix/env.c`:

- Lines513–535: rebuild_argv removes the loader's first argument. On a contiguous
  original argument region it moves strings and clears leftover bytes; otherwise
  it adjusts the argument-pointer array. This is a reason to retain raw bytes
  rather than guess from a generic mismatch.
- Lines543–602: prepend_argv can replace arguments and copy a bounded portion
  back into the original argument storage.
- Lines1911–1944: after main-image loading/classification, a nonzero status may
  select start.exe with /exec, rather than the simple rebuild branch. Conditions
  include alternate-loader handling. This is a possible DIFFERENT execution path,
  not merely a harmless title format; do not automatically accept it as the
  original native helper identity.
- Lines487–505 distinguish short process-name updates from argument rebuilding.

`loader/preloader.c` lines1371–1389 also shift argument strings and zero padding.
The observer's currently accepted exact argv_array/argv-without-loader and
flat-padded forms remain unchanged. Source possibilities alone do not establish
the actual rejected representation, why a branch ran, or Windows child execution.

Next action stays the offline rejected-identity evidence work order. Capture
only the recorded task, preserve failure, then parent reviews actual evidence
before deciding whether a narrow accepted rendering, loader problem or another
cause needs attention. No browser/native/API/graphics result follows source study.
