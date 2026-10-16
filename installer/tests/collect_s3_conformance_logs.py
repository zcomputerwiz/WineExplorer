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
TEST_PFX = "/tmp/wine_s3_conformance_pfx"

os.makedirs(LOG_DIR, exist_ok=True)

ENV = os.environ.copy()
ENV["WINEPREFIX"] = TEST_PFX
ENV["WINELINKFAKEDLLS"] = "1"
ENV["WINEDLLOVERRIDES"] = "mscoree,mshtml=;winemenubuilder.exe=d"
ENV["WINEDEBUG"] = "-all"

TESTS = [
    # (module, subtest, arch, exe_subpath, log_name)
    ("kernel32", "file", "64", "dlls/kernel32/tests/x86_64-windows/kernel32_test.exe", "s3_kernel32_file_64.log"),
    ("kernel32", "file", "32", "dlls/kernel32/tests/i386-windows/kernel32_test.exe", "s3_kernel32_file_32.log"),
    ("setupapi", "install", "64", "dlls/setupapi/tests/x86_64-windows/setupapi_test.exe", "s3_setupapi_install_64.log"),
    ("setupapi", "install", "32", "dlls/setupapi/tests/i386-windows/setupapi_test.exe", "s3_setupapi_install_32.log"),
    ("setupapi", "parser", "64", "dlls/setupapi/tests/x86_64-windows/setupapi_test.exe", "s3_setupapi_parser_64.log"),
    ("setupapi", "parser", "32", "dlls/setupapi/tests/i386-windows/setupapi_test.exe", "s3_setupapi_parser_32.log"),
    ("uxtheme", "system", "64", "dlls/uxtheme/tests/x86_64-windows/uxtheme_test.exe", "s3_uxtheme_system_64.log"),
    ("uxtheme", "system", "32", "dlls/uxtheme/tests/i386-windows/uxtheme_test.exe", "s3_uxtheme_system_32.log"),
]

def init_prefix():
    if os.path.exists(TEST_PFX):
        shutil.rmtree(TEST_PFX)
    print(f"Initializing conformance prefix at {TEST_PFX} (WINELINKFAKEDLLS=1)...")
    subprocess.run(["wineboot", "-i"], env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    subprocess.run(["wineserver", "-w"], env=ENV, check=True)
    print("Prefix initialized successfully.")

def run_tests():
    init_prefix()
    results = {}
    for module, subtest, arch, exe_rel, log_filename in TESTS:
        exe_path = os.path.join(BUILD_DIR, exe_rel)
        log_path = os.path.join(LOG_DIR, log_filename)
        print(f"\n--- Running {module}:{subtest} ({arch}-bit) ---")
        cmd = ["xvfb-run", "-a", "wine", exe_path, subtest]
        proc = subprocess.run(cmd, env=ENV, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        with open(log_path, "w") as f:
            f.write(proc.stdout)
        
        # Extract summary line
        summary = "No summary line found"
        for line in reversed(proc.stdout.splitlines()):
            if "tests executed" in line:
                summary = line.strip()
                break
        results[f"{module}:{subtest}_{arch}"] = summary
        print(f"Result ({log_filename}): {summary}")
        # Stop wineserver cleanly between suites
        subprocess.run(["wineserver", "-k"], env=ENV, stderr=subprocess.DEVNULL)
        subprocess.run(["wineserver", "-w"], env=ENV, stderr=subprocess.DEVNULL)

    print("\n=================== S3 CONFORMANCE TEST RESULTS ===================")
    for k, v in results.items():
        print(f"{k:30}: {v}")

if __name__ == "__main__":
    run_tests()
