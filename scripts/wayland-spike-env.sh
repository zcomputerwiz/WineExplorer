#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Wayland L0 spike harness (docs/WAYLAND_L0.md): source it on the Proton VM, then l0_start, l0_wine PROGRAM,
# l0_shot NAME, l0_key KEYS, l0_click X Y, l0_stop.
export L0=/tmp/l0; mkdir -p $L0
export XDG_RUNTIME_DIR=$L0/rt; mkdir -p -m 700 $XDG_RUNTIME_DIR
export PATH=$HOME/open/inst/bin:$PATH
export WINEPREFIX=$HOME/l0-prefix WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"

l0_start() {
    pkill -f "Xvfb :9"; pkill -x labwc; sleep 1
    setsid Xvfb :9 -screen 0 1280x800x24 >/dev/null 2>&1 < /dev/null &
    sleep 2
    # nested output fills the Xvfb screen; software renderer (no 3D in the VM)
    DISPLAY=:9 WLR_BACKENDS=x11 WLR_RENDERER=pixman WLR_X11_OUTPUTS=1 setsid labwc > $L0/labwc.log 2>&1 < /dev/null &
    for i in $(seq 1 20); do [ -S $XDG_RUNTIME_DIR/wayland-0 ] && break; sleep 0.5; done
    export WAYLAND_DISPLAY=wayland-0
    ls $XDG_RUNTIME_DIR
}
# a Wine program on the Wayland driver: no DISPLAY, so X11 cannot be picked
l0_wine() { env -u DISPLAY WAYLAND_DISPLAY=wayland-0 setsid wine "$@" >> $L0/wine.log 2>&1 < /dev/null & }
l0_shot() { DISPLAY=:9 import -window root "$L0/$1.png"; }
l0_key() { DISPLAY=:9 xdotool key "$@"; }
l0_click() { DISPLAY=:9 xdotool mousemove "$1" "$2" click 1; }
l0_stop() { wineserver -k 2>/dev/null; pkill -x labwc; pkill -f "Xvfb :9"; }
