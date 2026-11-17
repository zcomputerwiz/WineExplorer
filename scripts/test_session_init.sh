#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Prefix initialization only: no Wine, services, or installed templates needed.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURE=$(mktemp -d)
trap 'rm -rf -- "$FIXTURE"' EXIT
export HOME="$FIXTURE/home"
export WINEPREFIX="$HOME/.wine"
export WEX_SKEL_PFX="$FIXTURE/skel/.wine"
mkdir -p "$HOME" "$WEX_SKEL_PFX/drive_c/windows" "$WEX_SKEL_PFX/dosdevices"
echo template > "$WEX_SKEL_PFX/user.reg"
echo complete > "$WEX_SKEL_PFX/drive_c/windows/marker"
ln -s ../drive_c "$WEX_SKEL_PFX/dosdevices/c:"
. "$SCRIPT_DIR/wine-explorer-session-common.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
complete_prefix()
{
    diff -r -- "$WEX_SKEL_PFX" "$WINEPREFIX" || fail 'prefix differs from template'
    [ -L "$WINEPREFIX/dosdevices/c:" ] || fail 'template symlink lost'
    [ ! -e "$WINEPREFIX/.wine" ] || fail 'template nested in prefix'
}

wex_init_prefix
complete_prefix
echo 'fresh_init=PASS'

# Start again with user data from an interrupted first logon.
rm -rf -- "$WINEPREFIX"
mkdir -p "$WINEPREFIX"
echo preserve > "$WINEPREFIX/user-data"
old_inode=$(stat -c %i "$WINEPREFIX")
wex_init_prefix
complete_prefix
backups=("$WINEPREFIX".backup-*/prefix)
[ "${#backups[@]}" -eq 1 ] || fail 'expected one dated backup'
[ "$(cat "${backups[0]}/user-data")" = preserve ] || fail 'backup lost user data'
[ "$(stat -c %i "${backups[0]}")" = "$old_inode" ] || fail 'interrupted prefix was not moved aside'
echo 'interrupted_init=PASS'

# A good prefix must keep its inode, contents, and metadata.
echo personal > "$WINEPREFIX/user.reg"
echo untouched > "$WINEPREFIX/user-data"
before=$(find "$WINEPREFIX" -printf '%P %i %m %T@ %l\n' | sort)
wex_init_prefix
after=$(find "$WINEPREFIX" -printf '%P %i %m %T@ %l\n' | sort)
[ "$before" = "$after" ] || fail 'good prefix metadata changed'
[ "$(cat "$WINEPREFIX/user.reg")" = personal ] || fail 'good prefix registry changed'
[ "$(cat "$WINEPREFIX/user-data")" = untouched ] || fail 'good prefix data changed'
echo 'existing_good_prefix=PASS'
echo 'OK: session prefix initialization'
