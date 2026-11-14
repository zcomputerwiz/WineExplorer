<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# Lab helper scripts of the UWP track

Copies of the scratch scripts that drove the owned U1 guest (saved 2026-11-13 so that they are not lost with the
session's temporary folder). They are not a maintained harness: paths of the session's scratch folder are written
into `lab.sh` and `u11-fixture.sh` and have to be changed to where the scripts are run from. `docs/uwp/RESUME.md`
says how they are used.

* Host side (Git Bash on Windows): `lab.sh` (`ssh`, `push`, `delta`, `fixture`), `u11-fixture.sh`.
* Guest side (pushed to `claude/` in the guest workspace): the `guest-*.sh` scripts, `fastbuild*.sh`,
  `test-wexappx.sh` and the rest.
