#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# The native file work of the local Group Policy store (src/dlls/wexgpstore/native.c; docs/GROUP_POLICY.md, "Writing
# the local files: the spool") against hostile trees. Run as root, in a disposable fixture under /opt: a throw-away
# "session 0" user owns the tree, a throw-away "administrator" user in the system group may write the spool only, and
# root plays whoever else can touch the tree (the machine root above it is group-writable by design). Needs gcc.
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 2; }
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
S0="gpts0$$"; ADM="gptadm$$"; GRP="gptgrp$$"
FX="/opt/gpstore-test-$$"
BIN="/opt/gpstore-test-bin-$$"
cleanup() { rm -rf "$FX" "$BIN"; userdel "$S0" 2>/dev/null || true; userdel "$ADM" 2>/dev/null || true; groupdel "$GRP" 2>/dev/null || true; }
trap cleanup EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
PASS=0
ok() { echo "$1=PASS"; PASS=$((PASS + 1)); }

groupadd "$GRP"
useradd -M -N -s /usr/sbin/nologin -g "$GRP" "$S0"
useradd -M -N -s /usr/sbin/nologin -G "$GRP" "$ADM"
mkdir -p "$BIN"
cc -O2 -Wall -Wextra -o "$BIN/gpt" "$SRC/scripts/gpstore-native-test.c" "$SRC/src/dlls/wexgpstore/native.c" ||
    fail "cannot build the driver"
chmod 1777 "$BIN"   # (a scratch directory too: the throw-away users write their output files here)

as_s0() { runuser -u "$S0" -g "$GRP" -- "$@"; }
as_adm() { runuser -u "$ADM" -g "$GRP" -- "$@"; }
gpt() { as_s0 timeout 10 "$BIN/gpt" "$FX" "$@"; }
code() { sed -n 's/^result=\(-\?[0-9]*\).*/\1/p' <<<"$1"; }
GP="$FX/drive_c/windows/system32/GroupPolicy"
# result codes of native.h
R_OK=0; R_NONE=1; R_REFUSED=2; R_LARGE=3; R_IO=4; R_TREE=5
# kinds
K_MPOL=0; K_UPOL=1; K_TEMPLATE=2

fresh()
{
    rm -rf "$FX"
    mkdir -p "$GP/Spool"
    chown -R "$S0:$GRP" "$FX"
    chmod 2755 "$GP"
    chmod 2770 "$GP/Spool"
    chmod 0755 "$FX" "$FX/drive_c" "$FX/drive_c/windows" "$FX/drive_c/windows/system32"
}
expect() { local out; out=$("$@") || true; [ "$(code "$out")" = "$EXPECT" ] || fail "$WHAT: wanted result $EXPECT: $out"; }
want() { EXPECT=$1; WHAT=$2; shift 2; expect "$@"; }
request()   # NAME CONTENT: the administrator leaves a request, as GpStoreWrite does (a temporary file, then a rename)
{
    as_adm sh -c 'printf "%s" "$2" > "$1.tmp" && mv "$1.tmp" "$1"' _ "$GP/Spool/$1" "$2"
}

# 1. a request becomes a store file of session 0's account, readable by everyone
fresh
want $R_OK "open" gpt open
request Machine.Registry.pol 'PReg-content-1'
OUT=$(gpt read $K_MPOL "$BIN/out") || true
[ "$(code "$OUT")" = $R_OK ] && grep -q 'size=14$' <<<"$OUT" && [ "$(cat "$BIN/out")" = 'PReg-content-1' ] || fail "read of a request: $OUT"
cp "$BIN/out" "$BIN/in"
want $R_OK "write" gpt write $K_MPOL "$BIN/in"
[ "$(cat "$GP/Machine/Registry.pol")" = 'PReg-content-1' ] || fail "the store file has other content"
[ "$(stat -c '%U:%a' "$GP/Machine/Registry.pol")" = "$S0:644" ] || fail "store file is $(stat -c '%U:%a' "$GP/Machine/Registry.pol")"
[ "$(stat -c '%U:%a' "$GP/Machine")" = "$S0:2755" ] || fail "store directory is $(stat -c '%U:%a' "$GP/Machine")"
[ ! -e "$GP/Machine/Registry.pol.new" ] || fail "a STORE.new stayed"
want $R_OK "done" gpt done $K_MPOL
[ ! -e "$GP/Spool/Machine.Registry.pol" ] && [ ! -e "$GP/Spool/Machine.Registry.pol.error" ] || fail "the request or an error file stayed"
ok gps_request_becomes_session_0s_store_file

# 2. no request; zero bytes remove the store file; the template path is made and is session 0's
want $R_NONE "no request" gpt read $K_UPOL "$BIN/out"
request Machine.Registry.pol ''
OUT=$(gpt read $K_MPOL "$BIN/out") || true
[ "$(code "$OUT")" = $R_OK ] && grep -q 'size=0$' <<<"$OUT" || fail "an empty request: $OUT"
: > "$BIN/empty"
want $R_OK "remove" gpt write $K_MPOL "$BIN/empty"
[ ! -e "$GP/Machine/Registry.pol" ] || fail "zero bytes did not remove the store file"
want $R_OK "done" gpt done $K_MPOL
request Machine.GptTmpl.inf 'tmpl'
printf 'tmpl' > "$BIN/in"
want $R_OK "template write" gpt write $K_TEMPLATE "$BIN/in"
SE="$GP/Machine/Microsoft/Windows NT/SecEdit"
[ "$(cat "$SE/GptTmpl.inf")" = tmpl ] && [ "$(stat -c '%U:%a' "$SE")" = "$S0:2755" ] || fail "the SecEdit path is not session 0's"
ok gps_none_empty_and_template_path

# 3. what is not a regular file with one link is refused, and nothing hangs
fresh
as_adm ln -s /etc/passwd "$GP/Spool/User.Registry.pol"
want $R_REFUSED "a symlink as request" gpt read $K_UPOL "$BIN/out"
as_adm mkfifo "$GP/Spool/Machine.GptTmpl.inf"
want $R_REFUSED "a FIFO as request (no hang)" gpt read $K_TEMPLATE "$BIN/out"
printf 'secret' > "$BIN/adm-file"; chown "$ADM:$GRP" "$BIN/adm-file"; chmod 0660 "$BIN/adm-file"
ln "$BIN/adm-file" "$GP/Spool/Machine.Registry.pol"
want $R_REFUSED "a request with a second hard link" gpt read $K_MPOL "$BIN/out"
rm -f "$GP/Spool/Machine.Registry.pol" "$GP/Spool/User.Registry.pol" "$GP/Spool/Machine.GptTmpl.inf"
head -c $((4 * 1024 * 1024 + 1)) /dev/zero > "$GP/Spool/Machine.Registry.pol"
want $R_LARGE "a request over 4 MiB" gpt read $K_MPOL "$BIN/out"
head -c $((1024 * 1024 + 1)) /dev/zero > "$GP/Spool/Machine.GptTmpl.inf"
want $R_LARGE "a template over 1 MiB" gpt read $K_TEMPLATE "$BIN/out"
head -c $((4 * 1024 * 1024)) /dev/zero > "$GP/Spool/Machine.Registry.pol"
want $R_OK "a request of exactly 4 MiB" gpt read $K_MPOL "$BIN/out"
ok gps_links_fifos_hard_links_and_sizes_are_refused

# 4. NAME.error: written as session 0's account, after a planted link is removed (not followed), at most 200 bytes
fresh
printf 'victim' > "$BIN/victim"; chmod 0666 "$BIN/victim"
ln -s "$BIN/victim" "$GP/Spool/Machine.Registry.pol.error"
as_adm sh -c 'printf x > "$1"' _ "$GP/Spool/Machine.Registry.pol"
LONG=$(head -c 300 /dev/zero | tr '\0' 'e')
want $R_OK "done with an error" gpt done $K_MPOL "$LONG"
[ "$(cat "$BIN/victim")" = victim ] || fail "the link at NAME.error was followed"
[ ! -L "$GP/Spool/Machine.Registry.pol.error" ] && [ "$(stat -c '%s %U:%a' "$GP/Spool/Machine.Registry.pol.error")" = "200 $S0:640" ] ||
    fail "NAME.error is $(stat -c '%s %U:%a' "$GP/Spool/Machine.Registry.pol.error")"
[ ! -e "$GP/Spool/Machine.Registry.pol" ] || fail "the request stayed"
want $R_OK "done without an error" gpt done $K_MPOL
[ ! -e "$GP/Spool/Machine.Registry.pol.error" ] || fail "an old NAME.error stayed"
ok gps_error_file_is_written_and_cleared_without_following_links

# 5. a link planted at STORE.new is not followed; a stale STORE.new is removed
fresh
printf 'ok-content' > "$BIN/in"
want $R_OK "first write" gpt write $K_MPOL "$BIN/in"
ln -sf "$BIN/victim" "$GP/Machine/Registry.pol.new"
printf 'victim' > "$BIN/victim"
printf 'second' > "$BIN/in"
want $R_OK "write over a planted STORE.new" gpt write $K_MPOL "$BIN/in"
[ "$(cat "$BIN/victim")" = victim ] || fail "the link at STORE.new was followed"
[ "$(cat "$GP/Machine/Registry.pol")" = second ] && [ ! -e "$GP/Machine/Registry.pol.new" ] && [ ! -L "$GP/Machine/Registry.pol.new" ] ||
    fail "the store file or a STORE.new is wrong"
printf 'stale' > "$GP/Machine/Registry.pol.new"; chown "$S0" "$GP/Machine/Registry.pol.new"
want $R_OK "clean" gpt clean
[ ! -e "$GP/Machine/Registry.pol.new" ] || fail "a stale STORE.new stayed after clean"
ok gps_store_new_link_is_not_followed_and_stale_new_is_cleaned

# 6. a tree that is not as setup makes it is refused
fresh
mv "$GP/Spool" "$GP/Spool.real"; ln -s Spool.real "$GP/Spool"
want $R_TREE "a symlink in place of Spool" gpt open
fresh
chmod 2775 "$GP"
want $R_TREE "GroupPolicy writable by the group" gpt open
fresh
chmod 2775 "$GP/Spool"; chmod o+r "$GP/Spool"
want $R_TREE "Spool readable by others" gpt open
fresh
chown "root:$GRP" "$GP"
want $R_TREE "GroupPolicy not owned by this user" gpt open
fresh
mv "$FX/drive_c/windows" "$FX/drive_c/windows.real"; ln -s windows.real "$FX/drive_c/windows"
want $R_TREE "a symlink below the prefix" gpt open
fresh
printf 'ok' > "$BIN/in"
want $R_OK "write (makes Machine)" gpt write $K_MPOL "$BIN/in"
mv "$GP/Machine" "$GP/Machine.real"; ln -s Machine.real "$GP/Machine"
want $R_TREE "a symlink in place of Machine" gpt write $K_MPOL "$BIN/in"
rm -f "$GP/Machine"; mv "$GP/Machine.real" "$GP/Machine"; chmod 2775 "$GP/Machine"
want $R_TREE "Machine writable by the group" gpt write $K_MPOL "$BIN/in"
ok gps_tree_that_is_not_as_setup_makes_it_is_refused

# 7. the administrator can use the spool and nothing else
fresh
as_adm sh -c ': > "$1/Spool/x"' _ "$GP" || fail "the administrator cannot write the spool"
if as_adm sh -c ': > "$1/x"' _ "$GP" 2>/dev/null; then fail "the administrator can write GroupPolicy"; fi
want $R_OK "write (makes Machine)" gpt write $K_MPOL "$BIN/in"
if as_adm sh -c ': > "$1/Machine/x"' _ "$GP" 2>/dev/null; then fail "the administrator can write the store directory"; fi
ok gps_administrator_writes_the_spool_only

echo "=== gpstore native: $PASS checks passed ==="
