#!/usr/bin/env bash
# Rework Task 2: run the SAME PEs under the PROJECT candidate wine (g4 fork, wine-11.19).
# Runs the primary single-handle repro, then the multi-handle probe.
B=/var/tmp/wex-g4-token-parent-20261027/build
OUTDIR=/home/debian/navhandoff
export WINEPREFIX=/home/debian/navhandoff/wineprefix-project
export DISPLAY=""
export WINEDLLOVERRIDES="mscoree,mshtml="
cd "$OUTDIR" || exit 9
echo "-- version --"; timeout 30 "$B/loader/wine" --version 2>&1 | head -1

run_one () {
  local exe="$1" log="$2" tag="$3"
  rm -f "mojo_np_child.log" "mojo_np_multi_child.log" "wine_${tag}_stdout.txt"
  timeout 60 "$B/loader/wine" "$OUTDIR/$exe" > "wine_${tag}_stdout.txt" 2>&1
  echo "== $tag exit=$? =="
  echo "--- parent stdout ($tag) ---"; cat "wine_${tag}_stdout.txt"
  echo "--- child log ($tag) ---"
  if [ -f "$log" ]; then cat "$log"; else echo "(no child log)"; fi
}

run_one mojo_np.exe        mojo_np_child.log       primary
run_one mojo_np_multi.exe  mojo_np_multi_child.log multi
echo "=== done ==="
