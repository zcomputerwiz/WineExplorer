#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sourced by the X11 and Wayland session launchers. Keep account/prefix setup
# independent of the display backend (the prefix-split work owns that policy).
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export XDG_CURRENT_DESKTOP=WineExplorer
export XDG_SESSION_DESKTOP=WineExplorer
export WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d'
export WINEDEBUG="${WINEDEBUG:--all,err+all}"
WINE="${WINE:-/usr/local/bin/wine}"
