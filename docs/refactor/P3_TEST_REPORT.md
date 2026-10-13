<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# P3 validation — October 13, 2026

The public plugin/host build passes in /srv/protonexplorer/wex-refactor-p3.
The restored stock host changes 46 lines across desktop.c/explorer.c, plus
the shared version header and build entries. The DLL contains the moved shell
implementation and resources. Required exports and rejection of invalid
versions, null desktop and desktop commands following other switches pass.
Disabling the DLL retains the stock tray without plugin clock/task controls.

P2 and P3 desktop/tray/native-dialog checks pass. Both Start styles, My Computer
and Turn Off screenshots are identical above the taskbar; the clock differs.
The eight native-dialog placement cases pass. Linked src-module builds and
repeated assembly pass. On WineExplorer-VM, the repaired full build passes,
the session was installed/restarted and process maps confirm wexshell.dll.
The live API/control probe reports zero failures. No future-dated artifacts
remain after removing stale generated files from the earlier date tests.

Composition probes pass on P2 and P3, with composition disabled and enabled:
color-key holes pass input to the desktop; opaque ring and blue rectangle
receive their clicks; all six blend operations succeed and preserve styles
and attributes; compatible/DIB/scaled capture succeeds; resize and fullscreen
transitions work. Captures were visually inspected for alpha/color-key output.
The independent owner-process probe completes five disable/enable cycles plus
two manual toggles with matching requested/actual states and zero API errors.
This supplements the real-shell tests, since that probe can classify its own
controls as foreign clients. Visible popup shadows remain a baseline Xvfb
limitation: sampled pixels outside the popup show no shadow in the capture.
This extraction does not change the compositor or its driver hints.

The public runtime installed into isolated staging with prefix=/usr/local
passes the same tray/native-dialog checks from its relocated loader/server.
The ISO was built with that public source, without the dev VM's local artwork.
SHA-256:

`d7fb384656b5b2060dbdb1b26570979cbc530b52736ce498bf6dea195079d26c`

installer/tests/install-interactive.sh passes on a new QEMU/KVM disk:
partition/format/copy, GUI setup, OOBE, wrong-password rejection, Alice's
password login, lock/switch, Bob's passwordless login and prefix isolation.
Installed disk checks verify LightDM/custom/recovery greeters, session,
automatic logon, names/password state, administrator membership and setup
cleanup. Bob's desktop appeared in five seconds. VM p3-plugin shut down
normally; its evidence is /var/tmp/wineexplorer-vmtest/p3-plugin/steps in WSL.
The ISO and host test log are ignored build/refactor artifacts.

The ISO validates the extraction before the final task-band description
ownership adjustment; that adjustment only selects the plugin's existing
version resource for grouped Explorer captions. Its module build passes and
a 500-pixel desktop with three distinct directory windows shows the grouped
"3 Windows Explo..." caption, preserving the original description. The
executable's stock metadata stays unchanged.
No EFI/physical-machine, 32-bit or non-Wex-compositor validation is claimed.
