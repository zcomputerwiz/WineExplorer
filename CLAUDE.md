# WineExplorer

Windows XP / Server 2003 shell on Wine for Debian. Docs live in `docs/` (start with `HANDOFF.md`, `ROADMAP.md`,
`STOCK_CHANGES.md`, `FILE_EXPLORER.md`).

## Rules

- Pixel and functional parity with the XP VM; XP names; gray out what does not apply to Wine.
- Keep changes to stock Wine small and list each in `docs/STOCK_CHANGES.md`.
- Prefer D-Bus (wexhost) over spawning tools.
- Commit after each working step.
- License: GPL-2.0-or-later as a whole (`LICENSE.md`). New files get the GPL header (`scripts/add-license.py`); never
  change the notices of Wine or ReactOS files. No code under GPLv2-incompatible licenses (Apache-2.0, GPL-3.0-only).

## Editing

Never pass C escapes (`\\`, `\n`, `'\\'`) through Bash heredocs, `sed`, or `python -c`: they get collapsed or turned
into real newlines. Use the Edit tool, or Write a patch script to a file and run it. Grep the touched lines after
any scripted edit.

# Compact instructions

Keep, verbatim: every command or edit that failed and its exact cause (quoting/escaping, sync, races, build
errors), the workaround that worked, and any VM or session state left behind (powered off, shell not running,
files not synced). Keep the user's standing instructions and open requests. Drop routine successful build and
test output.
