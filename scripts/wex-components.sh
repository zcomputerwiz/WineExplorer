#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Administrator tool for optional Windows Components and Microsoft redistributables.
# Installs into the shared machine root (/var/lib/wineexplorer/machine) via winetricks.
set -euo pipefail

MACHINE_ROOT="${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}"
CACHE_DIR="${WEX_CACHE_DIR:-/var/cache/winetricks}"

check_admin() {
    if ! id -Gn 2>/dev/null | grep -qw sudo && [ "$(id -u)" -ne 0 ]; then
        msg="Windows Components requires Administrator privileges.\nOnly members of the sudo group can install shared components."
        if [ -n "${DISPLAY:-}" ] && command -v zenity >/dev/null 2>&1; then
            zenity --error --title="Access Denied - Windows Components" --text="$msg" 2>/dev/null || true
        else
            echo -e "ERROR: $msg" >&2
        fi
        exit 1
    fi
}

ensure_cache_dir() {
    if [ ! -d "$CACHE_DIR" ]; then
        if [ "$(id -u)" -eq 0 ]; then
            mkdir -p "$CACHE_DIR"
            chown root:sudo "$CACHE_DIR" 2>/dev/null || true
            chmod 2775 "$CACHE_DIR" 2>/dev/null || true
        elif command -v sudo >/dev/null 2>&1; then
            sudo mkdir -p "$CACHE_DIR"
            sudo chown root:sudo "$CACHE_DIR" 2>/dev/null || true
            sudo chmod 2775 "$CACHE_DIR" 2>/dev/null || true
        fi
    fi
}

EULA_TEXT="END-USER LICENSE AGREEMENT NOTICE

The optional components offered here are proprietary software provided directly by
Microsoft Corporation or their respective licensors.

- Visual C++ 2015-2022 Runtime: Microsoft Software License Terms
- Microsoft Core Fonts (Arial, Times New Roman, Courier New): Microsoft EULA
- Microsoft Tahoma Font: Microsoft EULA
- Direct3D 47 Compiler / XACT Engine: Microsoft DirectX End-User License Agreement
- NVIDIA PhysX: NVIDIA Corporation License Agreement

WineExplorer DOES NOT bundle or redistribute these proprietary files.
If you choose to install them, they will be downloaded directly from the vendor's servers
to your machine.

You must review and accept the respective vendor license terms before downloading and
using these components."

run_gui() {
    CHOICES=$(zenity --list --checklist --checklist \
        --title="Windows Components Wizard" \
        --text="Select the optional Windows components and runtimes you wish to install:\n(These will be installed into the shared machine root for all users)" \
        --column="Install" --column="ID" --column="Component Description" \
        TRUE "vcrun2022" "Visual C++ 2015-2022 Runtime libraries (vcruntime140, msvcp140)" \
        FALSE "d3dcompiler_47" "Microsoft Direct3D 47 HLSL Compiler" \
        FALSE "xact" "Microsoft XACT Audio Engine (DirectX)" \
        FALSE "physx" "NVIDIA PhysX Physics Engine" \
        FALSE "corefonts" "Microsoft Core Fonts (Arial, Times, Courier)" \
        FALSE "tahoma" "Microsoft Tahoma Font (XP Shell Font)" \
        --width=640 --height=380 2>/dev/null || true)

    if [ -z "$CHOICES" ]; then
        echo "No components selected. Exiting."
        exit 0
    fi

    # EULA acceptance dialog
    if ! echo "$EULA_TEXT" | zenity --text-info \
        --title="License Agreement" \
        --checkbox="I accept the license terms for the selected components" \
        --width=600 --height=400 2>/dev/null; then
        echo "License agreement was not accepted. Nothing downloaded. Exiting."
        exit 0
    fi

    ensure_cache_dir

    if [ -z "${XDG_RUNTIME_DIR:-}" ] || [ ! -d "${XDG_RUNTIME_DIR:-}" ]; then
        export XDG_RUNTIME_DIR="/tmp/runtime-$(id -u)"
        mkdir -p "$XDG_RUNTIME_DIR"
        chmod 700 "$XDG_RUNTIME_DIR"
    fi

    # Install each chosen verb
    IFS="|" read -ra VERBS <<< "$CHOICES"
    for verb in "${VERBS[@]}"; do
        [ -n "$verb" ] || continue
        echo "Installing $verb into machine root..."
        (
            export WINEDEBUG=-all
            export WINEPREFIX="$MACHINE_ROOT"
            export W_CACHE="$CACHE_DIR"
            export XDG_CACHE_HOME="$CACHE_DIR"
            winetricks -q -f "$verb" </dev/null
        ) | zenity --progress --pulsate --title="Installing $verb" --text="Downloading and installing $verb into machine root..." --auto-close 2>/dev/null || true
    done

    chown -R root:sudo "$MACHINE_ROOT" 2>/dev/null || true
    chmod -R g+rwX "$MACHINE_ROOT" 2>/dev/null || true

    zenity --info --title="Windows Components" --text="Selected Windows components installed successfully." 2>/dev/null || true
}

run_cli_install() {
    local verbs=("$@")
    if [ ${#verbs[@]} -eq 0 ]; then
        echo "Usage: wex-components [gui | install <verb...> | list]"
        exit 1
    fi

    echo "============================================================"
    echo "$EULA_TEXT"
    echo "============================================================"
    echo ""
    read -r -p "Do you accept the license agreement? [y/N]: " confirm
    case "$confirm" in
        [yY]|[yY][eE][sS]) ;;
        *) echo "License agreement not accepted. Aborting."; exit 0 ;;
    esac

    ensure_cache_dir

    if [ -z "${XDG_RUNTIME_DIR:-}" ] || [ ! -d "${XDG_RUNTIME_DIR:-}" ]; then
        export XDG_RUNTIME_DIR="/tmp/runtime-$(id -u)"
        mkdir -p "$XDG_RUNTIME_DIR"
        chmod 700 "$XDG_RUNTIME_DIR"
    fi

    for verb in "${verbs[@]}"; do
        echo "=== Installing $verb into machine root ($MACHINE_ROOT) ==="
        export WINEDEBUG=-all
        export WINEPREFIX="$MACHINE_ROOT"
        export W_CACHE="$CACHE_DIR"
        export XDG_CACHE_HOME="$CACHE_DIR"
        if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
            xvfb-run -a winetricks -q -f "$verb" </dev/null
        else
            winetricks -q -f "$verb" </dev/null
        fi
    done

    chown -R root:sudo "$MACHINE_ROOT" 2>/dev/null || true
    chmod -R g+rwX "$MACHINE_ROOT" 2>/dev/null || true
    echo "Installation completed."
}

list_components() {
    echo "Available Windows Components:"
    echo "  vcrun2022       - Visual C++ 2015-2022 Runtime libraries"
    echo "  d3dcompiler_47  - Microsoft Direct3D 47 Compiler"
    echo "  xact            - Microsoft XACT Audio Engine"
    echo "  physx           - NVIDIA PhysX Physics Engine"
    echo "  corefonts       - Microsoft Core Fonts (Arial, Times New Roman, Courier New)"
    echo "  tahoma          - Microsoft Tahoma Font"
}

check_admin

cmd="${1:-}"
case "$cmd" in
    ""|gui)
        if [ -n "${DISPLAY:-}" ] && command -v zenity >/dev/null 2>&1; then
            run_gui
        else
            echo "No graphical display detected. Use: wex-components install <verb...>"
            list_components
        fi
        ;;
    install)
        shift
        run_cli_install "$@"
        ;;
    list)
        list_components
        ;;
    *)
        echo "Usage: wex-components [gui | install <verb...> | list]"
        exit 1
        ;;
esac
