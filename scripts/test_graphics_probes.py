#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Automated build and test harness for WineExplorer graphics diagnostic probes.

Compiles standalone Win32 PE probes for both x86_64 and i686 architectures,
executes them under Wine, and asserts the baseline capabilities and exact
stub behaviors without masking errors.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def compile_probe(source_path: Path, output_exe: Path, compiler: str) -> None:
    """Compile the C++ probe with MinGW using static runtimes."""
    if not shutil.which(compiler):
        raise RuntimeError(f"Compiler '{compiler}' not found in PATH")

    cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-static-libgcc",
        "-static-libstdc++",
        str(source_path),
        "-o",
        str(output_exe),
        "-ld3d11",
        "-ldxgi",
        "-lole32",
        "-luuid",
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(
            f"Compilation failed with {compiler} (code {res.returncode}):\n{res.stderr}\n{res.stdout}"
        )


def run_probe(exe_path: Path, wine_bin: str = "wine") -> dict:
    """Run the probe executable under Wine and parse its JSON output."""
    if not shutil.which(wine_bin):
        raise RuntimeError(f"Wine binary '{wine_bin}' not found in PATH")

    env = os.environ.copy()
    env["WINEDEBUG"] = "-all"
    env["WINEDLLOVERRIDES"] = "mscoree,mshtml=;winemenubuilder.exe=d"

    cmd = [wine_bin, str(exe_path), "--json"]
    res = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(
            f"Probe execution failed (exit code {res.returncode}):\n{res.stderr}\n{res.stdout}"
        )

    # Find the JSON object in stdout (skip any driver warnings printed to stdout)
    raw_stdout = res.stdout
    json_start = raw_stdout.find("{")
    json_end = raw_stdout.rfind("}")
    if json_start == -1 or json_end == -1 or json_end < json_start:
        raise ValueError(f"No valid JSON object found in probe output:\n{raw_stdout}")

    json_str = raw_stdout[json_start : json_end + 1]
    return json.loads(json_str)


def assert_baseline_expectations(data: dict, arch: str) -> None:
    """Verify known baseline expectations for Phase G0."""
    p1 = data.get("probe1_d3d11", {})
    p2 = data.get("probe2_dcomp", {})
    p3 = data.get("probe3_sharing", {})

    # Probe 1: D3D11 HWND swapchain must succeed
    assert p1.get("device_created") is True, f"[{arch}] D3D11 device creation failed: {p1}"
    assert p1.get("swapchain_created") is True, f"[{arch}] Swapchain creation failed: {p1}"
    assert p1.get("present_succeeded") is True, f"[{arch}] HWND swapchain present failed: {p1}"

    # Probe 2: DirectComposition entry points must exist in dcomp.dll and return E_NOTIMPL (0x80004001) in baseline
    assert p2.get("dll_loaded") is True, f"[{arch}] dcomp.dll failed to load: {p2}"
    assert p2.get("dcomp_create_device_exported") is True, f"[{arch}] DCompositionCreateDevice missing"
    assert p2.get("dcomp_create_device2_exported") is True, f"[{arch}] DCompositionCreateDevice2 missing"
    assert p2.get("dcomp_create_device3_exported") is True, f"[{arch}] DCompositionCreateDevice3 missing"
    assert p2.get("dcomp_create_device_hr") == "0x80004001", f"[{arch}] Expected E_NOTIMPL for DCompCreateDevice"
    assert p2.get("dcomp_create_device2_hr") == "0x80004001", f"[{arch}] Expected E_NOTIMPL for DCompCreateDevice2"
    assert p2.get("dcomp_create_device3_hr") == "0x80004001", f"[{arch}] Expected E_NOTIMPL for DCompCreateDevice3"
    assert p2.get("composition_swapchain_hr") == "0x80004001", f"[{arch}] Expected E_NOTIMPL for composition swapchain"

    # Probe 3: Resource sharing checks
    assert p3.get("create_fence_hr") == "0x80004001", f"[{arch}] Expected E_NOTIMPL for D3D11 fence in baseline"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source",
        type=Path,
        default=Path(__file__).parent / "test_graphics_probes.cpp",
        help="Path to test_graphics_probes.cpp",
    )
    parser.add_argument(
        "--output-json",
        type=Path,
        default=None,
        help="Path to save aggregated JSON test results",
    )
    parser.add_argument(
        "--wine",
        default="/usr/local/bin/wine" if Path("/usr/local/bin/wine").exists() else "wine",
        help="Path or name of wine binary",
    )
    args = parser.parse_args()

    results = {}
    with tempfile.TemporaryDirectory(prefix="wex_graphics_probe_") as tmp_dir:
        tmp_path = Path(tmp_dir)

        # 64-bit probe
        exe64 = tmp_path / "graphics_probe64.exe"
        print("Compiling x86_64 probe with x86_64-w64-mingw32-g++...", flush=True)
        compile_probe(args.source, exe64, "x86_64-w64-mingw32-g++")
        print("Executing x86_64 probe under Wine...", flush=True)
        res64 = run_probe(exe64, args.wine)
        assert_baseline_expectations(res64, "x86_64")
        results["x86_64"] = res64
        print(f"  [x86_64] D3D11 Feature Level: {res64['probe1_d3d11']['feature_level']}, Adapter: {res64['probe1_d3d11']['adapter']}")
        print(f"  [x86_64] DComp Entry Points: E_NOTIMPL (verified), Composition SwapChain: E_NOTIMPL (verified)")

        # 32-bit probe
        exe32 = tmp_path / "graphics_probe32.exe"
        print("Compiling i686 probe with i686-w64-mingw32-g++...", flush=True)
        compile_probe(args.source, exe32, "i686-w64-mingw32-g++")
        print("Executing i686 probe under Wine...", flush=True)
        res32 = run_probe(exe32, args.wine)
        assert_baseline_expectations(res32, "i686")
        results["i686"] = res32
        print(f"  [i686]   D3D11 Feature Level: {res32['probe1_d3d11']['feature_level']}, Adapter: {res32['probe1_d3d11']['adapter']}")
        print(f"  [i686]   DComp Entry Points: E_NOTIMPL (verified), Composition SwapChain: E_NOTIMPL (verified)")

    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(results, indent=2) + "\n")
        print(f"Aggregated results saved to {args.output_json}")

    print("\nALL ARCHITECTURES VERIFIED: Baseline probes passed with 0 errors.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
