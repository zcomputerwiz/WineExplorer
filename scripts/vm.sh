#!/bin/bash
# Host-side helper for the WineExplorer-VM (Debian). Run from Git Bash.
#   scripts/vm.sh sync            push files changed since the last sync (git diff + untracked) to ~/src/wine
#   scripts/vm.sh build [target]  make in ~/build/wine64 (default: all)
#   scripts/vm.sh install         sudo make install (installs to /usr/local)
#   scripts/vm.sh deploy          sync + build + install
#   scripts/vm.sh restart-shell   clean every wine process and restart the session (GDM autologin)
#   scripts/vm.sh clean           same, without restarting the session
#   scripts/vm.sh shot [file]     screenshot of the VM display via VBoxManage (default vm_shot.png)
#   scripts/vm.sh xpshot [file]   screenshot of the "Windows XP Pro" VM
#   scripts/vm.sh fonts xp|open   install the genuine XP fonts into the prefix, or remove them and use the open replacements
#   scripts/vm.sh resources xp|open   copy the genuine XP themes, cursors, sounds and wallpapers into the prefix, or remove them
#   scripts/vm.sh ssh <cmd...>    run a command on the VM
# Override with VM_KEY / VM_PORT / VM_USER if the environment differs.
set -e
KEY="${VM_KEY:-/d/VirtualBox_VMs/vm_key}"
PORT="${VM_PORT:-2222}"
USER_="${VM_USER:-wineuser}"
VBM="/c/Program Files/Oracle/VirtualBox/VBoxManage.exe"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SSH=(ssh -i "$KEY" -p "$PORT" -o StrictHostKeyChecking=no "$USER_@127.0.0.1")
STAMP="$ROOT/.vm_synced_commit"   # gitignored; last commit whose tree was synced

cmd="${1:-}"; shift || true
case "$cmd" in
sync)
    cd "$ROOT"; "$ROOT/scripts/fix-eol.sh" >/dev/null
    base="$(cat "$STAMP" 2>/dev/null || git rev-list --max-parents=0 HEAD | head -1)"
    {
        git diff --name-only --diff-filter=AM "$base" -- wine
        git ls-files --others --exclude-standard -- wine
    } 2>/dev/null | sort -u > /tmp/vm_sync_files
    git diff --name-only --diff-filter=D "$base" -- wine 2>/dev/null | sed 's#^wine/##' > /tmp/vm_sync_deleted || true
    n=$(wc -l < /tmp/vm_sync_files)
    echo "syncing $n files"
    if [ "$n" -gt 0 ]; then
        tar -cf - -T /tmp/vm_sync_files | "${SSH[@]}" 'tar -xmf - -C ~/src'
    fi
    if [ -s /tmp/vm_sync_deleted ]; then
        "${SSH[@]}" 'cd ~/src/wine && xargs -r rm -f' < /tmp/vm_sync_deleted
    fi
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
resources)
    case "${1:-}" in
    xp)
        tar -C "$ROOT/references/xp_export" -cf - Resources Cursors Media Web | "${SSH[@]}" 'tar -C ~/.wine/drive_c/windows -xmf -'
        echo "resources: genuine XP set installed" ;;
    open)
        "${SSH[@]}" 'cd ~/.wine/drive_c/windows && rm -rf Resources/Themes/Luna Resources/Themes/Luna.theme "Resources/Themes/Windows Classic.theme" Cursors Media Web/Wallpaper'
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
