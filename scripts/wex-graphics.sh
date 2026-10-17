#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Small helper to manage opt-in DirectX-to-Vulkan layers (DXVK and vkd3d-proton).
# Usage: wex-graphics vulkan [on|off|status]
set -euo pipefail

DXVK_DIR="${WEX_DXVK_DIR:-/usr/local/share/wineexplorer/dxvk}"
WINE_PREFIX="${WINEPREFIX:-$HOME/.wine}"
DLLS=("d3d8" "d3d9" "d3d10core" "d3d11" "dxgi" "d3d12" "d3d12core")

check_vulkan_device() {
    if ! command -v vulkaninfo >/dev/null 2>&1; then
        return 1
    fi
    vulkaninfo --summary 2>/dev/null | grep -qE '^GPU[0-9]+:'
}

get_wine() {
    if command -v /usr/local/bin/wine >/dev/null 2>&1; then
        echo "/usr/local/bin/wine"
    elif command -v wine >/dev/null 2>&1; then
        echo "wine"
    else
        echo ""
    fi
}

vulkan_on() {
    echo "=== Enabling DirectX-to-Vulkan (DXVK & vkd3d-proton) ==="
    if ! check_vulkan_device; then
        echo "ERROR: No Vulkan device found on this system (checked via vulkaninfo)." >&2
        echo "DirectX-to-Vulkan requires a working Vulkan driver/device." >&2
        exit 1
    fi

    if [ ! -d "$DXVK_DIR" ]; then
        echo "ERROR: DXVK directory not found at $DXVK_DIR" >&2
        exit 1
    fi

    WINE_BIN=$(get_wine)
    if [ -z "$WINE_BIN" ]; then
        echo "ERROR: Wine binary not found" >&2
        exit 1
    fi

    # Set per-user DLL overrides in registry
    echo "Configuring native DLL overrides in user prefix: $WINE_PREFIX..."
    for dll in "${DLLS[@]}"; do
        WINEDEBUG=-all WINEPREFIX="$WINE_PREFIX" "$WINE_BIN" reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v "$dll" /d native /f >/dev/null 2>&1 || true
    done
    WINEDEBUG=-all WINEPREFIX="$WINE_PREFIX" wineserver -w 2>/dev/null || true

    # Link DXVK and vkd3d-proton DLLs if system directories are writable
    SYS32="$WINE_PREFIX/drive_c/windows/system32"
    SYSWOW64="$WINE_PREFIX/drive_c/windows/syswow64"
    if [ -w "$SYS32" ] && [ -d "$DXVK_DIR/x64" ]; then
        echo "Updating 64-bit DLL links in $SYS32..."
        for dll in "${DLLS[@]}"; do
            src="$DXVK_DIR/x64/$dll.dll"
            dst="$SYS32/$dll.dll"
            if [ -f "$src" ]; then
                if [ -e "$dst" ] || [ -L "$dst" ]; then
                    if [ ! -e "$dst.wex-builtin" ] && [ ! -L "$dst.wex-builtin" ]; then
                        mv "$dst" "$dst.wex-builtin" 2>/dev/null || true
                    else
                        rm -f "$dst"
                    fi
                fi
                ln -sf "$src" "$dst"
            fi
        done
    fi

    if [ -w "$SYSWOW64" ] && [ -d "$DXVK_DIR/x32" ]; then
        echo "Updating 32-bit DLL links in $SYSWOW64..."
        for dll in "${DLLS[@]}"; do
            src="$DXVK_DIR/x32/$dll.dll"
            dst="$SYSWOW64/$dll.dll"
            if [ -f "$src" ]; then
                if [ -e "$dst" ] || [ -L "$dst" ]; then
                    if [ ! -e "$dst.wex-builtin" ] && [ ! -L "$dst.wex-builtin" ]; then
                        mv "$dst" "$dst.wex-builtin" 2>/dev/null || true
                    else
                        rm -f "$dst"
                    fi
                fi
                ln -sf "$src" "$dst"
            fi
        done
    fi

    echo "Vulkan graphics enabled successfully for prefix: $WINE_PREFIX"
}

vulkan_off() {
    echo "=== Disabling DirectX-to-Vulkan (Restoring wined3d) ==="
    WINE_BIN=$(get_wine)
    if [ -n "$WINE_BIN" ] && [ -d "$WINE_PREFIX" ]; then
        echo "Clearing native DLL overrides in user prefix: $WINE_PREFIX..."
        for dll in "${DLLS[@]}"; do
            WINEDEBUG=-all WINEPREFIX="$WINE_PREFIX" "$WINE_BIN" reg delete 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v "$dll" /f >/dev/null 2>&1 || true
        done
        WINEDEBUG=-all WINEPREFIX="$WINE_PREFIX" wineserver -w 2>/dev/null || true
    fi

    SYS32="$WINE_PREFIX/drive_c/windows/system32"
    SYSWOW64="$WINE_PREFIX/drive_c/windows/syswow64"
    if [ -w "$SYS32" ]; then
        echo "Restoring 64-bit DLLs in $SYS32..."
        for dll in "${DLLS[@]}"; do
            dst="$SYS32/$dll.dll"
            if [ -L "$dst" ]; then
                target=$(readlink "$dst" || true)
                if [[ "$target" == *"$DXVK_DIR"* ]]; then
                    rm -f "$dst"
                    if [ -e "$dst.wex-builtin" ] || [ -L "$dst.wex-builtin" ]; then
                        mv "$dst.wex-builtin" "$dst" 2>/dev/null || true
                    elif [ -f "/usr/local/lib/wine/x86_64-windows/$dll.dll" ]; then
                        ln -sf "/usr/local/lib/wine/x86_64-windows/$dll.dll" "$dst"
                    fi
                fi
            fi
        done
    fi

    if [ -w "$SYSWOW64" ]; then
        echo "Restoring 32-bit DLLs in $SYSWOW64..."
        for dll in "${DLLS[@]}"; do
            dst="$SYSWOW64/$dll.dll"
            if [ -L "$dst" ]; then
                target=$(readlink "$dst" || true)
                if [[ "$target" == *"$DXVK_DIR"* ]]; then
                    rm -f "$dst"
                    if [ -e "$dst.wex-builtin" ] || [ -L "$dst.wex-builtin" ]; then
                        mv "$dst.wex-builtin" "$dst" 2>/dev/null || true
                    elif [ -f "/usr/local/lib/wine/i386-windows/$dll.dll" ]; then
                        ln -sf "/usr/local/lib/wine/i386-windows/$dll.dll" "$dst"
                    fi
                fi
            fi
        done
    fi

    echo "Vulkan graphics disabled. wined3d defaults restored."
}

vulkan_status() {
    echo "=== WineExplorer Graphics Status ==="
    echo "Prefix: $WINE_PREFIX"
    if check_vulkan_device; then
        dev=$(vulkaninfo --summary 2>/dev/null | grep -E '^GPU[0-9]+:' | head -1 || true)
        echo "Vulkan device: available ($dev)"
    else
        echo "Vulkan device: NOT AVAILABLE (wined3d software/OpenGL only)"
    fi

    WINE_BIN=$(get_wine)
    if [ -n "$WINE_BIN" ] && [ -d "$WINE_PREFIX" ]; then
        overrides=$(WINEDEBUG=-all WINEPREFIX="$WINE_PREFIX" "$WINE_BIN" reg query 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' 2>/dev/null || true)
        if echo "$overrides" | grep -qE 'd3d11'; then
            echo "DirectX-to-Vulkan: ENABLED (native overrides active)"
        else
            echo "DirectX-to-Vulkan: DISABLED (using default wined3d builtins)"
        fi
    fi
}

cmd="${1:-status}"
subcmd="${2:-}"

case "$cmd" in
vulkan)
    case "$subcmd" in
    on)  vulkan_on ;;
    off) vulkan_off ;;
    status|"") vulkan_status ;;
    *)   echo "Usage: wex-graphics vulkan [on|off|status]"; exit 1 ;;
    esac
    ;;
status)
    vulkan_status
    ;;
*)
    echo "Usage: wex-graphics vulkan [on|off|status]"
    exit 1
    ;;
esac
