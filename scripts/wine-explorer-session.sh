#!/bin/sh
# ==============================================================================
# WineExplorer Desktop Environment - Gamescope Session Launcher
# ==============================================================================
# Launches WineExplorer DE inside Valve's Gamescope Wayland compositor.
# Acts as a full Linux display manager session (GDM, LightDM, SDDM, or direct TTY).
# ==============================================================================

# Ensure environment variables
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export XDG_CURRENT_DESKTOP="WineExplorer"
export XDG_SESSION_DESKTOP="WineExplorer"
export XDG_SESSION_TYPE="wayland"

# Query or define resolution
WIDTH="${WIDTH:-1920}"
HEIGHT="${HEIGHT:-1080}"
REFRESH="${REFRESH:-60}"

# Check for gamescope
if command -v gamescope >/dev/null 2>&1; then
    echo "Starting WineExplorer DE session under Gamescope compositor (${WIDTH}x${HEIGHT}@${REFRESH}Hz)..."
    exec gamescope \
        -W "${WIDTH}" \
        -H "${HEIGHT}" \
        -r "${REFRESH}" \
        --adaptive-sync \
        --backend drm \
        -- \
        wine explorer.exe /desktop=Default,"${WIDTH}x${HEIGHT}"
else
    echo "Gamescope not found in PATH, launching direct Wine virtual desktop..."
    exec wine explorer.exe /desktop=Default,"${WIDTH}x${HEIGHT}"
fi
