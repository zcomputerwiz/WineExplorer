#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# The start-up check (docs/SAFE_MODE.md), run by wex-startup-check.service. A start counts as successful once the
# logon screen is up: the Welcome screen (logonui.exe) or the GTK greeter for the lightdm account, a graphical
# session after an automatic logon, or, while Setup runs, a setup stage screen. It then clears GRUB's
# wex_boot_pending, so the next start does not show "WineExplorer did not start successfully"
# (etc/grub.d/11_wineexplorer_advanced sets it on every start). If the logon screen does not come up in time, the
# check fails and the value stays set. Vintner Boot counts a start as good at boot-complete.target
# (docs/VINTNER_BOOT.md section 4.4); the unit is ordered before it.
#
# Starts through the Advanced Options menu do not count (docs/SAFE_MODE_WORKORDER.md, decision 3); GRUB has
# already written the previous value back for them. The exception is Start Normally on the failure screen, which is
# the start the screen interrupted. The WEX_* variables let scripts/test_safemode.py use other paths.
cmdline_file=${WEX_BOOTMODE_CMDLINE:-/proc/cmdline}
bootmode=${WEX_BOOTMODE_HELPER:-/usr/local/bin/wex-bootmode}
stage=${WEX_BOOTMODE_SETUP_STAGE:-/etc/wineexplorer/setup/stage}
grubenv=${WEX_GRUBENV:-/boot/grub/grubenv}
limit=${WEX_STARTUP_TIMEOUT:-900}
interval=${WEX_STARTUP_INTERVAL:-2}

cmdline=" $(cat "$cmdline_file" 2>/dev/null) "
mode=$("$bootmode" 2>/dev/null) || mode=normal
case "$cmdline" in
    *" wexmode="*)
        case "$cmdline" in
            *" wexfailed=1 "*)
                if [ "$mode" != normal ]; then
                    echo "not counted: $mode, chosen on the failure screen"
                    exit 0
                fi
                ;;
            *)
                echo "not counted: started through the Advanced Options menu ($mode)"
                exit 0
                ;;
        esac
        ;;
esac

found=
setup_screen() {
    [ -e "$stage" ] || return 1
    for unit in wex-setup.service wex-oobe.service wex-textsetup.service; do
        if systemctl -q is-active "$unit" 2>/dev/null; then found="the setup screen ($unit)"; return 0; fi
    done
    return 1
}

# The program itself, not a command line that names it: Explorer's "/desktop=Logon,... logonui.exe" runs before the
# Welcome screen exists. Wine shows a program's command line with or without its Windows path.
logon_screen() {
    if pgrep -u lightdm -f '^([^ ]*[\/])?logonui\.exe( |$)' >/dev/null 2>&1; then
        found="the Welcome screen"
    elif pgrep -u lightdm -f '^([^ ]*/)?lightdm-gtk-greeter( |$)' >/dev/null 2>&1; then
        found="the GTK greeter"
    else
        return 1
    fi
}

graphical_session() {   # an automatic logon skips the logon screen; a serial or text console logon does not count
    for session in $(loginctl list-sessions --no-legend 2>/dev/null | awk '{ print $1 }'); do
        case "$(loginctl show-session "$session" -p Class --value 2>/dev/null)/$(loginctl show-session "$session" -p Type --value 2>/dev/null)" in
            user/x11|user/wayland) found="a desktop session (session $session)"; return 0 ;;
        esac
    done
    return 1
}

waited=0
until setup_screen || logon_screen || graphical_session; do
    if [ "$waited" -ge "$limit" ]; then
        echo "the logon screen did not come up within $limit seconds: this start counts as failed"
        exit 1
    fi
    sleep "$interval"
    waited=$((waited + interval))
done

if [ -f "$grubenv" ] && command -v grub-editenv >/dev/null 2>&1; then
    grub-editenv "$grubenv" unset wex_boot_pending || { echo "could not clear wex_boot_pending in $grubenv"; exit 1; }
fi
echo "$found is up after about $waited seconds: this start counts as successful"
exit 0
