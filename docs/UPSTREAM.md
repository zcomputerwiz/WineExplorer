# Upstream Wine fixes

P1 is in progress. Branches are based on fork master
`081042ede6a165a36bf9988350dd584b440d7a3d`. No WineHQ merge requests have been
submitted. The user opens them manually and supplies final real-name authorship
and sign-off. Prepared commits use the existing Git identity `zcomputerwiz`
without sign-off or AI co-author trailers; amend that identity as appropriate
before submission.

## Validated and pushed

| Branch | Commit | Build/test evidence |
|---|---|---|
| [upstream/imagelist-alpha](https://github.com/zcomputerwiz/wine/tree/upstream/imagelist-alpha) | `985db3efad8382b5a72063f6be61ccc8f5374aa4` | comctl32 and comctl32_v6 build; full imagelist suite passes under Xvfb. Unpatched upstream fails six new assertions. |
| [upstream/empty-recycle-bin-icon](https://github.com/zcomputerwiz/wine/tree/upstream/empty-recycle-bin-icon) | `00d5a25fd4b118195f144553081b3c68e33c485e` | shell32 builds; full shelllink suite passes under Xvfb. Unpatched upstream fails the empty Recycle Bin icon assertion. |

Validation uses a clean **64-bit** upstream build on the Proton build VM,
not WineExplorer's modified runtime. Native Windows and 32-bit/WoW64 runs have
not been performed. First-prefix creation in the 64-bit build prints a missing
syswow64/rundll32 warning; both regression suites still pass. X connection
shutdown messages after Xvfb exits are expected.

### Image-list alpha replacement — ready-to-paste MR description

When a 32-bit alpha icon is replaced by a 32-bit icon with no alpha, the image
list retains ILIF_ALPHA from the previous image. Drawing the replacement then
produces invisible pixels. Update the item's alpha flag for both outcomes of
alpha detection.

The regression test alternates alpha/non-alpha icons in the same slot, checks
the rendered pixels and v6 GetItemFlags, and verifies that the neighboring
image and image count are unchanged. It runs with v5 and v6 common controls.

Validation: built comctl32/comctl32_v6 on current upstream; the imagelist suite
under Xvfb fails six new assertions without the fix and passes with it.

### Empty Recycle Bin icon — ready-to-paste MR description

SHGetStockIconInfo maps SIID_RECYCLER to shell32 icon resource 32, but the
resource is missing. The call succeeds with a null icon handle, leaving an
empty Recycle Bin without an icon. Define resource 32 using Wine's existing
trash_file.ico.

The existing stock-icon test now requires SIID_RECYCLER to return a non-null
handle instead of treating that missing icon as a todo. No external art is
introduced.

Validation: built shell32 on current upstream; the shelllink suite under Xvfb
fails the icon-handle assertion without the resource and passes with it.

## Remaining candidates

| Topic | State/constraint |
|---|---|
| Wizard focus | Local topic drafted; regression run in progress |
| Server DST bias | Needs deterministic timezone-transition regression |
| ntdll timezone cache | Needs localtime-change regression with host state isolated |
| Power capability battery presence | Use stock battery-state helper; test battery/no-battery cases |
| Folder custom IconFile | Independent two-line fix; needs folder attribute/icon regression |
| Shortcut target icon | Needs file/folder/explicit-icon fallback coverage |
| Rename notifications | Needs source/destination watcher coverage |
| DrawText DT_HIDEPREFIX | Needs rendering regression in user32 tests |
| Theme token whitespace | Needs a compatible, project-authored theme fixture |
| Internet Explorer desktop icon 190 | Use Wine's own existing icon; needs resource/API regression |

FileIconInit and endpoint volume remain fork patches pending smaller, complete
API implementations. Shell-hook registration currently supports one receiver,
so it is not ready for upstream. SHFindFiles depends on our Search Results
handler and ignores its folder/saved-search inputs; it becomes a project hook.
See `REFACTOR_MAP.md` and `refactor/REVIEWS.json` for exact hunk targets.

## Reproducing topic validation

The isolated VM checkout/build are `~/wex-refactor/upstream` and
`~/wex-refactor/build`. A full clean build completed using:

```text
CFLAGS='-O2 -pipe' CXXFLAGS='-O2 -pipe' ../upstream/configure --enable-win64 --prefix=/home/wineuser/wex-refactor/install
make -j8
```

No `make install` was run. Never install from `~/inst/build`.

`scripts/refactor/test-topic.sh` consumes `topics/<topic>/tests.patch`,
`fix.patch` and `failure-pattern` beneath the isolated VM root. Tests are
exported separately so they can first run against unpatched upstream. It
checks the base revision/clean checkout, holds a checkout/build lock, uses
an isolated Wine prefix and Xvfb, requires the expected baseline assertion
failure, rebuilds the changed module(s), and commits only after the fixed
suite passes. It does not push branches or submit merge requests.

Only one runner may use this checkout/build at a time. After accepting a
topic, switch back to master and run make before starting another, so the
previous fix is removed from the test runtime. Reusing an already committed
topic requires a separate clean worktree/build; the runner refuses to rewind
the existing branch.

Logs, exit codes and commit ids live in
`~/wex-refactor/results/<topic>/`. The host bare repository is
`build/refactor/wine.git`; sparse topic worktrees are local and ignored.
Fetch validated commits over the existing SSH connection and push from the
host, keeping GitHub credentials off the guest.
