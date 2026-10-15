#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors

import os
import sys
import subprocess
import shutil

ROOT_DIR = "/root/WineExplorer"
LOG_DIR = os.path.join(ROOT_DIR, "docs/prefix-split/logs")
BUILD_DIR = os.path.join(ROOT_DIR, "build/wow64")

os.makedirs(LOG_DIR, exist_ok=True)

PIN_PFX = "/tmp/wine_pin_pfx"
BRANCH_PFX = "/tmp/wine_branch_pfx"

ENV_BASE = os.environ.copy()
ENV_BASE["WINEDLLOVERRIDES"] = "mscoree,mshtml="

TESTS = [
    ("kernel32", "file", os.path.join(BUILD_DIR, "dlls/kernel32/tests/x86_64-windows/kernel32_test.exe"), "kernel32_file"),
    ("setupapi", "install", os.path.join(BUILD_DIR, "dlls/setupapi/tests/x86_64-windows/setupapi_test.exe"), "setupapi_install"),
    ("setupapi", "parser", os.path.join(BUILD_DIR, "dlls/setupapi/tests/x86_64-windows/setupapi_test.exe"), "setupapi_parser"),
    ("uxtheme", "system", os.path.join(BUILD_DIR, "dlls/uxtheme/tests/x86_64-windows/uxtheme_test.exe"), "uxtheme_system"),
]

def init_prefix(pfx_path, use_symlinks):
    if os.path.exists(pfx_path):
        shutil.rmtree(pfx_path)
    env = ENV_BASE.copy()
    env["WINEPREFIX"] = pfx_path
    if use_symlinks:
        env["WINELINKFAKEDLLS"] = "1"
    else:
        env.pop("WINELINKFAKEDLLS", None)

    print(f"Initializing prefix {pfx_path} (WINELINKFAKEDLLS={use_symlinks})...")
    subprocess.run(["wineboot", "-i"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    subprocess.run(["wineserver", "-w"], env=env, check=True)

def run_tests_for_target(target_name, pfx_path, use_symlinks):
    init_prefix(pfx_path, use_symlinks)
    env = ENV_BASE.copy()
    env["WINEPREFIX"] = pfx_path
    if use_symlinks:
        env["WINELINKFAKEDLLS"] = "1"
    else:
        env.pop("WINELINKFAKEDLLS", None)

    summaries = {}
    for module, subtest, exe_path, log_base in TESTS:
        log_file = os.path.join(LOG_DIR, f"{log_base}_{target_name}.log")
        print(f"Running {module}:{subtest} for {target_name} -> {log_file}...")
        cmd = ["wine", exe_path, subtest]
        result = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        with open(log_file, "w") as f:
            f.write(result.stdout)
        
        # Extract summary line (last line containing 'tests executed')
        summary = "No summary line found"
        for line in reversed(result.stdout.splitlines()):
            if "tests executed" in line:
                summary = line.strip()
                break
        summaries[f"{module}:{subtest}"] = summary
        print(f"  {module}:{subtest} ({target_name}): {summary}")
    return summaries

def main():
    print("=== Running Pin (Stock prefix without symlinks) ===")
    pin_results = run_tests_for_target("pin", PIN_PFX, use_symlinks=False)

    print("\n=== Running Branch (Prefix split with WINELINKFAKEDLLS=1) ===")
    branch_results = run_tests_for_target("branch", BRANCH_PFX, use_symlinks=True)

    print("\n=================== CONFORMANCE TEST COMPARISON ===================")
    for key in pin_results:
        print(f"[{key}]")
        print(f"  Pin:    {pin_results[key]}")
        print(f"  Branch: {branch_results[key]}")

if __name__ == "__main__":
    main()
