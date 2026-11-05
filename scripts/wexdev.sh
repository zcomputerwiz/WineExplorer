#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Development harness; see docs/WEXDEV.md. Requires Bash 4+, GNU tools and flock.
set -Eeuo pipefail
SRC=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=${WEXDEV_BUILD:-/root/WE-prefetch/build/wow64}
CLONE=${WEXDEV_CLONE:-/root/WE-prefetch/wine}
STATE=${WEXDEV_HOME:-/root/wexdev}
ROOT=/var/lib/wexdev-sys
M=$ROOT/machine GRP=wexdevsys ADM=wexdev STD=wexdevstd
CONF=/etc/wineexplorer/sysrun.conf
LIBEXEC=/usr/local/libexec/wineexplorer
SYSRUN=$LIBEXEC/wex-sysrun
# shellcheck source=scripts/lib/sysrun-fixture.sh
source "$SRC/scripts/lib/sysrun-fixture.sh"
RESULT="unexpected failure"
ACTIVE_PREFIX= ACTIVE_USER= XV_PID= JOB= HIVE_BACKUP=
GUEST_TMP= GUEST_TAR=
LOCKED=0
TIMEOUT=${WEXDEV_TIMEOUT:-120}
CAP=${WEXDEV_LOG_BYTES:-2097152}

fail() { RESULT="$*"; exit 1; }
valid_name() { [[ $1 =~ ^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}$ ]] || fail "invalid name: $1"; }
as_user() { local user=$1; shift; runuser -u "$user" -- env HOME="/home/$user" "$@"; }
stop_prefix()
{
    [[ -n $ACTIVE_PREFIX ]] || return 0
    local rc=0
    # -k exits nonzero when no server is running; that is a clean state, not a failure.
    # Only -w timing out (server stuck) is an error.
    if [[ -n $ACTIVE_USER ]]; then
        as_user "$ACTIVE_USER" env WINEPREFIX="$ACTIVE_PREFIX" timeout 15 "$BUILD/server/wineserver" -k || :
        as_user "$ACTIVE_USER" env WINEPREFIX="$ACTIVE_PREFIX" timeout 15 "$BUILD/server/wineserver" -w || rc=1
    else
        WINEPREFIX=$ACTIVE_PREFIX timeout 15 "$BUILD/server/wineserver" -k || :
        WINEPREFIX=$ACTIVE_PREFIX timeout 15 "$BUILD/server/wineserver" -w || rc=1
    fi
    return "$rc"
}
cleanup()
{
    local rc=$? stopped=1
    trap - EXIT INT TERM
    set +e
    if [[ -n $ACTIVE_PREFIX ]]; then
        stop_prefix || { rc=1; stopped=0; RESULT="prefix server cleanup failed: $ACTIVE_PREFIX"; }
    fi
    if [[ -n $JOB ]]; then kill "$JOB" 2>/dev/null; wait "$JOB" 2>/dev/null; fi
    if [[ -n $HIVE_BACKUP && $stopped == 1 ]]; then
        if cat "$HIVE_BACKUP" > "$M/system.reg"; then rm -f -- "$HIVE_BACKUP";
        else rc=1; RESULT="cannot restore service hive; backup retained: $HIVE_BACKUP"; fi
    fi
    if [[ -n $XV_PID ]]; then kill "$XV_PID" 2>/dev/null; wait "$XV_PID" 2>/dev/null; fi
    [[ -z $GUEST_TMP ]] || rm -f -- "$GUEST_TMP"
    [[ -z $GUEST_TAR ]] || rm -f -- "$GUEST_TAR"
    (( LOCKED == 0 )) || : > "$STATE/lock.owner"
    if [[ $RESULT == "unexpected failure" ]]; then RESULT="unexpected command failure (exit=$rc); inspect command diagnostics"; fi
    if (( rc == 0 )); then printf 'OK: %s\n' "$RESULT"; else printf 'FAIL: %s\n' "$RESULT" >&2; fi
    exit "$rc"
}
trap cleanup EXIT
trap 'RESULT="interrupted"; exit 130' INT
trap 'RESULT="terminated"; exit 143' TERM
[[ $TIMEOUT =~ ^[1-9][0-9]*$ && $CAP =~ ^[1-9][0-9]*$ ]] || fail "invalid timeout/log cap"
mkdir -p "$STATE/logs" "$STATE/pfx"
exec 9>"$STATE/lock"
if ! flock -n 9; then
    fail "harness locked by $(cat "$STATE/lock.owner" 2>/dev/null || echo unknown)"
fi
LOCKED=1
printf 'pid=%s user=%s host=%s command=%q started=%s\n' "$$" "$(id -un)" "$(hostname)" "$*" "$(date -Is)" > "$STATE/lock.owner"

need_build() { [[ -x $BUILD/wine && -x $BUILD/server/wineserver ]] || fail "missing WoW64 build: $BUILD"; }
display()
{
    [[ -z ${DISPLAY:-} || ${WEXDEV_HEADLESS:-0} == 1 ]] || return 0
    [[ -z $XV_PID ]] || return 0
    local number
    command -v Xvfb >/dev/null || fail "no DISPLAY; install Xvfb or set DISPLAY"
    : > "$STATE/display"
    Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$STATE/display" >"$STATE/xvfb.log" 2>&1 9>&- &
    XV_PID=$!
    for ((number=0; number<100; number++)); do
        [[ ! -s $STATE/display ]] || break
        kill -0 "$XV_PID" 2>/dev/null || fail "Xvfb failed; see $STATE/xvfb.log"
        sleep .1
    done
    [[ -s $STATE/display ]] || fail "Xvfb startup timed out"
    read -r number < "$STATE/display"
    export DISPLAY=:$number
}
wine_env()
{
    env WINEPREFIX="$ACTIVE_PREFIX" WINEARCH=win64 WINEDEBUG="${WINEDEBUG:-err+all}" \
        WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' "$@"
}
# Drain the complete stream while retaining only its tail, including very long lines.
cap_log()
{
    perl -e '$|=1; my $cap=shift; my $p=shift; open my $f, "+>", $p or die $!;
      my $buf=""; while (sysread(STDIN,my $s,65536)) {
        $buf.=$s; substr($buf,0,length($buf)-$cap,"") if length($buf)>$cap;
        seek($f,0,0); print $f $buf; truncate($f,length($buf));
      }' "$CAP" "$1"
}
logged()
{
    local name=$1 rc log; shift
    if [[ $1 == wine_env ]]; then
        shift
        set -- env WINEPREFIX="$ACTIVE_PREFIX" WINEARCH=win64 WINEDEBUG="${WINEDEBUG:-err+all}" \
            WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' "$@"
    fi
    log=$STATE/logs/$name-$(date +%Y%m%dT%H%M%S%N).log
    set +e
    timeout --kill-after=5 "$TIMEOUT" "$@" 9>&- 2>&1 | cap_log "$log"
    local -a codes=("${PIPESTATUS[@]}")
    set -e
    rc=${codes[0]}
    (( codes[1] == 0 )) || fail "log capture failed: $log"
    printf 'exit=%s log=%s\n' "$rc" "$log"
    tail -n 20 "$log"
    return "$rc"
}
fixture_owned()
{
    [[ -f $ROOT/owner && $(cat "$ROOT/owner") == "$BUILD" ]] ||
        fail "foreign or incomplete fixture: $ROOT; inspect manually; reset refuses foreign state"
    [[ ! -e $CONF ]] || grep -Fxq "machine_root=$M" "$CONF" ||
        fail "foreign $CONF; leave it untouched; arrange a maintenance window with its owner"
}
fixture_paths()
{
    local image resolved dir images
    for dir in system32 syswow64; do
        # WoW64 has no 32-bit wineboot (BUILD ships only x86_64 wineboot), and services.exe
        # lives only in system32; check each directory against the images it actually holds.
        if [[ $dir == system32 ]]; then images="wineboot.exe winedevice.exe ntdll.dll services.exe";
        else images="winedevice.exe ntdll.dll"; fi
        for image in $images; do
            # readlink -f succeeds even when the final component is absent, so test existence first.
            [[ -e "$M/drive_c/windows/$dir/$image" ]] || fail "missing $dir/$image"
            resolved=$(readlink -f "$M/drive_c/windows/$dir/$image")
            [[ $resolved == "$BUILD/"* ]] ||
                fail "$dir/$image resolves outside BUILD: $resolved; reset only this owned fixture, then up"
        done
    done
    grep -Fq '"Helper"="C:\\windows\\system32\\consent.exe"' "$M/system.reg" ||
        fail "system role disabled: missing elevation Helper"
}
fixture_stop()
{
    local user
    for user in "$STD" "$ADM"; do
        id "$user" >/dev/null 2>&1 || continue
        ACTIVE_USER=$user ACTIVE_PREFIX=/home/$user/.wine
        stop_prefix || fail "cannot stop fixture prefix $ACTIVE_PREFIX"
    done
    ACTIVE_USER= ACTIVE_PREFIX=
}
fixture_access()
{
    local user uid path dir acl mask
    local -a ancestors
    for user in "$ADM" "$STD"; do
        uid=$(id -u "$user")
        for path in "$BUILD" "$CLONE"; do
            path=$(realpath -e "$path")
            ancestors=()
            while [[ $path != / ]]; do ancestors=("$path" "${ancestors[@]}"); path=$(dirname "$path"); done
            for dir in "${ancestors[@]}"; do
                as_user "$user" test -x "$dir" && continue
                acl=$(getfacl -cpn "$dir")
                ! grep -q "^user:$uid:" <<<"$acl" || fail "pre-existing UID ACL on $dir; inspect manually"
                # Do not widen an existing mask and thereby unmask somebody else's ACL.
                mask=0
                if grep -q '^mask::' <<<"$acl"; then
                    mask=1
                    grep -q '^mask::..x' <<<"$acl" || fail "ACL mask lacks search on $dir; inspect manually"
                fi
                printf '%s\t%s\t%s\n' "$uid" "$mask" "$dir" >> "$ROOT/search-acls"
                if (( mask )); then setfacl -n -m "u:$uid:--x" "$dir";
                else setfacl -m "u:$uid:--x" "$dir"; fi
            done
        done
        as_user "$user" test -r "$BUILD/dlls/ntdll/ntdll.so" ||
            fail "BUILD binaries must be readable by $user"
    done
}
sysrole()
{
    (( $# == 1 )) || fail "usage: sysrole up|down|status|reset"
    [[ $(id -u) == 0 ]] || fail "sysrole needs root"
    local user p arch compiler uid dir mask
    case "$1" in
        status)
            fixture_owned
            [[ -e $ROOT/ready ]] || fail "owned fixture is incomplete; sysrole reset then up"
            fixture_paths
            cat "$CONF"
            RESULT="sysrole provisioned; servers are started per invocation, not left running";;
        down)
            fixture_owned; fixture_stop
            RESULT="sysrole fixture servers stopped; configuration retained";;
        reset)
            fixture_owned; fixture_stop
            [[ ! -e $SYSRUN ]] || {
                [[ -f $ROOT/launcher.sha256 ]] && sha256sum -c "$ROOT/launcher.sha256" ||
                    fail "launcher changed; inspect manually before removing"
            }
            # Only an owned fixture and its exact global configuration may be removed.
            if [[ -f $ROOT/search-acls ]]; then
                while IFS=$'\t' read -r uid mask dir; do
                    [[ -d $dir ]] || continue
                    if getfacl -cpn "$dir" | grep -q "^user:$uid:"; then setfacl -n -x "u:$uid" "$dir"; fi
                done < "$ROOT/search-acls"
                while IFS=$'\t' read -r uid mask dir; do
                    [[ -d $dir && $mask == 0 ]] || continue
                    # Remove only a mask introduced by this fixture, after both users.
                    if ! getfacl -cpn "$dir" | grep -Eq '^(user|group):[^:]'; then
                        setfacl -x m "$dir"
                    fi
                done < "$ROOT/search-acls"
                rm "$ROOT/search-acls"
            fi
            for user in "$STD" "$ADM"; do
                [[ ! -f $ROOT/created-$user ]] || userdel -r "$user"
            done
            [[ ! -f $ROOT/created-group ]] || groupdel "$GRP"
            [[ ! -e $CONF ]] || rm -- "$CONF"
            [[ ! -e $SYSRUN ]] || {
                rm -- "$SYSRUN"
            }
            rmdir "$LIBEXEC" /etc/wineexplorer 2>/dev/null || true
            rm -rf -- /var/lib/wexdev-sys
            RESULT="owned sysrole fixture removed";;
        up)
            need_build
            if [[ -e $ROOT ]]; then
                fixture_owned
                [[ -f $ROOT/ready ]] || fail "partial setup: inspect $ROOT/setup.log; sysrole reset then up"
                fixture_paths
            else
                # Fixed paths are compiled into ntdll. Never migrate or stop somebody else's setup.
                for p in /etc/wineexplorer "$LIBEXEC"; do
                    [[ ! -e $p ]] || fail "$p already exists; leave Qwen/wexadm state intact; retry after its owner removes it"
                done
                for user in "$ADM" "$STD"; do
                    ! id "$user" >/dev/null 2>&1 || fail "user $user already exists; refusing to reuse"
                done
                ! getent group "$GRP" >/dev/null || fail "group $GRP already exists"
                display
                install -d -m 0755 "$ROOT" "$ROOT/bin" "$ROOT/probes"
                printf '%s\n' "$BUILD" > "$ROOT/owner"
                groupadd --system "$GRP"; touch "$ROOT/created-group"
                ACTIVE_PREFIX=$M
                wex_fixture_machine timeout --kill-after=5 "$TIMEOUT" || fail "machine setup failed; see $ROOT/setup.log; reset owned partial fixture"
                stop_prefix || fail "machine bootstrap cleanup failed"
                ACTIVE_PREFIX=
                sha256sum "$SYSRUN" > "$ROOT/launcher.sha256"
                [[ $(stat -c '%a %U:%G' "$SYSRUN") == "2755 root:$GRP" ]] || fail "launcher not setgid"
                reg_append "$M/system.reg" 'Software\\Wine\\Elevation' '"Helper"="C:\\windows\\system32\\consent.exe"'
                for user in "$ADM" "$STD"; do
                    useradd -m -k "$ROOT/skel" -s /bin/bash "$user"; touch "$ROOT/created-$user"
                done
                fixture_access
                usermod -a -G sudo "$ADM"
                gcc -O2 -Wall -Wextra -o "$ROOT/bin/asgroup" "$SRC/scripts/sysrun_asgroup.c"
                install -m 2755 -o root -g "$GRP" "$ROOT/bin/asgroup" "$ROOT/bin/asgroup.sg"
                for arch in 64 32; do
                    compiler=x86_64-w64-mingw32-gcc
                    [[ $arch != 32 ]] || compiler=i686-w64-mingw32-gcc
                    "$compiler" -O2 -Wall -mwindows -o "$ROOT/probes/rolesprobe$arch.exe" "$SRC/scripts/sysrun_roles_probe.c" -ladvapi32
                    "$compiler" -O2 -Wall -o "$ROOT/probes/probe$arch.exe" "$SRC/scripts/sysrun_server_probe.c" -ladvapi32
                done
                fixture_paths
                touch "$ROOT/ready"
            fi
            printf 'Limited administrator: runuser -u %s -- env WINEPREFIX=/home/%s/.wine %q program.exe\n' "$ADM" "$ADM" "$BUILD/wine"
            printf 'Standard user: runuser -u %s -- env WINEPREFIX=/home/%s/.wine %q program.exe\n' "$STD" "$STD" "$BUILD/wine"
            printf 'Real system role: use wexdev.sh svc NAME /absolute/program.exe [args]. No interactive system shell.\n'
            RESULT="sysrole ready; Helper enabled; system images resolve into BUILD";;
        *) fail "unknown sysrole action: $1";;
    esac
}
# Quote one argument for CommandLineToArgvW / the Microsoft CRT.
win_quote()
{
    local s=$1 c slashes=0 i
    printf '"'
    for ((i=0; i<${#s}; i++)); do
        c=${s:i:1}
        if [[ $c == '\' ]]; then slashes=$((slashes+1)); continue; fi
        if [[ $c == '"' ]]; then
            printf '%*s' "$((slashes*2+1))" '' | tr ' ' '\\'
        else printf '%*s' "$slashes" '' | tr ' ' '\\'; fi
        slashes=0
        printf '%s' "$c"
    done
    printf '%*s' "$((slashes*2))" '' | tr ' ' '\\'
    printf '"'
}
svc()
{
    (( $# >= 2 )) || fail "usage: svc name exe [args] [--account LocalSystem|LocalService|NetworkService] [--arch 32|64]"
    local name=$1 exe=$2 account=LocalSystem arch=64 a compiler cmd image out rc elapsed user link
    shift 2; valid_name "$name"
    [[ $(id -u) == 0 ]] || fail "svc needs root"
    need_build; fixture_owned
    [[ -f $ROOT/ready ]] || fail "sysrole up is required"
    fixture_paths
    local -a args=()
    while (( $# )); do
        case "$1" in
            --account) (( $# >= 2 )) || fail "missing account"; account=$2; shift 2;;
            --arch) (( $# >= 2 )) || fail "missing arch"; arch=$2; shift 2;;
            --) shift; args+=("$@"); break;;
            *) args+=("$1"); shift;;
        esac
    done
    case "$account" in
        LocalSystem) ;;
        LocalService|NetworkService) account="NT AUTHORITY\\\\$account";;
        *) fail "unsupported service account";;
    esac
    [[ $arch == 32 || $arch == 64 ]] || fail "svc arch must be 32 or 64"
    arch_exe "$exe" "$arch"; exe=$(realpath "$EXE")
    fixture_stop
    display
    compiler=x86_64-w64-mingw32-gcc
    [[ $arch != 32 ]] || compiler=i686-w64-mingw32-gcc
    # Always compile the tiny adapter; never reuse a stale probe/adapter binary.
    "$compiler" -O2 -Wall -Wextra -municode -mwindows -o "$ROOT/bin/service$arch.exe.tmp" \
        "$SRC/scripts/wexdev-service.c" -ladvapi32
    mv "$ROOT/bin/service$arch.exe.tmp" "$ROOT/bin/service$arch.exe"
    install -d -m 2775 -o root -g "$GRP" "$M/drive_c/wexdev-out"
    # These accounts lack the system group. Give only their output directory write access.
    chmod 1777 "$M/drive_c/wexdev-out"
    # The skeleton shares Windows/ProgramData, but each user has a separate C:.
    for user in "$STD" "$ADM"; do
        link=/home/$user/.wine/drive_c/wexdev-out
        if [[ -e $link || -L $link ]]; then
            [[ -L $link && $(readlink -f "$link") == "$M/drive_c/wexdev-out" ]] ||
                fail "foreign output directory: $link"
        else
            ln -s "$M/drive_c/wexdev-out" "$link"
        fi
    done
    out=$M/drive_c/wexdev-out/$name.txt
    rm -f -- "$out" "$out.done"
    cmd=$(win_quote "Z:${exe//\//\\}")
    for a in "${args[@]}"; do cmd+=" $(win_quote "$a")"; done
    printf '%s' "$cmd" | iconv -f UTF-8 -t UTF-16LE > "$ROOT/bin/$name.cmd"
    chmod 0644 "$ROOT/bin/$name.cmd"
    image="\"Z:${ROOT//\//\\}\\bin\\service$arch.exe\" $name \"C:\\wexdev-out\\$name.txt\" \"Z:${ROOT//\//\\}\\bin\\$name.cmd\""
    image=${image//\\/\\\\}; image=${image//\"/\\\"}
    [[ ! -e $ROOT/service-hive.backup ]] || fail "leftover service-hive.backup; inspect and restore it before retry"
    HIVE_BACKUP=$ROOT/service-hive.backup
    cp -p "$M/system.reg" "$HIVE_BACKUP"
    add_service "$name" "$account" "$name" 2 "$image"
    ACTIVE_USER=$STD ACTIVE_PREFIX=/home/$STD/.wine
    # The build's ntdll invokes the installed setgid launcher for this prefix's server.
    as_user "$STD" env DISPLAY="$DISPLAY" WINEPREFIX="$ACTIVE_PREFIX" WINEDEBUG="${WINEDEBUG:-err+all}" \
        WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' \
        timeout --kill-after=5 "$TIMEOUT" "$BUILD/wine" "$ROOT/probes/rolesprobe64.exe" hold \
        >"$STATE/logs/service-start.log" 2>&1 9>&- &
    JOB=$!
    for ((elapsed=0; elapsed<TIMEOUT; elapsed++)); do
        [[ ! -s $out.done ]] || break
        sleep 1
    done
    [[ -s $out.done ]] || fail "service $name timed out; see $STATE/logs/service-start.log"
    read -r rc < "$out.done"; rc=${rc//$'\r'/}
    cap_log "$STATE/logs/$name-$(date +%Y%m%dT%H%M%S%N).log" < "$out"
    tail -c "$CAP" "$out" | tail -n 20
    printf 'service exit=%s output=%s\n' "$rc" "$out"
    [[ $rc == 0 ]] || fail "service $name child exit=$rc"
    RESULT="service $name completed as $account (arch=$arch); service removed on exit"
}
build()
{
    local p module rc full=0
    local -a paths=() targets=() modules=()
    need_build
    [[ $(git -C "$SRC/wine" branch --show-current) == development ]] || fail "source wine must be on development"
    rm -f "$STATE/last-build.files"
    if (( $# )); then paths=("$@"); else
        mapfile -d '' paths < <(git -C "$SRC/wine" diff --name-only -z HEAD; git -C "$SRC/wine" ls-files --others --exclude-standard -z)
        while IFS= read -r -d '' p; do
            if [[ ! -f $CLONE/$p ]] || ! cmp -s "$SRC/wine/$p" "$CLONE/$p"; then paths+=("$p"); fi
        done < <(git -C "$SRC/wine" ls-files -z)
        while IFS= read -r -d '' p; do
            [[ -e $SRC/wine/$p ]] || paths+=("$p")
        done < <(git -C "$CLONE" ls-files -z)
    fi
    if (( $# )); then
        # Explicit paths: copy only those files. The full scan below walks every tracked file over /mnt/d, which
        # takes minutes (it made an earlier 120 s timeout fail), so naming the changed files is the fast path.
        : > "$STATE/logs/sync.log"
        for p in "$@"; do
            p=${p#wine/}
            [[ $p != /* && $p != *..* ]] || fail "unsafe source path: $p"
            if [[ -f $SRC/wine/$p ]]; then
                mkdir -p "$CLONE/$(dirname -- "$p")"
                cp -p -- "$SRC/wine/$p" "$CLONE/$p" || fail "copy failed: $p"
                printf 'sync %s\n' "$p" >> "$STATE/logs/sync.log"
            fi
        done
    else
        [[ -f /root/sync-wine.sh ]] || fail "missing authoritative /root/sync-wine.sh"
        # Keep the existing changed/untracked-file sync implementation authoritative.
        timeout --kill-after=5 "${WEXDEV_SYNC_TIMEOUT:-900}" bash /root/sync-wine.sh >"$STATE/logs/sync.log" 2>&1 ||
            fail "source sync failed or timed out (WEXDEV_SYNC_TIMEOUT, default 900 s); see $STATE/logs/sync.log"
    fi
    grep -Ei 'warning:|error:|fatal:' "$STATE/logs/sync.log" || true
    for p in "${paths[@]}"; do
        p=${p#wine/}; p=${p%/}
        [[ $p != /* && $p != *..* && $p != *$'\n'* ]] || fail "unsafe source path: $p"
        case "$p" in
            dlls/*) module=${p#dlls/}; module=${module%%/*}
                targets+=("dlls/$module/all")
                [[ ! -d $SRC/wine/dlls/$module/tests ]] || targets+=("dlls/$module/tests/all")
                modules+=("dlls/$module");;
            programs/*) module=${p#programs/}; module=${module%%/*}
                targets+=("programs/$module/all"); modules+=("programs/$module");;
            server|server/*) targets+=(server/wineserver); modules+=(server);;
            *) printf 'NOTE: %s requires a full make (shared headers/build inputs).\n' "$p"; full=1;;
        esac
    done
    if (( ${#paths[@]} == 0 )); then RESULT="PASS build: no changed modules"; return; fi
    mapfile -t modules < <(printf '%s\n' "${modules[@]}" | sed '/^$/d' | sort -u)
    mapfile -t targets < <(printf '%s\n' "${targets[@]}" | sed '/^$/d' | sort -u)
    if (( full )); then targets=(); modules=(full); fi
    set +e
    make -C "$BUILD" -j"${WEXDEV_JOBS:-$(nproc)}" "${targets[@]}" >"$STATE/logs/build.log" 2>&1 9>&-
    rc=$?
    set -e
    grep -Ei 'warning:|error:|fatal:|undefined reference|make(\[[0-9]+\])?: \*\*\*' "$STATE/logs/build.log" || true
    (( rc == 0 )) || fail "FAIL build: ${modules[*]}; make exit=$rc; $STATE/logs/build.log"
    : > "$STATE/last-build.files.tmp"
    for module in "${modules[@]}"; do
        [[ $module != full ]] || module=.
        while IFS= read -r -d '' p; do
            p=${p#"$BUILD/"}; p=${p#./}
            printf '%s\n' "$p" >> "$STATE/last-build.files.tmp"
        done < <(find "$BUILD/$module" \( -type f -o -type l \) \
            \( -name '*.dll' -o -name '*.exe' -o -name '*.so' -o -name wineserver -o -name wine \) -print0)
    done
    sort -u "$STATE/last-build.files.tmp" > "$STATE/last-build.files"
    rm "$STATE/last-build.files.tmp"
    RESULT="PASS build: ${modules[*]} (in-tree runtime; no system install)"
}
prefix()
{
    (( $# >= 1 )) || fail "usage: prefix name [--gate K=V ...]"
    local name=$1 gate key value; shift
    valid_name "$name"; need_build
    ACTIVE_PREFIX=$STATE/pfx/$name
    [[ ! -e $ACTIVE_PREFIX ]] || { ACTIVE_PREFIX=; fail "prefix exists; choose a fresh name"; }
    local -a gates=()
    while (( $# )); do
        [[ $1 == --gate && $# -ge 2 && $2 == *=* ]] || fail "expected --gate K=V"
        gates+=("$2"); shift 2
    done
    # Validate gates before creating the prefix so a bad gate leaves no orphan.
    for gate in "${gates[@]}"; do
        key=${gate%%=*}; value=${gate#*=}
        valid_name "$key"
        [[ $value =~ ^[0-9]+$ || $value =~ ^0x[0-9a-fA-F]+$ ]] || fail "gate must be a DWORD: $gate"
    done
    display
    logged "$name-init" wine_env "$BUILD/wine" wineboot.exe -i || fail "wineboot failed"
    for gate in "${gates[@]}"; do
        key=${gate%%=*}; value=${gate#*=}
        logged "$name-gate" wine_env "$BUILD/wine" reg.exe add 'HKCU\Software\WineExplorer\Shell' \
            /v "$key" /t REG_DWORD /d "$value" /f || fail "gate failed: $gate"
    done
    printf 'export WINEPREFIX=%q WINEARCH=win64 WINEDLLOVERRIDES=%q\nexport WINE=%q WINESERVER=%q\n' \
        "$ACTIVE_PREFIX" 'mscoree,mshtml=;winemenubuilder.exe=d' "$BUILD/wine" "$BUILD/server/wineserver"
    RESULT="prefix $name created"
}
arch_exe()
{
    local exe=$1 arch=$2
    exe=${exe//\{arch\}/$arch}
    if [[ $arch == 32 ]]; then exe=${exe//x86_64-windows/i386-windows};
    else exe=${exe//i386-windows/x86_64-windows}; fi
    [[ -f $exe ]] || fail "missing executable for $arch: $exe"
    if [[ $arch == 32 ]]; then file -L "$exe" | grep -q 'PE32 executable' || fail "not a 32-bit PE: $exe";
    else file -L "$exe" | grep -q 'PE32+ executable' || fail "not a 64-bit PE: $exe"; fi
    EXE=$exe
}
run()
{
    (( $# >= 2 )) || fail "usage: run prefix [--arch 32|64|both] exe [args...]"
    local name=$1 arch=native a exe failed=0; shift
    valid_name "$name"; need_build
    [[ -d $STATE/pfx/$name ]] || fail "unknown prefix: $name"
    ACTIVE_PREFIX=$STATE/pfx/$name
    if [[ $1 == --arch ]]; then (( $# >= 3 )) || fail "missing arch/exe"; arch=$2; shift 2; fi
    [[ $arch == native || $arch == 32 || $arch == 64 || $arch == both ]] || fail "invalid architecture"
    exe=$1; shift
    display
    local -a arches=("$arch")
    [[ $arch != both ]] || arches=(64 32)
    for a in "${arches[@]}"; do
        EXE=$exe
        [[ $a == native ]] || arch_exe "$exe" "$a"
        logged "$name-$a" wine_env "$BUILD/wine" "$EXE" "$@" || failed=1
        stop_prefix || fail "cannot stop prefix $name"
    done
    (( failed == 0 )) || fail "run $name: one or more executions failed (124 means timeout)"
    RESULT="run $name: exit=0"
}
test_module()
{
    (( $# )) || fail "usage: test module [test names...]"
    local module=$1 name arch testname failed=0; shift
    valid_name "$module"
    name=test-$module-$$
    prefix "$name"
    local -a names=("$@")
    (( ${#names[@]} )) || names=("")
    for arch in x86_64 i386; do
        for testname in "${names[@]}"; do
            local -a args=()
            [[ -z $testname ]] || args=("$testname")
            logged "$name-$arch" wine_env "$BUILD/wine" \
                "$BUILD/dlls/$module/tests/$arch-windows/${module}_test.exe" "${args[@]}" || failed=1
            stop_prefix || fail "test server cleanup failed"
        done
    done
    (( failed == 0 )) || fail "test $module: one or more architectures/tests failed"
    RESULT="test $module: both architectures passed"
}
guest()
{
    (( $# >= 2 )) || fail "usage: guest ssh-port push|sysrole ..."
    local port=$1 p sum got destination op; shift
    [[ $port =~ ^[0-9]+$ && $port -gt 0 && $port -lt 65536 ]] || fail "invalid SSH port"
    op=$1; shift
    (( port > 0 && port < 65536 )) || fail "invalid SSH port"
    local -a ssh_command
    # WEXDEV_SSH is an executable plus whitespace-separated options, never evaluated.
    read -r -a ssh_command <<< "${WEXDEV_SSH:-ssh -i /var/tmp/wex-g4-lab-credentials/vm_key -o BatchMode=yes -o ConnectTimeout=10}"
    destination=${WEXDEV_GUEST:-debian@127.0.0.1}
    [[ $destination =~ ^[a-zA-Z0-9_.@:-]+$ && $destination != -* ]] || fail "invalid SSH destination"
    guest_send()
    {
        local local_file=$1 remote_file=$2
        base64 "$local_file" | timeout "$TIMEOUT" "${ssh_command[@]}" -p "$port" "$destination" \
            "mkdir -p /tmp/gsr /tmp/gsr-out /tmp/gsr/repo/scripts /tmp/gsr/repo/src/resources; base64 -d > '$remote_file'" ||
            fail "guest upload failed: $remote_file"
    }
    guest_run() { timeout "$TIMEOUT" "${ssh_command[@]}" -p "$port" "$destination" sudo -n bash /tmp/gsr/guest-control.sh "$@"; }
    guest_stage()
    {
        local f
        for f in scripts/lab/guest-sysrole/guest-control.sh scripts/lab/guest-sysrole/guest-rt2.sh \
            scripts/lab/guest-sysrole/guest-sysrole-provision.sh scripts/lab/guest-sysrole/guest-sysrole-user.sh \
            scripts/lab/guest-sysrole/guest-broker-install.sh scripts/lab/guest-sysrole/guest-env-debug.py \
            scripts/lab/guest-sysrole/guest-elev-run.sh scripts/lab/guest-sysrole/guest-personalize.py \
            scripts/lab/guest-sysrole/guest-sysrole-ids.sh \
            scripts/personalize-prefix.sh; do
            guest_send "$SRC/$f" "/tmp/gsr/$(basename "$f")"
        done
        guest_send "$SRC/scripts/setup-machine-root.sh" /tmp/gsr/repo/scripts/setup-machine-root.sh
        tar -C "$SRC/src/resources" -cf - fonts | base64 | timeout "$TIMEOUT" "${ssh_command[@]}" -p "$port" "$destination" \
            'base64 -d | tar -xf - -C /tmp/gsr/repo/src/resources' || fail 'guest font upload failed'
        timeout "$TIMEOUT" "${ssh_command[@]}" -p "$port" "$destination" chmod +x /tmp/gsr/*.sh || fail 'guest staging chmod failed'
    }
    case "$op" in
        push)
            (( $# == 0 )) || fail 'usage: guest ssh-port push'
            [[ -s $STATE/last-build.files ]] || fail "no successful build artifact manifest"
            ;;
        sysrole|broker|elev|install|debug)
            guest_stage
            ;;
        *) fail "unknown guest operation: $op";;
    esac
    if [[ $op == sysrole && ${1:-} == up ]]; then
        [[ -x $BUILD/loader/wine && -x $BUILD/server/wineserver ]] || fail "missing WoW64 build: $BUILD"
        command -v cc >/dev/null || fail 'cc is required to build wex-sysrun launcher'
        GUEST_TMP=$STATE/guest-wex-sysrun
        cc -O2 -Wall -fPIE -pie -D_FORTIFY_SOURCE=2 -fstack-protector-strong -Wl,-z,relro,-z,now \
            '-DSYSRUN_CONF="/etc/wineexplorer/sysrun.conf"' -o "$GUEST_TMP" "$SRC/tools/wex-sysrun/wex-sysrun.c" ||
            fail 'could not build wex-sysrun launcher'
        guest_send "$GUEST_TMP" /tmp/gsr/wex-sysrun
        GUEST_TAR=$STATE/guest-runtime.tar
        tar -C "$BUILD" -cf "$GUEST_TAR" .
        tar --append --file "$GUEST_TAR" --transform='s,^nls,wine/nls,' --transform='s,^fonts,wine/fonts,' -C "$SRC/wine" nls fonts
        guest_send "$GUEST_TAR" /tmp/build-wow64.tar
    fi
    case "$op" in
        sysrole) (( $# == 1 )) || fail 'usage: guest port sysrole up|status|reset'; guest_run sysrole "$1";;
        broker) guest_run broker "$@";;
        elev) (( $# >= 1 )) || fail 'usage: guest port elev PROGRAM [ARGS...]'; guest_run elev "$@";;
        debug) [[ ${1:-} == off || ${1:-} =~ ^[a-zA-Z0-9+_,.-]+$ ]] || fail 'invalid debug channels'; guest_run debug "${1:-off}";;
        install)
            (( $# >= 1 )) || fail 'usage: guest port install FILE [--elevated] [--debug channels] [--label name]'
            local file=$1 label=install arg
            shift
            [[ -f $file ]] || fail "installer not found: $file"
            while (( $# )); do case $1 in --elevated) shift;; --debug) (( $# >= 2 )) || fail 'missing debug channels'; guest_run debug "$2"; shift 2;; --label) (( $# >= 2 )) || fail 'missing label'; label=$2; shift 2;; *) fail "unknown install option: $1";; esac; done
            guest_send "$file" "/tmp/gsr-out/$(basename "$file")"
            guest_run install "$(basename "$file")" "$label";;
    esac
    [[ $op == push ]] || { RESULT="guest $op completed"; return; }
    while IFS= read -r p; do
        [[ $p =~ ^[a-zA-Z0-9_./+-]+$ && $p != /* && $p != *..* ]] || fail "unsafe artifact path: $p"
        [[ -f $BUILD/$p ]] || fail "artifact missing in this distro: $BUILD/$p; copy build and manifest or set WEXDEV_BUILD/HOME"
        sum=$(md5sum "$BUILD/$p"); sum=${sum%% *}
        # Use positional arguments restricted above; fixed remote root, atomic replacement,
        # original backup retained across repeat pushes. A failure stops before the next file.
        {
            cat <<'REMOTE'
set -eu
root=/home/debian/development-validation-20261101/memory-runtime2/build/wow64
file=$root/$1
expected=$2
[ -f "$file" ] || { echo "missing guest artifact: $file" >&2; exit 1; }
tmp=$(mktemp "$file.wexdev.XXXXXX")
trap 'rm -f "$tmp"' EXIT
base64 -d > "$tmp" <<'WEXDEV_PAYLOAD'
REMOTE
            base64 "$BUILD/$p"
            cat <<'REMOTE'
WEXDEV_PAYLOAD
actual=$(md5sum "$tmp"); actual=${actual%% *}
[ "$actual" = "$expected" ] || exit 1
[ -e "$file.bak-wexdev" ] || cp -pL "$file" "$file.bak-wexdev"
chmod --reference="$file" "$tmp"
mv -fT "$tmp" "$file"
REMOTE
        } | timeout "$TIMEOUT" "${ssh_command[@]}" -p "$port" "$destination" bash -s -- "$p" "$sum" ||
            fail "guest upload failed: $p (earlier files may already be replaced)"
        got=$(timeout "$TIMEOUT" "${ssh_command[@]}" -p "$port" "$destination" \
            "cat /home/debian/development-validation-20261101/memory-runtime2/build/wow64/$p" | md5sum) ||
            fail "guest readback failed: $p"
        [[ ${got%% *} == "$sum" ]] || fail "guest md5 round trip differs: $p"
        printf 'verified %s md5=%s\n' "$p" "$sum"
    done < "$STATE/last-build.files"
    RESULT="guest artifacts pushed; backups .bak-wexdev; md5 round trips matched"
}

(( $# )) || fail "usage: wexdev.sh build|prefix|run|test|sysrole|svc|guest ..."
command=$1; shift
case "$command" in
    build) build "$@";;
    prefix) prefix "$@";;
    run) run "$@";;
    test) test_module "$@";;
    sysrole) sysrole "$@";;
    svc) svc "$@";;
    guest) guest "$@";;
    *) fail "unknown command: $command";;
esac
