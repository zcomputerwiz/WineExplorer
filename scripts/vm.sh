#!/bin/bash
# Host-side helper for the WineExplorer-VM (Debian). Run from Git Bash.
#   scripts/vm.sh sync            push files changed since the last sync (git diff + untracked) to ~/src/wine
#   scripts/vm.sh build [target]  make in ~/build/wine64 (default: all)
#   scripts/vm.sh install         sudo make install (installs to /usr/local)
#   scripts/vm.sh deploy          sync + build + install
#   scripts/vm.sh restart-shell   clean every wine process and restart the session (display manager autologin)
#   scripts/vm.sh clean           same, without restarting the session
#   scripts/vm.sh shot [file]     screenshot of the VM display via VBoxManage (default vm_shot.png)
#   scripts/vm.sh xpshot [file]   screenshot of the "Windows XP Pro" VM
#   scripts/vm.sh fonts xp|open   install the genuine XP fonts into the prefix, or remove them and use the open replacements
#   scripts/vm.sh resources xp|open   copy the genuine XP themes, cursors, sounds and wallpapers into the prefix, or remove them
#   scripts/vm.sh ssh <cmd...>    run a command on the VM
#   scripts/vm.sh cursor software|hardware   select Xorg cursor rendering (requires graphical-session restart)
# Override with VM_KEY / VM_PORT / VM_USER if the environment differs.
set -eo pipefail
KEY="${VM_KEY:-}"
if [ -z "$KEY" ]; then
    if [ -f "/d/Projects/VirtualBox_VMs/vm_key" ]; then
        KEY="/d/Projects/VirtualBox_VMs/vm_key"
    else
        KEY="/d/VirtualBox_VMs/vm_key"
    fi
fi
PORT="${VM_PORT:-2222}"
USER_="${VM_USER:-wineuser}"
VBM="/c/Program Files/Oracle/VirtualBox/VBoxManage.exe"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SSH=(ssh -i "$KEY" -p "$PORT" -o BatchMode=yes -o IPQoS=none -o StrictHostKeyChecking=no "$USER_@127.0.0.1")
VM_SOURCE="${VM_SOURCE:-/home/$USER_/src}"
source_id=$(printf '%s:%s:%s' "$USER_" "$PORT" "$VM_SOURCE" | sha256sum | cut -c1-16)
mkdir -p "$ROOT/build/vm-sync"
STAMP="$ROOT/build/vm-sync/$source_id.commit"
# The local-only repository with the genuine Windows XP files (never pushed; see docs/ASSETS.md).  sync puts its
# copies over the open ones on the VM, so local builds look like XP while this repository holds open art only.
LOCAL="${WEX_LOCAL:-$ROOT/../WineExplorer-local}"

cmd="${1:-}"; shift || true
case "$cmd" in
sync)
    cd "$ROOT"; "$ROOT/scripts/fix-eol.sh" >/dev/null
    source_q=$(printf '%q' "$VM_SOURCE")
    wine_pin=$(git -C "$ROOT/wine" rev-parse HEAD)
    prepared=$("${SSH[@]}" "bash -s -- $source_q $wine_pin" < "$ROOT/scripts/prepare-wine.sh")
    echo "$prepared"
    full=()
    if [ -f "$STAMP" ]; then base=$(cat "$STAMP"); else base=$(git rev-parse HEAD); full=(--full); fi
    if [[ "$prepared" == *WINE_SOURCE_PREPARED* ]]; then full=(--full); fi
    python "$ROOT/scripts/sync-files.py" "$base" "${full[@]}" > /tmp/vm_sync_files
    python "$ROOT/scripts/sync-files.py" "$base" --deleted > /tmp/vm_sync_deleted
    if [ -s /tmp/vm_sync_deleted ]; then
        "${SSH[@]}" "mkdir -p $source_q/scripts && cat > $source_q/scripts/sync-delete.py" < "$ROOT/scripts/sync-delete.py"
        "${SSH[@]}" "python3 $source_q/scripts/sync-delete.py $source_q" < /tmp/vm_sync_deleted
    fi
    rm -f /tmp/vm_sync_local
    if [ -d "$LOCAL/ms-assets/wine" ]; then
        (cd "$LOCAL/ms-assets" && find wine -type f | sort) > /tmp/vm_sync_local
        python "$ROOT/scripts/source_layout.py" < /tmp/vm_sync_local | sort > /tmp/vm_sync_local_targets
        comm -23 /tmp/vm_sync_files /tmp/vm_sync_local_targets > /tmp/vm_sync_open && mv /tmp/vm_sync_open /tmp/vm_sync_files
    fi
    n=$(wc -l < /tmp/vm_sync_files)
    echo "syncing $n files"
    if [ "$n" -gt 0 ]; then
        python "$ROOT/scripts/sync-archive.py" /tmp/vm_sync_files | "${SSH[@]}" "tar -xmf - -C $source_q"
    fi
    if [ -s /tmp/vm_sync_local ]; then
        # only those that differ on the VM, touched so what is built from them is rebuilt
        (cd "$LOCAL/ms-assets" && xargs md5sum < /tmp/vm_sync_local) |
            python "$ROOT/scripts/source_layout.py" --checksum |
            "${SSH[@]}" "cd $source_q && md5sum -c 2>/dev/null | grep -v ': OK$' | sed 's/: [A-Z].*$//'" |
            python "$ROOT/scripts/source_layout.py" --reverse > /tmp/vm_sync_overlay || true
        if [ -s /tmp/vm_sync_overlay ]; then
            python "$ROOT/scripts/sync-overlay.py" "$LOCAL/ms-assets" /tmp/vm_sync_overlay | "${SSH[@]}" "tar -xmf - -C $source_q"
        fi
        echo "genuine files from $LOCAL: $(wc -l < /tmp/vm_sync_overlay) updated"
    fi
    "${SSH[@]}" "bash $source_q/scripts/assemble.sh"
    git rev-parse HEAD > "$STAMP"
    ;;
build)
    "${SSH[@]}" "cd ~/build/wine64 && export PATH=/usr/lib/ccache:\$PATH && make -j8 $* 2>&1 | tail -40"
    ;;
install)
    "${SSH[@]}" 'cd ~/build/wine64 && sudo make install 2>&1 | tail -5'
    # the prefix keeps its own copies of builtin modules; refresh any that differ from the installed ones
    "${SSH[@]}" 'cd ~/.wine/drive_c/windows/system32 && for f in *; do i=/usr/local/lib/wine/x86_64-windows/$f; [ -f "$i" ] && ! cmp -s "$f" "$i" && cp "$i" "$f" && echo "refreshed $f"; done; true'
    ;;
deploy)
    "$0" sync && "$0" build "$@" && "$0" install
    ;;
restart-shell)
    "${SSH[@]}" 'bash -s restart' < "$ROOT/scripts/vm-clean-wine.sh"
    ;;
shot)
    out="${1:-$ROOT/vm_shot.png}"
    "$VBM" controlvm "WineExplorer-VM" screenshotpng "$(cygpath -w "$out")"
    echo "$out"
    ;;
xpshot)
    out="${1:-$ROOT/xp_shot.png}"
    "$VBM" controlvm "Windows XP Pro" screenshotpng "$(cygpath -w "$out")"
    echo "$out"
    ;;
clean)
    "${SSH[@]}" 'bash -s' < "$ROOT/scripts/vm-clean-wine.sh"
    ;;
fonts)
    mode="${1:-}"; FD='$HOME/.wine/drive_c/windows/Fonts'
    case "$mode" in
    xp)
        "${SSH[@]}" "mkdir -p $FD"
        python "$ROOT/scripts/fonts.py" files | while read -r f; do f="$(cygpath -u "$f")"
            scp -q -i "$KEY" -P "$PORT" -o StrictHostKeyChecking=no "$f" "$USER_@127.0.0.1:.wine/drive_c/windows/Fonts/"
        done
        "${SSH[@]}" "wine reg delete 'HKCU\Software\Wine\Fonts\Replacements' /f >/dev/null 2>&1; true" ;;
    open)
        python "$ROOT/scripts/fonts.py" files | while read -r f; do f="$(cygpath -u "$f")"; "${SSH[@]}" "rm -f $FD/$(basename "$f")"; done
        python "$ROOT/scripts/fonts.py" reg > /tmp/font_repl.reg
        scp -q -i "$KEY" -P "$PORT" -o StrictHostKeyChecking=no /tmp/font_repl.reg "$USER_@127.0.0.1:/tmp/font_repl.reg"
        "${SSH[@]}" "wine regedit /tmp/font_repl.reg >/dev/null 2>&1; true" ;;
    *) echo "usage: vm.sh fonts xp|open"; exit 1 ;;
    esac
    echo "fonts: $mode set; restart the shell (vm.sh restart-shell) to reload the font list" ;;
cursor)
    mode="${1:-}"
    case "$mode" in
    software|hardware) ;;
    *) echo "usage: vm.sh cursor software|hardware"; exit 1 ;;
    esac
    "${SSH[@]}" 'sudo mkdir -p /etc/X11/xorg.conf.d; f=/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf; if [ -f "$f" ] && [ ! -e "$f.before-cursor-fix" ]; then sudo cp -p "$f" "$f.before-cursor-fix"; fi'
    if [ "$mode" = software ]; then
        "${SSH[@]}" 'sudo tee /etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf >/dev/null' < "$ROOT/scripts/20-wineexplorer-modesetting.conf"
    else
        sed 's/"SWcursor" "true"/"SWcursor" "false"/' "$ROOT/scripts/20-wineexplorer-modesetting.conf" |
            "${SSH[@]}" 'sudo tee /etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf >/dev/null'
    fi
    echo "cursor: $mode configured; restart the graphical session to apply (closes open Wine windows)"
    ;;
resources)
    case "${1:-}" in
    xp)
        tar -C "$ROOT/references/xp_export" -cf - Resources Cursors Media Web srchasst | "${SSH[@]}" 'tar -C ~/.wine/drive_c/windows -xmf -'
        python "$LOCAL/tools/xpres.py" pack
        echo "resources: genuine XP set installed" ;;
    open)
        "${SSH[@]}" 'cd ~/.wine/drive_c/windows && rm -rf Resources/Themes/Luna Resources/Themes/Luna.theme "Resources/Themes/Windows Classic.theme" Cursors Media Web/Wallpaper srchasst'
        "${SSH[@]}" 'rm -f ~/.wine/drive_c/windows/Resources/Icons/xpres_*.dll'
        echo "resources: XP set removed" ;;
    *) echo "usage: vm.sh resources xp|open"; exit 1 ;;
    esac ;;
ssh)
    "${SSH[@]}" "$@"
    ;;
*)
    sed -n '2,12p' "$0"
    exit 1
    ;;
esac
