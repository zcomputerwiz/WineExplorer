<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Debian relocation all-clear and owned lab cleanup — 2026-10-21

User explicitly gave the all-clear to start Debian again and requested deletion
of large obviously unnecessary files, then asked to resume the graphics item.
The earlier relocation STOP is lifted. No VM/distro export/unregister/compaction
was performed, and no reset credit consumed.

Read-only inventory: Debian filesystem used396GiB; /var/tmp371GiB, of which
owned G4 evidence lab198GiB. Generated a dry-run manifest for62 obsolete stopped
Wine-prefix copies under /var/tmp/wex-g4-tree-20261020, each approximately1.8GiB.
Matched exact build identity, exact prefix path, saved cleanup remaining_servers=[],
registry markers and unchanged inode/registry time. Checked no live WINEPREFIX
process, no child mount; locked each consumer state and revalidated before deletion.
No prefix outside that one owned tree could be selected. All r4-* runs, private
runtime directories and profile/restart proofs were excluded. No root directory,
source tree, logs, screenshots, JSON results or profiles were deleted.

Applied the reviewed manifest with cleanup-owned-graphics-prefixes.py. Removed
119091859456 allocated bytes (about111GiB /119GB); filesystem used285GiB,
available672GiB afterward. Owned G4 tree now87GiB. Manifest and deletion audit:
/var/tmp/wex-g4-tree-20261020/cleanup-prefixes-20261021.json and
cleanup-prefixes-20261021.removed.json. Deletion is not directly recoverable;
the disposable Wine prefixes can be recreated with retained sources/runtimes.
Historical reports mentioning a retained prefix no longer imply that prefix
still exists for these62 old runs. All historical evidence remains.

Preserved the active combined-WoW64 build, compositor build/source, cached lawful
private runtimes, interrupted frozen-resize evidence, all current r4-* controls,
and all other workers' OOBE/token/VM trees (including the91GiB vmtest tree whose
ownership/current need is not established). No application or package cache
cleanup was performed. Host VHDX size/compaction was not modified.

Final inventory found no owned graphics/Wine/compositor/QEMU test processes.
Resume the one selected issue from RELOCATION_CHECKPOINT: inspect actual frozen
resize PNGs before interpreting the missing-blue diagnostic as a code defect.
