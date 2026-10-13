#!/bin/bash
# Strip CRs from modified/untracked text sources under wine/ (Windows tools like to write CRLF; the tree is LF).
cd "$(dirname "$0")/.."
{ git diff --name-only --diff-filter=AM HEAD -- wine src scripts docs; git ls-files --others --exclude-standard -- wine src scripts docs; } 2>/dev/null |
  grep -E '\.(c|h|rc|in|idl|spec|sh|py|md|manifest|theme|txt)$' | sort -u |
  while read -r f; do grep -q $'\r' "$f" 2>/dev/null && sed -i 's/\r$//' "$f" && echo "lf: $f"; done
exit 0
