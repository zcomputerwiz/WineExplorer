#!/bin/bash
exec > /tmp/wine-session.log 2>&1
echo "=== WineExplorer session started: $(date) ==="
echo "USER=$USER DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY"

export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export XDG_CURRENT_DESKTOP="WineExplorer"
export XDG_SESSION_DESKTOP="WineExplorer"
export WINEDLLOVERRIDES="mscoree,mshtml="

# Disable screen blanking & DPMS
xset s off -dpms 2>/dev/null || true

# Query display resolution via xrandr if in X11
if command -v xrandr >/dev/null 2>&1; then
    XRANDR_RES=$(xrandr 2>/dev/null | grep 'current' | sed -E 's/.*current ([0-9]+) x ([0-9]+).*/\1x\2/')
    if [ -n "$XRANDR_RES" ]; then
        WIDTH=$(echo "$XRANDR_RES" | cut -d'x' -f1)
        HEIGHT=$(echo "$XRANDR_RES" | cut -d'x' -f2)
    fi
fi

WIDTH="${WIDTH:-1920}"
HEIGHT="${HEIGHT:-1080}"
REFRESH="${REFRESH:-60}"



# Check for gamescope
if command -v gamescope >/dev/null 2>&1; then
    echo "Starting WineExplorer DE session under Gamescope (${WIDTH}x${HEIGHT}@${REFRESH}Hz)..."
    exec gamescope \
        -W "${WIDTH}" \
        -H "${HEIGHT}" \
        -r "${REFRESH}" \
        -- \
        /usr/local/bin/wine explorer.exe /desktop=Default,"${WIDTH}x${HEIGHT}"
else
    echo "Starting direct WineExplorer virtual desktop (${WIDTH}x${HEIGHT})..."
    exec /usr/local/bin/wine explorer.exe /desktop=Default,"${WIDTH}x${HEIGHT}"
fi
