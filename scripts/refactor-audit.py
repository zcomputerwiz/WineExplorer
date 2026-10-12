#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Inventory committed Wine changes against a real upstream tag, including modes.

The external bare repository must contain the upstream tag and the project's
commit (fetch the latter into refs/audit/wineexplorer). No working files are read.
Classification is deliberately separate from evidence; suggestions are not review.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def git(repository, *args):
    return subprocess.check_output(["git", f"--git-dir={repository}", *args])


def tree(repository, revision):
    entries = {}
    for item in git(repository, "ls-tree", "-rz", revision).split(b"\0"):
        if not item:
            continue
        metadata, path = item.split(b"\t", 1)
        mode, kind, oid = metadata.decode().split()
        entries[path.decode("utf-8")] = {"mode": mode, "kind": kind, "oid": oid}
    return entries


def sha(data):
    return hashlib.sha256(data).hexdigest()


def inventory(repository, baseline, source, imported, master):
    revisions = {
        "baseline": git(repository, "rev-parse", f"{baseline}^{{commit}}").decode().strip(),
        "source": git(repository, "rev-parse", f"{source}^{{commit}}").decode().strip(),
        "import": git(repository, "rev-parse", f"{imported}^{{commit}}").decode().strip(),
        "fork_master": git(repository, "rev-parse", f"{master}^{{commit}}").decode().strip(),
    }
    old = tree(repository, revisions["baseline"])
    new = tree(repository, revisions["source"] + ":wine")
    initial = tree(repository, revisions["import"] + ":wine")
    upstream = tree(repository, revisions["fork_master"])
    upstream_objects = set()
    for line in git(repository, "rev-list", "--objects", revisions["fork_master"],
                    "^" + revisions["baseline"]).decode().splitlines():
        fields = line.split(" ", 1)
        if len(fields) == 2:
            upstream_objects.add((fields[1], fields[0]))
    patch = git(repository, "diff", "--no-ext-diff", "--no-renames", "--binary",
                revisions["baseline"], revisions["source"] + ":wine", "--")
    blocks = {}
    for block in re.split(br"(?=^diff --git )", patch, flags=re.M):
        if not block:
            continue
        first = block.split(b"\n", 1)[0].decode()
        # Wine paths have no quoted/non-ASCII names; fail instead of misindexing.
        match = re.fullmatch(r"diff --git a/(.+) b/\1", first)
        if not match:
            raise ValueError(f"Unsupported patch path: {first}")
        blocks[match[1]] = block
    records = []
    for path in sorted(set(old) | set(new)):
        before, after = old.get(path), new.get(path)
        if before == after:
            continue
        block = blocks.pop(path)
        hunks = []
        for hunk in re.split(br"(?=^@@ )", block, flags=re.M)[1:]:
            lines = hunk.splitlines()
            hunks.append({"header": lines[0].decode("utf-8", "replace"),
                          "sha256": sha(hunk),
                          "added": sum(line.startswith(b"+") for line in lines[1:]),
                          "removed": sum(line.startswith(b"-") for line in lines[1:])})
        mode_only = bool(before and after and before["oid"] == after["oid"])
        inherited = after == initial.get(path)
        present_upstream = after == upstream.get(path)
        records.append({"path": path, "before": before, "after": after,
                        "import": initial.get(path), "fork_master": upstream.get(path),
                        "mode_only": mode_only, "unchanged_since_import": inherited,
                        "matches_fork_master": present_upstream,
                        "upstream_blob_known": bool(after and
                            ((path, after["oid"]) in upstream_objects or
                             (upstream.get(path) and after["oid"] == upstream[path]["oid"]))),
                        "patch_sha256": sha(block), "patch_lines": len(block.splitlines()),
                        "hunks": hunks})
    if blocks:
        raise ValueError(f"Unaccounted patch blocks: {list(blocks)}")
    return {"schema": 1, "revisions": revisions, "patch_sha256": sha(patch),
            "patch_lines": len(patch.splitlines()), "files": records}, patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--git-dir", required=True)
    parser.add_argument("--baseline", default="wine-11.19")
    parser.add_argument("--source", default="refs/audit/wineexplorer")
    parser.add_argument("--import", dest="imported", default="40c17e1")
    parser.add_argument("--master", default="master")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verify", action="store_true",
                        help="Compare regenerated evidence against existing JSON and patch")
    args = parser.parse_args()
    result, patch = inventory(args.git_dir, args.baseline, args.source,
                              args.imported, args.master)
    json_path = args.output.with_suffix(".json")
    patch_path = args.output.with_suffix(".patch")
    if args.verify:
        if json.loads(json_path.read_text(encoding="utf-8")) != result:
            raise SystemExit("Inventory changed: source or baseline differs")
        if patch_path.read_bytes() != patch:
            raise SystemExit("Saved patch does not match Git objects")
    else:
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        patch_path.write_bytes(patch)
    files = result["files"]
    print(json.dumps({"revisions": result["revisions"], "files": len(files),
                      "hunks": sum(len(file["hunks"]) for file in files),
                      "mode_only": sum(file["mode_only"] for file in files),
                      "unchanged_since_import": sum(file["unchanged_since_import"] for file in files),
                      "matches_fork_master": sum(file["matches_fork_master"] for file in files),
                      "patch_lines": result["patch_lines"]}, indent=2))


if __name__ == "__main__":
    main()
