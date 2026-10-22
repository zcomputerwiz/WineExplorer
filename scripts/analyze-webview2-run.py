#!/usr/bin/env python3
"""Classify a lab run; navigation alone and a few startup frames are not passes."""
import argparse
import json
from pathlib import Path
import re


def analyze(root):
    host = (root / "host.log").read_text(errors="replace")
    samples = []
    for match in re.finditer(r"^(\d+) ms sample hr=0x00000000 (.+)$", host, re.MULTILINE):
        try:
            data = json.loads(match[2])
            if isinstance(data, str):
                data = json.loads(data)
            if not isinstance(data, dict) or not isinstance(data.get("frames"), int):
                continue
            samples.append({"ms": int(match[1]), **data})
        except (ValueError, TypeError):
            continue
    tail = samples[-6:]
    changes = [b["frames"] - a["frames"] for a, b in zip(tail, tail[1:])]
    if len(tail) < 6 or tail[-1]["ms"] - tail[0]["ms"] < 4000:
        state = "insufficient_samples"
    elif all(sample["frames"] == 0 for sample in samples):
        state = "no_animation_frames"
    elif not any(change > 0 for change in changes):
        state = "animation_stalled"
    elif sum(change > 0 for change in changes) >= 3:
        state = "animation_progressing"
    else:
        state = "animation_intermittent"
    chromium_path = root / "chromium.log"
    chromium = chromium_path.read_text(errors="replace") if chromium_path.exists() else ""
    signatures = {}
    for name, pattern in {
        "gl_allowlist": r"Requested GL implementation.*not found",
        "warp_initialization": r"eglInitialize D3D11Warp failed",
        "d3d11_output": r"D3D11CreateDevice failed",
        "directcomposition_output": r"DCompositionCreateDevice failed",
        "gpu_process_crash": r"GPU process exited unexpectedly",
        "fatal_log": r":FATAL:",
        "skia_surface": r"Unable to initialize SkSurface",
        "shared_context": r"Failed to create shared context for virtualization",
    }.items():
        signatures[name] = len(re.findall(pattern, chromium))
    optional_capability_warnings = {
        "p010_video_processor_format_support": len(re.findall(r"CheckFormatSupport failed", chromium)),
    }
    navigation = "success=1 web_error=0" in host
    return {
        "state": state, "navigation_succeeded": "success=1 web_error=0" in host,
        "diagnostic_passed": state == "animation_progressing" and navigation
                             and chromium_path.exists() and not any(signatures.values()),
        "samples": len(samples), "first": samples[0] if samples else None,
        "last": samples[-1] if samples else None, "late_frame_deltas": changes,
        "chromium_log_present": chromium_path.exists(), "errors": signatures,
        "optional_capability_warnings": optional_capability_warnings,
        "note": "Animation progression is not proof of correct visual presentation; inspect the VM screenshot.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    result = analyze(args.run)
    output = json.dumps(result, indent=2) + "\n"
    (args.run / "summary.json").write_text(output)
    print(output, end="")
    return 0 if result["diagnostic_passed"] else 4


if __name__ == "__main__":
    raise SystemExit(main())
