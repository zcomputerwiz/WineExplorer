#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wex-test.sh [BASE] - the checks this worktree's changes call for, and only those (docs/TOOLING.md, "Targeted runs").
#
# Run as root in the wexbuild WSL distribution, from any worktree on D: (like wex-run.sh):
#   wsl.exe -d wexbuild -u root -- bash /mnt/d/DeepThought/Projects/<worktree>/scripts/lab/wex-test.sh [BASE]
#
# BASE (default origin/master) is the superproject commit the changes are measured from; Wine's changes are measured
# from the Wine commit BASE pins. Changed paths: committed since BASE, uncommitted and untracked, in both trees.
#   1. Each changed path is looked up in scripts/lab/test-map.txt, which names the checks it calls for.
#   2. Unless only documentation changed: an incremental sync and build (wex-run.sh build).
#   3. Every changed Wine module that has tests (dlls/X, programs/X, src/dlls/X, src/programs/X) runs its own Wine
#      tests, both architectures (make MODULE/tests/check, in a prefix of its own under /var/tmp/wex-test).
#   4. The static tests the map names (py:FILE, sh:FILE).
#   5. The suites the map names, each once: t1suite, normalsuite, lab (wex-run.sh on the build of step 2).
#   6. VM checks (image, chain, gpolab) are printed as the command to run, not run.
# WEX_TEST_PLAN=1 prints the plan (steps 1 and 6) without building or running anything.
#
# A targeted run is for iterating; it is never a merge check: before a merge the full suite and the batch run
# (docs/TOOLING.md, "Which checks when").
set -u
main() {
BASE=${1:-origin/master}
D=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
MAP=$D/scripts/lab/test-map.txt
P=/root/WE-prefetch
CL=$P/wine
MAIN=$P/build/wow64
LOGS=/var/tmp/wex-test
mkdir -p "$LOGS"
[ -f "$MAP" ] || { echo "no $MAP"; exit 2; }

# A git worktree's .git file names a Windows path (D:/...): translate it for WSL (as wex-run.sh does).
if [ -f "$D/wine/.git" ]; then
    GD=$(sed -n 's/^gitdir: //p' "$D/wine/.git" | tr -d '\r' | sed 's#^\([A-Za-z]\):#/mnt/\L\1#')
    WGIT=(git --git-dir="$GD" --work-tree="$D/wine")
else
    WGIT=(git -C "$D/wine")
fi
# the same for the superproject when it is a worktree
if [ -f "$D/.git" ]; then
    SGD=$(sed -n 's/^gitdir: //p' "$D/.git" | tr -d '\r' | sed 's#^\([A-Za-z]\):#/mnt/\L\1#')
    SGIT=(git -C "$D" --git-dir="$SGD" --work-tree="$D")
else
    SGIT=(git -C "$D")
fi
"${SGIT[@]}" rev-parse -q --verify "$BASE^{commit}" > /dev/null || { echo "unknown base $BASE"; exit 2; }

# the changed paths: superproject ones as they are, Wine's as "wine:PATH"
local super wine old changed=() p
# untracked files only in the project's folders: a scan of the whole worktree over /mnt/d (other agents' scratch
# folders included) took most of a minute
super=$( { "${SGIT[@]}" diff --name-only "$BASE" -- . ':!wine';
           "${SGIT[@]}" ls-files --others --exclude-standard -- src scripts installer tools tests docs vintner; } | sort -u)
old=$("${SGIT[@]}" rev-parse "$BASE:wine" 2>/dev/null)
if [ -n "$old" ] && "${WGIT[@]}" rev-parse -q --verify "$old^{commit}" > /dev/null 2>&1; then
    wine=$( { "${WGIT[@]}" diff --name-only "$old"; "${WGIT[@]}" ls-files --others --exclude-standard; } | sort -u)
else
    wine="server/UNKNOWN-BASE"   # the base's Wine commit is not here: treat Wine as changed everywhere in the server
    echo "NOTE: Wine commit ${old:-?} of $BASE is not in this Wine repository; assuming the server changed"
fi
for p in $super; do changed+=("$p"); done
for p in $wine; do changed+=("wine:$p"); done
echo "base $BASE (Wine ${old:0:12}); ${#changed[@]} changed paths"
[ ${#changed[@]} -gt 0 ] || { echo "nothing changed"; exit 0; }

# step 1: the map
local -A checks=() modules=()
local glob list matched c m i unmapped=0 globs=() lists=()
# the map is read once (a batch changes hundreds of paths)
while read -r glob list; do
    case $glob in ''|'#'*) continue ;; esac
    globs+=("$glob"); lists+=("$list")
done < "$MAP"
for p in "${changed[@]}"; do
    matched=
    for i in "${!globs[@]}"; do
        # shellcheck disable=SC2053
        [[ $p == ${globs[i]} ]] || continue
        matched=1
        IFS=, read -ra cs <<< "${lists[i]}"
        for c in "${cs[@]}"; do [ "$c" = none ] || checks[$c]=1; done
        break   # the first matching rule decides (the map lists specific rules before general ones)
    done
    if [ -z "$matched" ]; then
        checks[normalsuite]=1; unmapped=$((unmapped + 1))
        echo "NOTE: no rule for $p (normalsuite; add a rule to scripts/lab/test-map.txt)"
    fi
    # the Wine module the path belongs to
    m=
    case $p in
        wine:dlls/*/*|wine:programs/*/*) m=${p#wine:} ;;
        src/dlls/*/*|src/programs/*/*) m=${p#src/} ;;
    esac
    if [ -n "$m" ]; then local top=${m%%/*} rest=${m#*/}; m=$top/${rest%%/*}; fi
    [ -z "$m" ] || modules[$m]=1
done
echo "modules: ${!modules[*]}"
echo "checks: ${!checks[*]}"

local only_docs=1
[ ${#modules[@]} = 0 ] && [ ${#checks[@]} = 0 ] || only_docs=
if [ -n "$only_docs" ]; then echo "TARGETED PASS (documentation only, nothing to run)"; exit 0; fi
if [ "${WEX_TEST_PLAN:-0}" = 1 ]; then
    print_vm
    echo "plan only (WEX_TEST_PLAN=1)"
    exit 0
fi

local rc=0 t0=$SECONDS xvfb=()
command -v xvfb-run > /dev/null && xvfb=(xvfb-run -a)
# step 2: the build, when anything below needs one (static tests and printed VM checks do not)
local need_build=
[ ${#modules[@]} = 0 ] || need_build=1
for c in t1suite normalsuite lab; do [ -z "${checks[$c]:-}" ] || need_build=1; done
if [ -n "$need_build" ]; then
    bash "$D/scripts/lab/wex-run.sh" build || { echo "TARGETED FAILED: build"; exit 1; }
fi

# step 3: each module's own Wine tests (make runs only the test files whose programs changed since their last pass)
for m in "${!modules[@]}"; do
    [ -d "$CL/$m/tests" ] || continue
    mkdir -p "$LOGS/prefix"
    if WINEPREFIX=$LOGS/prefix WINEDEBUG=-all timeout 1800 "${xvfb[@]}" \
           bash "$D/scripts/lab/wex-make.sh" -C "$MAIN" -k "$m/tests/check" > "$LOGS/$(echo "$m" | tr / -).log" 2>&1; then
        echo "tests $m ok"
    else
        echo "tests $m FAILED ($LOGS/$(echo "$m" | tr / -).log):"
        grep -E 'Test failed|Test succeeded inside todo|timeout|\*\*\*' "$LOGS/$(echo "$m" | tr / -).log" | head -8
        rc=1
    fi
done

# step 4: the static tests the map names (py:FILE: python3 FILE; sh:FILE: bash FILE; from the worktree, whose scripts
# have LF line ends: .gitattributes)
local f
for c in "${!checks[@]}"; do
    case $c in
        py:*) f=${c#py:}
              (cd "$D" && timeout 900 python3 "$f") > "$LOGS/$(basename "$f").log" 2>&1 ;;
        sh:*) f=${c#sh:}
              (cd "$D" && timeout 900 bash "$f") > "$LOGS/$(basename "$f").log" 2>&1 ;;
        *) continue ;;
    esac
    if [ $? = 0 ]; then
        echo "$f ok"
    else
        echo "$f FAILED ($LOGS/$(basename "$f").log):"
        grep -E '^(FAIL|ERROR|not ok)' "$LOGS/$(basename "$f").log" | head -8
        rc=1
    fi
done

# step 5: the suites, on that build
for c in t1suite normalsuite; do
    [ -n "${checks[$c]:-}" ] || continue
    # the suite's own status decides, not grep's (a failed suite once read as a pass through the pipe; Codex review)
    WEX_ANY_BUILD=1 bash "$D/scripts/lab/wex-run.sh" "$c" | grep -v '^NOTE: WEX_ANY_BUILD'
    [ "${PIPESTATUS[0]}" = 0 ] || rc=1
done
if [ -n "${checks[lab]:-}" ]; then bash "$D/scripts/lab/wex-run.sh" lab || rc=1; fi

# step 6: the VM checks, printed
print_vm
echo "TARGETED $([ $rc = 0 ] && echo PASS || echo FAILED) ($(( (SECONDS - t0 + 30) / 60 )) min; not a merge check)"
exit $rc
}

print_vm()
{
    local rev
    rev=$("${SGIT[@]}" rev-parse HEAD)
    if [ -n "${checks[chain]:-}" ]; then
        echo "VM: the whole chain (wex-batch.sh $rev NAME), or one stage onto an installed disk with installer/tests/quick-run.sh"
    elif [ -n "${checks[image]:-}" ]; then
        echo "VM: the image smoke check (WEX_BATCH_FWS=uefi wex-batch.sh $rev NAME rundialog), or installer/tests/quick-run.sh"
    fi
    if [ -n "${checks[gpolab]:-}" ]; then
        echo "VM: the GPO lab's installed-machine guests on wexgpo (docs/GPO_LAB.md): gpo-lab-run.sh machines SOURCE_VM [GUEST...]," \
             "with WEX_LAB_TREE_* for scripts tested without a new image"
    fi
}

main "$@"
exit $?
