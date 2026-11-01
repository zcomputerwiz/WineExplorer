#!/usr/bin/env bash
# Build the Chromium-matching named-pipe repro PE with mingw (x86_64).
set -e
SRC="/mnt/d/DeepThought/Projects/OpenClaude/ac_build/navhandoff_mojo_np.c"
OUTDIR=/home/debian/navhandoff
mkdir -p "$OUTDIR"
# Copy source to fast disk first.
cp "$SRC" "$OUTDIR/navhandoff_mojo_np.c"

CC=""
for c in x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc; do
  if command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
done
echo "CC=$CC"
if [ -z "$CC" ]; then echo "mingw not found in Debian distro"; exit 2; fi
"$CC" --version | head -1
# Primary: pinned single-pipe-handle primitive.
cp /mnt/d/DeepThought/Projects/OpenClaude/ac_build/navhandoff_mojo_np.c "$OUTDIR/navhandoff_mojo_np.c"
"$CC" -O2 -static -o "$OUTDIR/mojo_np.exe" "$OUTDIR/navhandoff_mojo_np.c"
# Extended probe: multi-handle HANDLE_LIST.
cp /mnt/d/DeepThought/Projects/OpenClaude/ac_build/navhandoff_mojo_np_multi.c "$OUTDIR/navhandoff_mojo_np_multi.c"
"$CC" -O2 -static -o "$OUTDIR/mojo_np_multi.exe" "$OUTDIR/navhandoff_mojo_np_multi.c"
echo "-- output --"
file "$OUTDIR/mojo_np.exe" "$OUTDIR/mojo_np_multi.exe"
ls -la "$OUTDIR/mojo_np.exe" "$OUTDIR/mojo_np_multi.exe"
echo "=== build done ==="
