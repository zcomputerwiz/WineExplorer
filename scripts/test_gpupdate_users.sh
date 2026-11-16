#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root-only, disposable fixture: installer/scripts/wex-gpupdate.sh's users of the accounts map (docs/DOMAIN_LOGON.md,
# WO 4) with a fake net, wbinfo, testparm and wine: which accounts are asked for policy, with which name Samba is asked,
# and the SID that policy is written under ("wexpolicy apply /user:SID"), for a version 1 and a version 2 map.
# Needs nothing from the host but runuser and bash; writes only under a temporary directory and /run/wineexplorer-policy.
set -euo pipefail
[ "$(id -u)" -eq 0 ] || { echo 'FAIL: run as root' >&2; exit 1; }
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="$HERE/../installer/scripts/wex-gpupdate.sh"
FX="$(mktemp -d)"
trap 'rm -rf -- "$FX"; rm -rf /run/wineexplorer-policy' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

GUID='{AAAAAAAA-0000-0000-0000-000000000001}'
POL="$FX/cache/gpo_cache/AD.WEXLAB.EXAMPLE/POLICIES/$GUID/USER"   # Samba's cache names the folder in capitals
mkdir -p "$POL" "$FX/bin"
printf 'PReg' > "$POL/Registry.pol"

cat > "$FX/bin/testparm" <<EOF
#!/bin/sh
case "\$*" in
    *realm*) echo ad.wexlab.example ;;
    *cache*) echo "$FX/cache" ;;
esac
EOF
cat > "$FX/bin/samba-gpupdate" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$FX/bin/net" <<EOF
#!/bin/sh
# net ads testjoin | net ads gpo list NAME -P | net ads search ...
case "\$2" in
    testjoin) exit 0 ;;
    gpo) echo "gpo list \$4" >> "$FX/net.log"
         case "\$4" in
             wexuser1|bob) printf 'name:\t%s\ndisplayname:\tWexLab Domain\nversion:\t1\nfilesyspath:\t/x\nlink_type:\t0\nuser_extensions:\t[{35378EAC-683F-11D2-A89A-00C04FBBCFA2}]\n' '$GUID' >&2 ;;
         esac ;;
    search) exit 0 ;;
esac
exit 0
EOF
cat > "$FX/bin/wbinfo" <<'EOF'
#!/bin/sh
# only these are users of the domain
case "$2" in
    'WEXLAB\wexuser1'|bob) echo "S-1-5-21-111-222-333-1104 SID_USER (1)"; exit 0 ;;
esac
exit 1
EOF
cat > "$FX/wine" <<EOF
#!/bin/sh
# the fake loader: what wexpolicy was asked
[ "\$1" = wexpolicy ] || exit 0
shift
echo "\$1 \$2" >> "$FX/wexpolicy.log"
exit 0
EOF
chmod 0755 "$FX"/bin/* "$FX/wine"
# runuser and the script's own tools come from the fakes first
run_gpupdate()
{
    : > "$FX/wexpolicy.log"; : > "$FX/net.log"
    PATH="$FX/bin:$PATH" WEX_ACCOUNTS_MAP="$1" WINE="$FX/wine" WEX_SESSION0_USER=root bash "$SCRIPT" > "$FX/out.log" 2>&1 ||
        { cat "$FX/out.log"; fail "wex-gpupdate.sh failed on $1"; }
}

# version 1: "21" is column 5 and the SID parts are columns 6 to 9; a local account is told apart by Winbind
cat > "$FX/map1" <<'EOF'
WEXACCOUNTS 1 3 1002 11 22 33
alice 1001 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa active 21 11 22 33 1001
bob 1002 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb active 21 11 22 33 1002
gone 1003 cccccccccccccccccccccccccccccccc tombstone 21 11 22 33 1003
EOF
run_gpupdate "$FX/map1"
grep -qx 'apply /user:S-1-5-21-11-22-33-1002' "$FX/wexpolicy.log" || { cat "$FX/wexpolicy.log"; fail 'v1: the user was not applied under the map SID'; }
! grep -q 'alice\|1001\|1003' "$FX/wexpolicy.log" || fail 'v1: a local or removed account got policy'
grep -qx 'gpo list bob' "$FX/net.log" || fail 'v1: Samba was not asked for bob'

# version 2: local|domain in column 5; the domain account's SID is the domain's, asked for by its plain name
cat > "$FX/map2" <<'EOF'
WEXACCOUNTS 2 4 1030 11 22 33 111 222 333 WEXLAB
alice 1001 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa active local 21 11 22 33 1001
bob 1002 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb active local 21 11 22 33 1002
WEXLAB\wexuser1 21104 dddddddddddddddddddddddddddddddd active domain 21 111 222 333 1104
WEXLAB\old 21105 eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee tombstone domain 21 111 222 333 1105
EOF
run_gpupdate "$FX/map2"
grep -qx 'apply /user:S-1-5-21-111-222-333-1104' "$FX/wexpolicy.log" || { cat "$FX/wexpolicy.log"; fail 'v2: the domain user was not applied under the domain SID'; }
grep -qx 'record /user:S-1-5-21-111-222-333-1104' "$FX/wexpolicy.log" || fail 'v2: the applied list was not recorded under the domain SID'
! grep -q '1002\|1001\|1105\|-11-22-33' "$FX/wexpolicy.log" || { cat "$FX/wexpolicy.log"; fail 'v2: a local or removed account got policy'; }
grep -qx 'gpo list wexuser1' "$FX/net.log" || { cat "$FX/net.log"; fail 'v2: Samba was not asked for the plain name'; }
! grep -q 'WEXLAB' "$FX/net.log" || fail 'v2: Samba was asked for a name with the domain'
echo 'OK: wex-gpupdate.sh users of a version 1 and a version 2 accounts map'
