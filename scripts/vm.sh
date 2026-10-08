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
ssh)
    "${SSH[@]}" "$@"
    ;;
*)
    sed -n '2,12p' "$0"
    exit 1
    ;;
esac
