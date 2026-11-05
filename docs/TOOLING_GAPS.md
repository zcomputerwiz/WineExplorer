# Tooling gaps

Static assessment; sizes are rough engineering estimates, not commitments. The
actual wexbuild `/root` inventory was inaccessible and must be checked before
consolidation. `scripts/wexdev.sh` and `docs/WEXDEV.md` are in progress in a
separate task; coordinate with that work rather than duplicating it.

| Gap or overlap | Recommended action | Size | Agent type |
|---|---|---|---|
| Catalog coverage incomplete; `/root` read denied and many repo and guest fixtures remain unreviewed | Enumerate and read each entry point, dependencies, arguments, outputs, cleanup and runtime bounds; explicitly mark historical one-offs and verify the actual `/root` versions. | Large | Codex, with Qwen for bounded file inventories and independent verification |
| `dc2.sh` lab copy and `scripts/check-dcomp-surface-screen.sh` duplicate screen capture with differing formats and thresholds | Compare behavior and history, consolidate into one parameterized owned-process harness after preserving both use cases. | Medium | Codex |
| Fixed-display phase scripts and repeated global `pkill` | Replace with owned Xvfb/process groups, unique prefixes and explicit cleanup; preserve phase-specific options as fixtures. | Large | Codex |
| `/root` sync/build/visual scripts are invisible to repo agents and hardcode clone paths | After actual `/root` audit, port reusable entry points and documentation into the repo; archive clearly identified one-offs. Coordinate build functionality with in-progress wexdev. | Large | Codex |
| One-command rebuild plus selected test with meaningful exit status | Extend or compose the in-progress wexdev workflow once its interface is settled; avoid another competing wrapper. Cover WoW64 and preserve logs. | Medium | Codex |
| One-command guest deploy plus fixture run | Extend wexdev guest-push or add a reviewed companion with explicit VM, architecture, input hashes, ownership, rollback and result paths. Avoid hardcoded overwrite of shared runtimes. | Large | Codex |
| `summ*.sh`, `pull*.sh`, `run-validate*.sh` contain numbered experiment-specific copies | Build a parameterized results collector/summarizer accepting an explicit result directory and schema, retaining raw logs and failure states. | Medium | Codex; Qwen can inventory fields |
| Guest disk exhaustion | Add a free-space guard before prefix creation and deployment; report owned cleanup candidates without automatic deletion. | Small–medium | Codex |
| Bridge failures and escaped paths confuse small models | Add short tested examples and preflight checks for job state, edit mode, shell permission and paths; do not treat exit 0 as task success. | Small | Codex or Qwen, with Codex review |
| Visual results can be mistaken for presented-pixel evidence | Provide a common screen-pixel capture and fresh-prefix evidence manifest, with explicit ownership and environment metadata. | Medium–large | Codex |
