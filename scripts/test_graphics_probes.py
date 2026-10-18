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

    # Probe 2: DirectComposition entry points must exist in dcomp.dll and return E_NOTIMPL (0x80004001) in baseline or S_OK (0x00000000) in G2+
    assert p2.get("dll_loaded") is True, f"[{arch}] dcomp.dll failed to load: {p2}"
    assert p2.get("dcomp_create_device_exported") is True, f"[{arch}] DCompositionCreateDevice missing"
    assert p2.get("dcomp_create_device2_exported") is True, f"[{arch}] DCompositionCreateDevice2 missing"
    assert p2.get("dcomp_create_device3_exported") is True, f"[{arch}] DCompositionCreateDevice3 missing"
    assert p2.get("dcomp_create_device_hr") in ("0x80004001", "0x00000000"), f"[{arch}] Expected E_NOTIMPL or S_OK for DCompCreateDevice"
    assert p2.get("dcomp_create_device2_hr") in ("0x80004001", "0x00000000"), f"[{arch}] Expected E_NOTIMPL or S_OK for DCompCreateDevice2"
    assert p2.get("dcomp_create_device3_hr") in ("0x80004001", "0x00000000"), f"[{arch}] Expected E_NOTIMPL or S_OK for DCompCreateDevice3"
    assert p2.get("composition_swapchain_hr") in ("0x80004001", "0x00000000"), f"[{arch}] Expected E_NOTIMPL or S_OK for composition swapchain"

    # Probe 3: Resource sharing checks
    assert p3.get("create_fence_hr") == "0x80004001", f"[{arch}] Expected E_NOTIMPL for D3D11 fence in baseline"


def assert_g1_expectations(data: dict, arch: str) -> None:
    """Verify non-DComp animated presentation expectations for Phase G1."""
    assert data.get("d3d11_device_created") is True, f"[{arch}] G1 D3D11 device creation failed: {data}"
    assert data.get("parent_hwnd_created") is True, f"[{arch}] G1 parent HWND creation failed"
    assert data.get("child_hwnd_created") is True, f"[{arch}] G1 child HWND creation failed"
    assert data.get("swapchain_created") is True, f"[{arch}] G1 swapchain creation failed: {data}"
    assert data.get("frames_rendered") == data.get("frames_presented"), f"[{arch}] G1 frames rendered != presented"
    assert data.get("frames_presented", 0) >= 30, f"[{arch}] G1 insufficient presented frames: {data}"
    assert data.get("last_present_hr") == "0x00000000", f"[{arch}] G1 present failed: {data}"
    assert data.get("resize_succeeded") is True, f"[{arch}] G1 dynamic resize failed: {data}"


def assert_g2_expectations(data: dict, arch: str) -> None:
    """Verify DirectComposition animated retained visual presentation expectations for Phase G2."""
    assert data.get("d3d11_device_created") is True, f"[{arch}] G2 D3D11 device creation failed: {data}"
    assert data.get("parent_hwnd_created") is True, f"[{arch}] G2 parent HWND creation failed"
    assert data.get("child_hwnd_created") is True, f"[{arch}] G2 child HWND creation failed"
    assert data.get("dcomp_dll_loaded") is True, f"[{arch}] G2 dcomp.dll load failed"
    assert data.get("dcomp_create_device_hr") == "0x00000000", f"[{arch}] G2 DCompositionCreateDevice failed: {data}"
    assert data.get("composition_swapchain_created") is True, f"[{arch}] G2 composition swapchain failed: {data}"
    assert data.get("composition_swapchain_hr") == "0x00000000", f"[{arch}] G2 composition swapchain hr failed: {data}"
    assert data.get("dcomp_target_created") is True, f"[{arch}] G2 target creation failed: {data}"
    assert data.get("dcomp_target_hr") == "0x00000000", f"[{arch}] G2 target hr failed: {data}"
    assert data.get("dcomp_visual_created") is True, f"[{arch}] G2 visual creation failed: {data}"
    assert data.get("dcomp_visual_hr") == "0x00000000", f"[{arch}] G2 visual hr failed: {data}"
    assert data.get("set_content_hr") == "0x00000000", f"[{arch}] G2 set_content failed: {data}"
    assert data.get("set_root_hr") == "0x00000000", f"[{arch}] G2 set_root failed: {data}"
    assert data.get("commit_hr") == "0x00000000", f"[{arch}] G2 commit failed: {data}"
    assert data.get("commit_count") == 1, f"[{arch}] G2 commit count must be exactly 1: {data}"
    assert data.get("frames_rendered") == data.get("frames_presented"), f"[{arch}] G2 frames rendered != presented"
    assert data.get("frames_presented", 0) >= 60, f"[{arch}] G2 insufficient presented frames: {data}"
    assert data.get("last_present_hr") == "0x00000000", f"[{arch}] G2 present failed: {data}"
    assert data.get("retained_tree_verified") is True, f"[{arch}] G2 retained visual tree not verified: {data}"
    assert data.get("resize_succeeded") is True, f"[{arch}] G2 dynamic resize failed: {data}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-g0",
        type=Path,
        default=Path(__file__).parent / "test_graphics_probes.cpp",
        help="Path to Phase G0 test_graphics_probes.cpp",
    )
    parser.add_argument(
        "--source-g1",
        type=Path,
        default=Path(__file__).parent / "test_g1_non_dcomp_probe.cpp",
        help="Path to Phase G1 test_g1_non_dcomp_probe.cpp",
    )
    parser.add_argument(
        "--source-g2",
        type=Path,
        default=Path(__file__).parent / "test_g2_dcomp_probe.cpp",
        help="Path to Phase G2 test_g2_dcomp_probe.cpp",
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

    results = {"g0_baseline": {}, "g1_non_dcomp": {}, "g2_dcomp": {}}
    with tempfile.TemporaryDirectory(prefix="wex_graphics_probe_") as tmp_dir:
        tmp_path = Path(tmp_dir)

        # Phase G0: 64-bit baseline probe
        exe_g0_64 = tmp_path / "graphics_probe64.exe"
        print("--- Phase G0: Baseline Diagnostic Probes ---", flush=True)
        print("Compiling x86_64 G0 probe with x86_64-w64-mingw32-g++...", flush=True)
        compile_probe(args.source_g0, exe_g0_64, "x86_64-w64-mingw32-g++")
        print("Executing x86_64 G0 probe under Wine...", flush=True)
        res_g0_64 = run_probe(exe_g0_64, args.wine)
        assert_baseline_expectations(res_g0_64, "x86_64")
        results["g0_baseline"]["x86_64"] = res_g0_64
        dcomp_status = "S_OK (implemented)" if res_g0_64['probe2_dcomp']['dcomp_create_device_hr'] == "0x00000000" else "E_NOTIMPL (baseline)"
        print(f"  [x86_64 G0] D3D11 Feature Level: {res_g0_64['probe1_d3d11']['feature_level']}, Adapter: {res_g0_64['probe1_d3d11']['adapter']}")
        print(f"  [x86_64 G0] DComp Entry Points: {dcomp_status}, Composition SwapChain: {dcomp_status}")

        # Phase G0: 32-bit baseline probe
        exe_g0_32 = tmp_path / "graphics_probe32.exe"
        print("Compiling i686 G0 probe with i686-w64-mingw32-g++...", flush=True)
        compile_probe(args.source_g0, exe_g0_32, "i686-w64-mingw32-g++")
        print("Executing i686 G0 probe under Wine...", flush=True)
        res_g0_32 = run_probe(exe_g0_32, args.wine)
        assert_baseline_expectations(res_g0_32, "i686")
        results["g0_baseline"]["i686"] = res_g0_32
        dcomp_status_32 = "S_OK (implemented)" if res_g0_32['probe2_dcomp']['dcomp_create_device_hr'] == "0x00000000" else "E_NOTIMPL (baseline)"
        print(f"  [i686 G0]   D3D11 Feature Level: {res_g0_32['probe1_d3d11']['feature_level']}, Adapter: {res_g0_32['probe1_d3d11']['adapter']}")
        print(f"  [i686 G0]   DComp Entry Points: {dcomp_status_32}, Composition SwapChain: {dcomp_status_32}")

        # Phase G1: 64-bit Non-DComp Animated Diagnostic Probe
        exe_g1_64 = tmp_path / "g1_probe64.exe"
        print("\n--- Phase G1: Non-DComp Animated Diagnostic Probes ---", flush=True)
        print("Compiling x86_64 G1 probe with x86_64-w64-mingw32-g++...", flush=True)
        compile_probe(args.source_g1, exe_g1_64, "x86_64-w64-mingw32-g++")
        print("Executing x86_64 G1 probe (60 frames animated + dynamic resize)...", flush=True)
        res_g1_64 = run_probe(exe_g1_64, args.wine)
        assert_g1_expectations(res_g1_64, "x86_64")
        results["g1_non_dcomp"]["x86_64"] = res_g1_64
        print(f"  [x86_64 G1] Presented {res_g1_64['frames_presented']}/60 frames (FPS: {res_g1_64['fps']:.1f}), Resize Succeeded: {res_g1_64['resize_succeeded']}")

        # Phase G1: 32-bit Non-DComp Animated Diagnostic Probe
        exe_g1_32 = tmp_path / "g1_probe32.exe"
        print("Compiling i686 G1 probe with i686-w64-mingw32-g++...", flush=True)
        compile_probe(args.source_g1, exe_g1_32, "i686-w64-mingw32-g++")
        print("Executing i686 G1 probe (60 frames animated + dynamic resize)...", flush=True)
        res_g1_32 = run_probe(exe_g1_32, args.wine)
        assert_g1_expectations(res_g1_32, "i686")
        results["g1_non_dcomp"]["i686"] = res_g1_32
        print(f"  [i686 G1]   Presented {res_g1_32['frames_presented']}/60 frames (FPS: {res_g1_32['fps']:.1f}), Resize Succeeded: {res_g1_32['resize_succeeded']}")

        # Phase G2: 64-bit DirectComposition Retained Visual Animated Diagnostic Probe
        exe_g2_64 = tmp_path / "g2_probe64.exe"
        print("\n--- Phase G2: DirectComposition Retained Visual Diagnostic Probes ---", flush=True)
        print("Compiling x86_64 G2 probe with x86_64-w64-mingw32-g++...", flush=True)
        compile_probe(args.source_g2, exe_g2_64, "x86_64-w64-mingw32-g++")
        print("Executing x86_64 G2 probe (60 frames 2-color retained animation + dynamic resize)...", flush=True)
        res_g2_64 = run_probe(exe_g2_64, args.wine)
        assert_g2_expectations(res_g2_64, "x86_64")
        results["g2_dcomp"]["x86_64"] = res_g2_64
        print(f"  [x86_64 G2] Presented {res_g2_64['frames_presented']}/60 frames (FPS: {res_g2_64['fps']:.1f}), Retained Verified: {res_g2_64['retained_tree_verified']}, Resize: {res_g2_64['resize_succeeded']}")

        # Phase G2: 32-bit DirectComposition Retained Visual Animated Diagnostic Probe
        exe_g2_32 = tmp_path / "g2_probe32.exe"
        print("Compiling i686 G2 probe with i686-w64-mingw32-g++...", flush=True)
        compile_probe(args.source_g2, exe_g2_32, "i686-w64-mingw32-g++")
        print("Executing i686 G2 probe (60 frames 2-color retained animation + dynamic resize)...", flush=True)
        res_g2_32 = run_probe(exe_g2_32, args.wine)
        assert_g2_expectations(res_g2_32, "i686")
        results["g2_dcomp"]["i686"] = res_g2_32
        print(f"  [i686 G2]   Presented {res_g2_32['frames_presented']}/60 frames (FPS: {res_g2_32['fps']:.1f}), Retained Verified: {res_g2_32['retained_tree_verified']}, Resize: {res_g2_32['resize_succeeded']}")

    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(results, indent=2) + "\n")
        print(f"\nAggregated results saved to {args.output_json}")

    print("\nALL ARCHITECTURES VERIFIED: Phase G0 baseline, Phase G1 non-DComp animated, and Phase G2 DirectComposition diagnostic probes passed with 0 errors.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
