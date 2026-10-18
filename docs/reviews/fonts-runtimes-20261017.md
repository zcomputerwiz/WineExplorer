<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fonts/runtimes branch review, 2026-10-17

**Decision: do not merge.** Reviewed `gemini/fonts-runtimes` at
`dfd42ab03c95215e0013c7c3f3b8dad6e5897312` against its merge base with master.
Master remains at `1fa63233`, which already contains the reviewed Luna merge
and its pre-publication art audit. No fonts/runtimes implementation was
changed or merged during this review.

## Merge blockers

1. **P1: the per-user graphics switch changes the shared machine files.**
   `scripts/wex-graphics.sh:56-89` moves and replaces DLLs under
   `$WINEPREFIX/drive_c/windows/system32` and `syswow64`. In the supported
   prefix-split layout, `drive_c/windows` is a symlink to the shared machine
   root. A writable administrator prefix therefore changes the DLLs seen by
   every other user. A normal user's non-writable directories instead skip
   installation after native-only overrides have already been written.
   Both enable and disable must operate on genuinely private per-user DLLs,
   and report failures rather than claim success with missing native files.
   Reproduced with two temporary split prefixes and dummy DLLs: enabling in
   Alice changes Bob's `system32/d3d11.dll` to the same DXVK symlink; disabling
   changes the shared file back. The script also processes `syswow64`.

2. **P1: acceptance screenshots include prohibited Microsoft art.**
   All four new captures under `docs/fonts-runtimes/` show the Windows-logo
   Start button and the Bliss wallpaper. These are not the clean open-art
   captures required by `CLAUDE.md` and the work order. Retake WordPad 32/64,
   Mono and Gecko evidence in a fresh open-only prefix or installed image
   and remove the contaminated images from the proposed integration.
   Do not treat Microsoft's freely licensed Selawik font as the problem:
   that font is explicitly permitted by the work order; the captured shell
   artwork is the issue.

3. **P1: Windows Components permits non-root administrators but never elevates
   the actual installer.** `scripts/wex-components.sh:11-21` accepts membership
   in `sudo`. Cache creation can invoke sudo, but lines 95-100 and 135-144 run
   winetricks as the original user with the root-owned machine root as
   `WINEPREFIX`. The desktop launcher does not elevate either. Wine's unchanged
   pinned `ntdll/unix/server.c:1346` rejects a prefix whose owner is not the
   calling UID, irrespective of group write permission. A fixture advertising
   UID 1000 and sudo-group membership reached winetricks without an elevation
   call. The acceptance log only tests installing via `sudo`, which misses
   the actual desktop-launch path. Authenticate/elevate the installation
   operation explicitly, and test the desktop entry as a normal admin account.

4. **P1: graphics on/off waits for the logged-in desktop to exit.**
   `scripts/wex-graphics.sh:53,105` invokes `wineserver -w` against the live
   user's prefix. That waits for all Wine clients, including the desktop
   shell, rather than merely for a registry update. The DLL links are changed
   only after the wait. A bounded fixture with an active-server wait returned
   timeout status 124 at that exact command, before any DLL links changed.
   Remove that lifecycle dependency; test the command while Explorer and an
   application are still running.

5. **P2: failed component installers are reported as successful.**
   `scripts/wex-components.sh:95-107` discards the failing pipeline with
   `|| true` and then unconditionally displays the success dialog. A fake
   winetricks returning 42 still produced the success message and script
   status 0. Preserve the install status and display/report the failed verb.

6. **P2: the CLI bypasses the supported-component boundary.**
   `scripts/wex-components.sh:135-144` passes arbitrary arguments directly to
   winetricks. The listed supported verbs are not an allowlist: a temporary
   fixture accepted `install dotnet48` and called `winetricks -q -f dotnet48`.
   The same path can request verbs the work order explicitly excludes or
   core-library overrides. Validate verbs against the supported list before
   accepting terms or running installation. The current dialog also presents
   a generic licence notice, not the selected vendors' actual terms required
   by the work order.

## Acceptance evidence does not establish completion

`docs/FONTS_RUNTIMES.md:7` claims all acceptance criteria passed, but the
provided test runner does not establish that claim:

- `scripts/test_fonts_runtimes.py` runs against an existing development
  prefix, uses fixed displays and shared-folder paths, and does not perform
  the required fresh offline X11 and Wayland installations.
- Its font test prints `fc-match` output without asserting that the requested
  exact family exists. Fontconfig returns a fallback with success status for
  missing families, so this is insufficient for the work order's `fc-list`
  requirement. The line-wrap claims have no measured reference comparison.
- Mono/Gecko and graphics status labels are largely fixture text. No network
  isolation, D3D11 sample/DXVK renderer proof, or both-session runtime checks
  are shown in the script. Mono's shown program identifies itself as 64-bit.
- Several commands swallow errors while the runner prints an unconditional
  all-verifications-successful banner.
- The runner executes global `killall -9` against Wine, Explorer and Xvfb,
  and writes the current user's prefix. It is not safe to run on a shared
  development host as an ordinary acceptance command. Isolate processes,
  prefixes, displays and cleanup to resources created by that run.
- The reported ISO growth is 397.7 MiB, beyond the work order's approximately
  150 MB threshold, without the requested concrete trimming proposal.

The correct pinned runtime versions are a positive finding: the unchanged
Wine pin `ed5fe967ed471ca524666631ad70dabbed6af076` expects Mono 11.3.0 and
Gecko 2.47.4, matching the package script. Download URLs have SHA256 pins,
and the font data includes the symbol-family spelling correction. These
do not resolve the functional and evidence blockers above.

## Verification performed

Read the complete work order, changed packaging/setup/session code, both new
management scripts, complete acceptance runner, logs and all four captures.
Six changed shell scripts pass `bash -n`. No proprietary components were
downloaded or installed, and no real user prefix was changed.

The independent fixture uses fake Wine, winetricks, zenity, vulkaninfo and
wineserver commands with temporary prefixes only. It ran under local Debian;
the branch's destructive full acceptance runner was not executed. Observed
output:

```text
REPRODUCED: Alice enabling Vulkan changes Bob's shared system32/d3d11.dll (both architectures modified).
REPRODUCED: off also restores the shared DLLs for all prefixes, not just Alice.
REPRODUCED: enable stops at wineserver -w before DLL linking when a prefix server remains active (timeout exit 124).
REPRODUCED: failed installer (exit 42) still produces success GUI and exit 0.
REPRODUCED: a sudo-group UID 1000 is passed directly to winetricks without elevation.
REPRODUCED: unsupported dotnet48 verb is accepted; no component allowlist protects built-in runtime policy.
PASS: six changed shell scripts parse. The branch acceptance runner was NOT executed (global killall/live-prefix writes).
```

The root-ownership failure is established from the unchanged Wine source and
the setup script's explicit root ownership; no installer was run against an
actual shared root. The active-server wait was simulated, bounded and checked
for timeout status; no live desktop was blocked.

The managed review worktree is retained on this chat for follow-up. The only
main-worktree additions are these review notes; the pre-existing `.gitignore`
edit remains untouched. No new VM or session was started for this review.

## Subsequent authorized implementation

The user subsequently requested a clean branch from master, salvaging sound
parts and fixing the issues. That work is on `codex/fonts-runtimes`, not an
integration of the rejected branch. Its implementation, independent evidence
and remaining acceptance gates are in [FONTS_RUNTIMES.md](../FONTS_RUNTIMES.md).
The preceding review and its historical VM-state statements describe the
review phase, not the later repair tests.
