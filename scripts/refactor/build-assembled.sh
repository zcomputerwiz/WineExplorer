#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Isolated P2 verification only; this script never installs Wine.
set -euo pipefail
root=${1:?usage: build-assembled.sh root patch patch-sha256 base-commit assembled-tree}
patch=${2:?missing binary patch}
expected=${3:?missing patch hash}
base=${4:?missing base commit}
assembled=${5:?missing assembled tree}
test "$root" = /srv/protonexplorer/wex-refactor-p2
mkdir -p "$root"
exec 9>"$root/build.lock"
flock -n 9
trap 'printf "%s\n" BUILD_FAILED > "$root/build.status"' ERR
printf '%s\n' BUILD_PREPARING > "$root/build.status"
printf '%s  %s\n' "$expected" "$patch" | sha256sum --check -
if [ ! -d "$root/source" ]; then
    git -C "$HOME/wex-refactor/upstream" worktree add --detach "$root/source" "$base"
    git -C "$root/source" apply --check "$patch"
    git -C "$root/source" apply "$patch"
    git -C "$root/source" add -A
    test "$(git -C "$root/source" write-tree)" = "$assembled"
    git -C "$root/source" -c user.name=zcomputerwiz -c user.email=zcomputerwiz@users.noreply.github.com commit -m 'P2 isolated assembled verification tree.'
fi
test "$(git -C "$root/source" rev-parse 'HEAD^{tree}')" = "$assembled"
# A repeated build may retain only the regenerated configure from the prior run.
test -z "$(git -C "$root/source" status --porcelain -- . ':!configure')"
cd "$root/source"
autoconf -f
mkdir -p "$root/build"
cd "$root/build"
CFLAGS='-O2 -pipe' CXXFLAGS='-O2 -pipe' "$root/source/configure" --enable-win64 --prefix="$root/install"
printf '%s\n' BUILD_RUNNING > "$root/build.status"
make -j8
printf '%s\n' BUILD_PASSED > "$root/build.status"
