# Upstream Wine fixes

P1 acceptance passed: all twelve candidates build and their regression suites
pass on the isolated 64-bit VM. Branches are based on fork master
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
| [upstream/wizard-focus](https://github.com/zcomputerwiz/wine/tree/upstream/wizard-focus) | `94a5845ccd86f34ec858ae0004cc67190b517432` | comctl32 and comctl32_v6 build; full propsheet suite passes under Xvfb. Unpatched upstream fails eight new focus assertions. |
| [upstream/drawtext-hideprefix](https://github.com/zcomputerwiz/wine/tree/upstream/drawtext-hideprefix) | `72feeed7325cd6b8122eac756c6d96d7d0b7f90a` | user32 builds; full text suite passes under Xvfb. Unpatched upstream renders seven unwanted underline pixels. |
| [upstream/folder-iconfile](https://github.com/zcomputerwiz/wine/tree/upstream/folder-iconfile) | `d942c4f40647c599e01d4b8a4accdcc5420d37b1` | shell32 builds; full shlfolder suite passes under Xvfb. Unpatched upstream fails both literal and environment-expanded icon-path assertions. |
| [upstream/rename-source-notification](https://github.com/zcomputerwiz/wine/tree/upstream/rename-source-notification) | `f7038cc7d9b5077db349318b26b0d9ff9e294a52` | shell32 builds; full shlfolder suite passes under Xvfb. Unpatched upstream fails eight destination-watcher assertions. |
| [upstream/power-capability-battery](https://github.com/zcomputerwiz/wine/tree/upstream/power-capability-battery) | `aee72c042a96f06a6e91d2d837b1494d263d7ed2` | ntdll builds; full info suite passes with synthetic battery and no-battery sysfs views. Unpatched upstream fails the battery-present assertion. |
| [upstream/shortcut-target-icon](https://github.com/zcomputerwiz/wine/tree/upstream/shortcut-target-icon) | `794d2015441eb02df809218bc7b5ba7422c4b240` (fix `23e9bae`) | shell32 builds; full shelllink suite passes, including a self-referencing link. Unpatched upstream fails the folder target icon assertion. |
| [upstream/ie-desktop-icon](https://github.com/zcomputerwiz/wine/tree/upstream/ie-desktop-icon) | `1fab750bcaac50f29fe87c83b16058b22f85acd1` | shdoclc and shell32 build; full shelllink suite passes after isolated prefix refresh. Unpatched upstream fails three resource-190 assertions. |
| [upstream/theme-token-whitespace](https://github.com/zcomputerwiz/wine/tree/upstream/theme-token-whitespace) | `acfcdd57b02e12f91c9f921cbb72b6d4065dc1ad` | uxtheme builds; full system suite passes. Unpatched upstream fails three font-token assertions. |
| [upstream/timezone-cache](https://github.com/zcomputerwiz/wine/tree/upstream/timezone-cache) | `eaf8a9edca9bcdb556b5c466f8ea30dd22ad5498` | ntdll builds; full time suite passes with isolated same-offset rule changes. Unpatched upstream retains stale DST rules. |
| [upstream/server-dst-bias](https://github.com/zcomputerwiz/wine/tree/upstream/server-dst-bias) | `5372d43d2f15c96582ce6756d7b4f09007d1de38` | server builds; full ntdll time suite passes with winter and summer warm starts. Unpatched upstream fails both transition assertions. |

Validation uses a clean **64-bit** upstream build on the Proton build VM,
not WineExplorer's modified runtime. Native Windows and 32-bit/WoW64 runs have
not been performed. First-prefix creation in the 64-bit build prints a missing
syswow64/rundll32 warning; the regression suites still pass. X connection
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

### Wizard navigation focus — ready-to-paste MR description

Changing wizard navigation buttons can hide or disable the focused button,
leaving keyboard focus on an unavailable control. When this happens, move
focus to the current enabled, visible default button. Preserve valid button
focus and focus within the page.

The regression checks Next/Finish/Back/Cancel transitions and preservation of
page-edit focus, with both v5 and v6 common controls.

Validation: built comctl32/comctl32_v6 on current upstream; the full propsheet
suite under Xvfb fails eight new assertions without the fix and passes with it.

### DrawText hidden prefix — ready-to-paste MR description

DrawText removes the prefix ampersand with DT_HIDEPREFIX, but still draws the
mnemonic underline. Skip underline drawing when that flag is set.

The regression compares the rendered RGB pixels of hidden-prefix text with
plain text. A visible-prefix control must produce different pixels, confirming
that the rendering comparison detects the underline.

Validation: built user32 on current upstream; the full text suite under Xvfb
fails the new assertion with seven differing pixels without the fix and passes
with it.

### Custom folder icon path — ready-to-paste MR description

IExtractIconW reads desktop.ini's IconFile and IconIndex, but leaves the output
icon path unset. Return IconFile after expanding environment variables, with
the existing literal path as a fallback if expansion fails.

The regression creates a temporary system/read-only folder with desktop.ini,
obtains IExtractIconW through its parent shell folder, and checks literal and
environment-expanded icon paths plus the negative resource index. It restores
the test environment variable and removes the temporary folder.

Validation: built shell32 on current upstream; the full shlfolder suite under
Xvfb fails both new icon-path assertions without the fix and passes with it.

### Rename notifications — ready-to-paste MR description

SHChangeNotify's source-item condition also includes two-item events, making
its destination-item branch unreachable. Renames into a watched folder then
produce no notification. Check both source and destination for two-item events
and retain the source-only check for one-item events.

The regression covers file and folder renames into, out of, and within the
watched folder. It verifies both PIDLs and event types with ANSI and Unicode
paths, using legacy and shared-memory notification delivery. Within-folder
renames must deliver exactly one notification.

Validation: built shell32 on current upstream; the full shlfolder suite under
Xvfb fails eight destination-watcher assertions without the fix and passes
with it. Source and within-folder cases pass before and after the fix.

### Battery capability — ready-to-paste MR description

SystemPowerCapabilities always reports SystemBatteriesPresent as false, even
when SystemBatteryState detects a system battery. Use the existing platform
battery-state helper to fill that capability. Initialize its input structure
because platforms with no battery leave BatteryPresent untouched.

The regression checks consistency between both NtPowerInformation results.
Validation: built ntdll on current upstream and ran the full info suite under
Xvfb with both battery and no-battery synthetic sysfs views. The battery case
fails the new assertion without the fix; both states pass with it. The fixture
uses a private Linux mount namespace, runs Wine as the normal guest user, and
does not alter real hardware state. Non-Linux and native Windows runs remain
unperformed. The existing noexec-related skip occurs in both runs.

### Shortcut target icon — ready-to-paste MR description

A shortcut without an explicit icon falls back to shell32 icon zero rather
than its target's icon. Obtain IExtractIconW for the target and use its icon
location. Explicit shortcut icons retain priority. Avoid recursively following
link targets, which may form cycles; those retain the existing fallback.

The regression saves shortcuts with fully parsed target PIDLs, checks a folder
target against its actual icon location, covers an executable target, and
verifies an explicit icon override. Validation: built shell32 on current
upstream; the full shelllink suite under Xvfb fails the folder assertion without
the fix and passes with it. The executable case is a preservation control on
this prefix, whose exe association returns the generic icon.

The separate SetPath simplified-PIDL folder-type issue remains outside this
branch. A follow-up regression saves a self-referencing shortcut and checks
that icon lookup completes with a nonempty fallback path; the full shelllink
suite passes with that case. Native Windows has not been tested.

### Internet Explorer desktop icon — ready-to-paste MR description

iexplore.inf registers shdoclc.dll resource 190 as the Internet Explorer
desktop item's default icon, but that resource is missing. Define it using an
unchanged copy of Wine's existing programs/iexplore/iexplore.ico. No external
art is introduced.

The regression calls ExtractIconExW for resource 190 at large and small sizes
and checks both handles. Validation: built shdoclc and shell32 on current
upstream; the full shelllink suite under Xvfb fails three assertions without
the resource and passes with it. The isolated prefix is refreshed with
wineboot -u before both phases, so file-based extraction reads the DLL just
built. Installed runtimes are untouched. Systems lacking shdoclc skip the case.

### Theme font tokens — ready-to-paste MR description

Comma-separated theme tokens retain spaces preceding the comma, and the final
token can lose its last character when followed by whitespace. Trim from the
character preceding the token end, bounded by the token start.

The regression builds an original text-theme fixture in a temporary copy of
the test image using UpdateResource. It compares normal and spaced font names
and bold/italic/underline/strikeout attributes through GetThemeFont, then
restores the prior theme and deletes the fixture. No existing theme art or
external resources are used. Validation: built uxtheme on current upstream;
the full system suite under Xvfb fails three new assertions without the fix
and passes with it. Platforms rejecting unsigned text themes skip the case;
native Windows has not been tested. Existing display/DPI/light-theme skips
occur in both runs.

### Timezone cache invalidation — ready-to-paste MR description

ntdll caches timezone rules by year and UTC bias. Replacing /etc/localtime
with a zone having the same standard offset but different DST rules leaves
the old rules cached. Invalidate when the file's device, inode or mtime changes.

The regression queries RtlQueryTimeZoneInformation in one process before and
after externally coordinated rule changes. The Linux fixture compiles two
original zones with the same standard offset, switches DST to fixed-offset
and back in a private mount namespace, and runs Wine as the normal guest user.
Validation: built ntdll on current upstream; the full time suite under Xvfb
fails the stale-DST assertion without the fix and passes with it. A SHA256
check confirms the guest's actual /etc/localtime remains unchanged. The test
skips when the optional fixture environment is absent; native Windows has not
been tested. Fixture: scripts/refactor/timezone-fixture.sh in WineExplorer.

### Server DST bias — ready-to-paste MR description

The server caches its DST-adjusted shared timezone bias by year and standard
offset. Crossing a DST boundary within the same year leaves that value stale.
Recompute when tm_isdst changes as well.

The regression reads KUSER_SHARED_DATA.TimeZoneBias while an isolated server
clock changes between January and July 2026. It tests winter→summer→winter
and summer→winter→summer. Validation: built server on current upstream; the
full ntdll time suite under Xvfb fails the two transition assertions without
the fix and passes with it. A server-only preload shim controls time(); real
system time, monotonic clocks and the installed desktop remain untouched.
The optional fixture uses a process-local POSIX TZ rule and skips when absent.
Fixture code is in WineExplorer scripts/refactor/test-dst-topic.sh,
server-clock.sh and fake-server-time.c. Native Windows has not been tested.

## Evaluated candidates retained in the fork

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

For a resource fix in another module, optional build-targets lists additional
dlls/<module>/all targets; update-prefix refreshes only the isolated prefix
before each phase. For hardware fixtures, optional test-wrapper.sh runs the
same suite in each synthetic state. New patch files are staged before commit.

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

## Wine Wayland zones — 2026-10-13

Branch: upstream/winewayland-zones, commit aed8b9599c46db289fd8f193e045e1ad11628bad,
based on upstream master 081042ede6a165a36bf9988350dd584b440d7a3d.
Submission remains manual; no WineHQ MR has been opened.

Proposed title: winewayland: Position toplevel windows in shared zones when supported.

Proposed description: Bind optional xx-zones-v1 and place Wine toplevels in one
coordinate space shared across processes. A volatile per-socket registry value
publishes the opaque handle; a named mutant serializes creation, accepting
informational existing-object and abandoned-wait results. Terminal-launched
programs join without an inherited token. Place before the first mapping commit
and on geometry changes, account for surface scale/frame extents, process
position feedback and rejoin/re-place on zone changes. Keep existing behavior
when the protocol is absent. Vendor the MIT XML unchanged; new driver code is
available under LGPL-2.1-or-later or GPL-2.0-or-later.

Validation: Wine Wayland driver builds on Debian 13; six stationary windows match
X11 captures pixel-for-pixel, and a second process joins the same handle and
places its seventh window exactly at (740,50). Maintained probes:
scripts/wayland-position-probe.c and scripts/check-wayland-positions.py.
The compositor's existing protocol suite passes 25/25. Final integration
acceptance, stock-labwc fallback and fractional-DPI coverage remain in progress
in WAYLAND_L1.md; this topic is prepared but not yet marked fully submission-ready.
