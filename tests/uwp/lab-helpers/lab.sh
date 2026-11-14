#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Host-side helper (Git Bash): drive the owned U1 guest through its controller.
#   lab.sh ssh 'command'            run a command in the guest (55 s limit)
#   lab.sh push FILE [GUEST_NAME]   copy a scratchpad file into the guest workspace's claude/ directory
#   lab.sh delta NAME               tar the modified and new files of the UWP worktree and push them as NAME.tar
set -euo pipefail
export MSYS_NO_PATHCONV=1
ctl=/mnt/d/DeepThought/Projects/WineExplorer/scripts/lab/uwp/owned-lab.py
pad_win="/c/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad"
pad_wsl="/mnt/c/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad"
ws=/home/debian/uwp-u1-20261111
wt="C:/Users/Owner/.codex/worktrees/uwp-runtime/wine"
lab() { wsl.exe --distribution Debian --user root --exec python3 "$ctl" "$@"; }
case "$1" in
ssh) lab ssh "$2" ;;
push) lab push "$pad_wsl/$2" "$ws/claude/${3:-$2}" ;;
fixture)
    src=/d/DeepThought/Projects/WineExplorer/tests/uwp/u1_0
    rm -rf "$pad_win/fixture"
    mkdir -p "$pad_win/fixture/x64" "$pad_win/fixture/x86"
    cp "$src"/build/x64/{probe.exe,component.dll,launcher.exe} "$pad_win/fixture/x64/"
    cp "$src"/build/x86/{probe.exe,component.dll,launcher.exe} "$pad_win/fixture/x86/"
    cp "$src"/{register-wine.py,run-wine-packaged.sh,context-negatives.c,AppxManifest.xml,expected.txt,observations.txt} "$pad_win/fixture/"
    (cd "$pad_win/fixture" && sha256sum x64/* x86/* > SHA256SUMS)
    (cd "$pad_win" && tar -cf fixture.tar fixture)
    lab push "$pad_wsl/fixture.tar" "$ws/claude/fixture.tar"
    lab ssh "cd $ws/claude && rm -rf fixture && tar -xf fixture.tar && cd fixture && sha256sum -c --quiet SHA256SUMS && echo fixture-ok"
    ;;
delta)
    cd "$wt"
    git ls-files -m -o --exclude-standard > "$pad_win/$2.list"
    git ls-files -d > "$pad_win/$2.tar.deleted"
    grep -v -x -F -f "$pad_win/$2.tar.deleted" "$pad_win/$2.list" > "$pad_win/$2.files" || true
    tar -cf "$pad_win/$2.tar" -T "$pad_win/$2.files"
    wc -l < "$pad_win/$2.files"
    lab push "$pad_wsl/$2.tar" "$ws/claude/$2.tar"
    lab push "$pad_wsl/$2.tar.deleted" "$ws/claude/$2.tar.deleted"
    ;;
esac
