<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 resize-border diagnostic interrupted for Debian relocation

User needs to relocate Debian WSL's approximately500GB virtual disk. **Do not
start or access Debian WSL until the user gives the all-clear.** No WSL shutdown,
export, unregister, deletion, compaction or relocation was performed by this chat.
Existing VM/Qwen/OOBE work was not touched.

Parent before this slice bc188066; Wine571e907 and compositor94faa651 unchanged.
Current item is ONLY the transient native resize-border issue. No production
fix yet, no merge and no G4-complete claim.

## Saved diagnostic work (not a successful real-consumer acceptance)

Added opt-in `--resize-border` to the existing native Electron lifecycle runner:
freeze DOM updates before final growth; hold the fixture at resize_back and
take independent grim output samples at offsets0,.25,1,2,4 seconds. A read-only
oracle checks default unthemed scale1 frame rims against the independently read
HWND rectangle and requires the original40x40 blue square (1600 pixels) too.
The bounded settled assertion requires the last two samples; early mismatches
are retained. This is not atomic resize certification or installed-theme proof.
Default lifecycle fixture behavior is unchanged unless the new flag is selected.
Three synthetic oracle unit tests and11 existing consumer-harness tests PASS.
No production Wine/compositor code was edited or rebuilt.

Owned runs in `/var/tmp/wex-g4-tree-20261020`:
`r4-frozen-resize-first-vulkan`, `r4-frozen-resize-first-gl`.
Both reached the held capture sequence before the relocation request. Saved
results show blue_pixels=0 in later samples despite complete rims; Vulkan early
.25 sample had1600 blue pixels but missing right/bottom rims. This is important
unresolved evidence: inspect actual saved PNGs after relocation, validate the
oracle against the known fixture, then trace native backing/retained presentation
if content really disappears when DOM is frozen. Do not call this fixed from
the frame rims or RAF progression. No screenshot inspection was completed in
this interrupted slice. The real runs are interrupted/failed, not acceptance.

Runner sources synced to owned `clean-source/scripts`, fixture main.js synced
to `clean-source/scripts/g4-electron-probe/`. Wine build remains
`/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64`; exact owned compositor,
runtime/config paths remain in NATIVE_GEOMETRY/NATIVE_BRIDGE reports. After move,
verify path availability rather than recreating any lab or downloading payloads.

## Explicit cleanup and failure evidence

On the user's relocation request stopped ONLY these two prefixes with:
`env WINEPREFIX=/var/tmp/wex-g4-tree-20261020/r4-frozen-resize-first-RENDERER/prefix
/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64/server/wineserver -k`.
The two owned runners then exited. Vulkan reported host_exit1 and cleanup error
`RuntimeError: Owned servers remain: [1276]`; GL host exited0 but diagnostic
remained failed/interrupted (not its full60-second acceptance).
An owned helper/cleanup shutdown race could have briefly recreated the server.
Repeated Vulkan's exact-prefix `wineserver -k`, followed by `wineserver -w`.
Final `ps -eo pid,args | grep -E
"clean-source/build/wow64|wexcomp-build-acc46fe/labwc|r4-frozen-resize|Xvfb"`
matched only the inventory shell/grep themselves. **No owned Wine server,
compositor, probe runner or Xvfb remained.** No test processes were left running.
Do not interpret the interrupted cleanup race as a proven renderer defect.

Only host-side checkpoint/Git actions followed that final WSL inventory. No
reset credit consumed. Last usage reading was19% primary remaining before the
diagnostic; recheck on resume. Relocation, not usage, is why work stopped.
Do not restart a continuation automation into Debian during relocation.
