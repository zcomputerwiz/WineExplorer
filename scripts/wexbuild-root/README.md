<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# Snapshot of the wexbuild /root scripts (2026-11-05)

Read-only mirror of `/root/*.sh` in the WSL distro `wexbuild`, copied so that agents that cannot enter that distro
(Codex gets `E_ACCESSDENIED`) can read them. The live copies run from `/root` and may have changed since; many are
one-offs. They are documented in `docs/TOOLING.md`. Do not run them from here: they assume the paths
`/root/WE-prefetch/...` and several kill every wineserver and Xvfb.
