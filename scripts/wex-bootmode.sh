#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Prints this boot's WineExplorer mode: normal, menu, safe, safenet or safecmd (docs/SAFE_MODE.md); with --option,
# the menu's start option instead.
# The mode lives only in /run, as Windows keeps it in a volatile registry key: the Advanced Options menu writes
# /run/wineexplorer/bootmode, otherwise wexmode= on the kernel command line decides. Installed as
# /usr/local/bin/wex-bootmode; the generator, the PAM check, the greeter and the session ask it.
# The WEX_BOOTMODE_* variables let the tests point it at other files.
state=${WEX_BOOTMODE_STATE:-/run/wineexplorer/bootmode}
option_state=${WEX_BOOTMODE_OPTION_STATE:-/run/wineexplorer/bootoption}
cmdline=${WEX_BOOTMODE_CMDLINE:-/proc/cmdline}
setup_stage=${WEX_BOOTMODE_SETUP_STAGE:-/etc/wineexplorer/setup/stage}
root_uid=${WEX_BOOTMODE_ROOT_UID:-0}
set -f   # the command line is split into words below, never expanded as file names

# --option: the start option chosen in the Advanced Options menu for this boot (bootlog, debug, norestart, lastgood
# or vga; the menu writes /run/wineexplorer/bootoption), or nothing. Like the mode, only from a regular file root owns.
if [ "${1:-}" = --option ]; then
    option=
    if [ -f "$option_state" ] && [ ! -L "$option_state" ] && [ "$(stat -c %u "$option_state" 2>/dev/null)" = "$root_uid" ]; then
        read -r option < "$option_state" || true
    fi
    case "$option" in
        bootlog|debug|norestart|lastgood|vga) echo "$option" ;;
    esac
    exit 0
fi

mode=
# only a regular file that root owns: anyone else could otherwise choose the mode
if [ -f "$state" ] && [ ! -L "$state" ] && [ "$(stat -c %u "$state" 2>/dev/null)" = "$root_uid" ]; then
    read -r mode < "$state" || true
fi
if [ -z "$mode" ] && [ -r "$cmdline" ]; then
    for word in $(cat "$cmdline"); do
        case "$word" in
            wexmode=*) mode=${word#wexmode=} ;;
        esac
    done
fi
case "$mode" in
    menu|safe|safenet|safecmd) ;;
    *) mode=normal ;;
esac
# Setup has to finish first: its stages run only in a normal start.
if [ -e "$setup_stage" ]; then
    case "$mode" in
        safe*) mode=normal ;;
    esac
fi
# --record (wex-bootmode-record.service, early in every boot): Wine reads the mode from the state file
# (docs/SAFE_MODE_WORKORDER.md B0), and without the menu nothing else writes it. A menu boot records nothing: the
# menu writes its own choice.
if [ "${1:-}" = --record ]; then
    if [ "$mode" = menu ] || [ -e "$state" ] || [ -L "$state" ]; then exit 0; fi
    mkdir -p "${state%/*}" && printf '%s\n' "$mode" > "$state.tmp" && chmod 644 "$state.tmp" && mv -f "$state.tmp" "$state"
    exit
fi
echo "$mode"
