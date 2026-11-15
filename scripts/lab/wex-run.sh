#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wex-run.sh MODE - build this worktree in wexbuild and run the system tier suites (docs/TOOLING.md, "wex-run").
#
# Run as root in the wexbuild WSL distribution, from any worktree on D: (its path is taken from this script's place):
#   wsl.exe -d wexbuild -u root -- bash /mnt/d/DeepThought/Projects/<worktree>/scripts/lab/wex-run.sh MODE
#
# MODE, the cheapest that covers the change:
#   labsuite  lab suite only, no builds      (lab scripts, verifier, worker or suite script changed)
#   lab       sync, builds, lab suite        (only code behind the lab build flags changed)
#   normal    sync, build, normal suite, broker test
#   full      sync, builds, T1, normal suite, broker test, lab suite (product code changed)
#   build     sync and build only (code the suites do not cover; run that code's own tests after it)
#   clean     remove a suite fixture left behind by a killed run, nothing else
#
# The suites run from a copy of the worktree's scripts, tools, src and installer in /root/wex-snap/<worktree>, so the
# worktree may be edited during a run (bash reads a script as it goes; an edit under a running suite corrupts it).
# Sources go into the build trees by the sync at the start: /root/WE-prefetch (wine clone, src overlay, WoW64 build
# in build/wow64) and, for the lab, /root/owned-r8 (scripts/lab/r8-lab-build.sh).
# Output: one line per stage with its result and minutes; logs in /var/tmp/wex-run/*.log (not /tmp: a tmpfs there is emptied when WSL stops the idle distribution).
#
# Only for wexbuild or a throwaway machine: the suites create and remove /etc/wineexplorer,
# /usr/local/libexec/wineexplorer, /var/lib/wexsys-c2 and the users wexadm, wexstd, wexcar, wexgreeter.
set -u
# The whole script is one function, called at the end: bash reads it completely before running, so editing this
# file during a run cannot change the run.
main() {
MODE=${1:?labsuite|lab|normal|full|build|clean}
D=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TREE=$(basename "$D")
SNAP=/root/wex-snap/$TREE
P=/root/WE-prefetch
CL=$P/wine
MAIN=$P/build/wow64
LAB=/root/owned-r8/build/wow64
LOGS=/var/tmp/wex-run
mkdir -p "$LOGS"

clean_fixture()
{
    pkill -f '/var/lib/wexsys-c2/bin/' 2>/dev/null
    pkill -f 'e1s6-lab.py' 2>/dev/null
    sleep 1
    for u in wexadm wexstd wexcar wexgreeter; do pkill -u "$u" 2>/dev/null; done
    sleep 2
    rm -f /etc/dbus-1/system.d/org.wineexplorer.E1S6Lab.conf
    rm -rf /var/lib/wexsys-c2 /etc/wineexplorer /usr/local/libexec/wineexplorer
    for u in wexadm wexstd wexcar; do id "$u" >/dev/null 2>&1 && userdel "$u"; rm -rf "/home/$u"; done
    id wexgreeter >/dev/null 2>&1 && userdel -r wexgreeter 2>/dev/null
    getent group wexsysc2 >/dev/null && groupdel wexsysc2
    rm -rf /run/user/1007 /run/user/1008 /run/user/1009
    # setup runs enable the account watcher units; the suites refuse a machine where they are active
    systemctl disable --now wex-accounts.path wex-accounts.timer >/dev/null 2>&1
    systemctl reset-failed wex-accounts.service >/dev/null 2>&1
    return 0
}

case $MODE in
    labsuite|lab|normal|full|build) ;;
    clean) clean_fixture; echo "fixture removed"; exit 0 ;;
    *) echo "unknown mode $MODE"; exit 2 ;;
esac
[ -d "$D/scripts" ] && [ -e "$D/wine" ] || { echo "not a worktree: $D"; exit 2; }
if pgrep -f 'test_sysrun_server|test_e1s6|test_broker_e1s18' >/dev/null || pgrep -f "make -C $P|make -C /root/owned-r8" >/dev/null; then
    echo BUSY; exit 3
fi

# A git worktree's .git file names a Windows path (D:/...): translate it for WSL.
if [ -f "$D/wine/.git" ]; then
    GD=$(sed -n 's/^gitdir: //p' "$D/wine/.git" | tr -d '\r' | sed 's#^\([A-Za-z]\):#/mnt/\L\1#')
    WGIT=(git --git-dir="$GD" --work-tree="$D/wine")
    [ -f "$GD/commondir" ] && GD=$(cd "$GD/$(tr -d '\r' < "$GD/commondir")" && pwd)
else
    GD=$D/wine
    WGIT=(git -C "$D/wine")
fi

# Wine: the clone checks out the worktree's commit, then takes its changed and new files; src is copied over.
sync_tree()
{
    local head files f m
    head=$("${WGIT[@]}" rev-parse HEAD) || return 1
    echo "wine $head"
    git -C "$CL" fetch -q "$GD" "$head" && git -C "$CL" checkout -q -f "$head" || return 1
    # the stock folders that the src overlay replaces are links into src (checkout -f restores the real folders)
    for m in dlls/desk.cpl dlls/wlanui programs/wmplayer; do
        if [ -d "$CL/$m" ] && [ ! -L "$CL/$m" ]; then
            rm -rf "$P/build/e1s-batch-realdir-backup/$(echo "$m" | tr / -)"
            mkdir -p "$P/build/e1s-batch-realdir-backup"
            mv "$CL/$m" "$P/build/e1s-batch-realdir-backup/$(echo "$m" | tr / -)"
            ln -s "../../src/$m" "$CL/$m"
        fi
    done
    files=$(cd "$D/wine" && { "${WGIT[@]}" diff --name-only; "${WGIT[@]}" ls-files --others --exclude-standard; } | sort -u)
    for f in $files; do
        [ -f "$D/wine/$f" ] || continue
        # copy without keeping the worktree's time stamp: an object built from another tree's newer copy of the
        # file would otherwise look up to date to make (bit us when two worktrees took turns on the runner)
        cmp -s "$D/wine/$f" "$CL/$f" && continue
        mkdir -p "$CL/$(dirname "$f")" && cp "$D/wine/$f" "$CL/$f" || return 1
    done
    echo "changed wine files: $(echo "$files" | grep -c .)"
    ( cd "$D/src" && find . -type f ! -path '*__pycache__*' ! -name '*.pyc' -print0 ) |
        while IFS= read -r -d '' f; do
            cmp -s "$D/src/$f" "$P/src/$f" || { mkdir -p "$(dirname "$P/src/$f")"; cp "$D/src/$f" "$P/src/$f"; }
        done
    # regenerate: assemble the wex modules into the wine tree, autoconf, configure
    ( cd "$P" && python3 scripts/assemble.py --wine-root wine ) && ( cd "$CL" && autoconf -f ) &&
        ( cd "$MAIN" && ./config.status --recheck && ./config.status ) && ( cd "$CL" && perl tools/make_requests )
}

# snapshot of what the suites read; wine stays a link (the suite's guard only greps build files there)
rm -rf "$SNAP"; mkdir -p "$SNAP"
for d in scripts tools src installer; do cp -a "$D/$d" "$SNAP/$d"; done
ln -s "$D/wine" "$SNAP/wine"
echo "snapshot of $TREE in $SNAP"

T0=$SECONDS; LAST=$SECONDS; RC=0
stage() { echo "$1 $2 ($(( (SECONDS - LAST + 30) / 60 )) min)"; LAST=$SECONDS; [ "$2" = ok ] || RC=1; }
suite()  # build dir, script, log
{
    local rc
    clean_fixture
    install -d -m 1777 /var/tmp/wex-r8-x
    # WSLg mounts /tmp/.X11-unix read-only, so Xvfb cannot listen there: a private mount namespace with an owned
    # socket directory bound over it
    ( cd "$SNAP" && WINE_BUILD_DIR=$1 timeout 3300 unshare --mount --propagation private sh -c \
        "mount --bind /var/tmp/wex-r8-x /tmp/.X11-unix && exec bash $2" ) > "$3" 2>&1
    rc=$?
    clean_fixture
    return $rc
}
passed() { grep -o '[0-9]* checks passed' "$1" | tail -1; }

if [ "$MODE" != labsuite ]; then
    sync_tree > $LOGS/sync.log 2>&1 && stage sync ok || { stage sync FAILED; tail -5 $LOGS/sync.log; exit 1; }
    make -C "$MAIN" -j16 > $LOGS/make.log 2>&1 && stage build ok ||
        { stage build FAILED; grep -m5 'error:' $LOGS/make.log; exit 1; }
fi
if [ "$MODE" = build ]; then
    echo "ALL PASS build $TREE ($(( (SECONDS - T0 + 30) / 60 )) min)"
    exit 0
fi
if [ "$MODE" = full ]; then
    ( cd "$SNAP" && BUILD=$MAIN bash scripts/lab/system-tier-t1.sh ) > $LOGS/t1.log 2>&1
    grep -q '^FAILS=0' $LOGS/t1.log && stage t1 ok || stage t1 FAILED
fi
if [ "$MODE" = normal ] || [ "$MODE" = full ]; then
    suite "$MAIN" scripts/test_sysrun_server.sh $LOGS/normal.log && stage "normal: $(passed $LOGS/normal.log)" ok ||
        { stage normal FAILED; grep -m3 '^FAIL' $LOGS/normal.log; }
    ( cd "$SNAP" && bash scripts/test_broker_e1s18.sh ) > $LOGS/broker.log 2>&1 && stage broker ok ||
        { stage broker FAILED; tail -3 $LOGS/broker.log; }
fi
if [ "$MODE" = lab ] || [ "$MODE" = full ]; then
    ( cd "$SNAP" && R8_LAB_LOG=$LOGS/lab-build.log bash scripts/lab/r8-lab-build.sh ) > $LOGS/lab-build.out 2>&1 &&
        stage lab-build ok || { stage lab-build FAILED; tail -3 $LOGS/lab-build.out; exit 1; }
fi
if [ "$MODE" != normal ]; then
    suite "$LAB" scripts/test_e1s6_transport.sh $LOGS/lab.log && stage "lab: $(passed $LOGS/lab.log)" ok ||
        { stage lab FAILED; grep -m3 '^FAIL' $LOGS/lab.log; }
fi
echo "ALL $([ $RC = 0 ] && echo PASS || echo FAILED) $MODE $TREE ($(( (SECONDS - T0 + 30) / 60 )) min)"
exit $RC
}
main "$@"
exit $?
