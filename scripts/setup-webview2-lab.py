#!/usr/bin/env python3
"""Fetch Microsoft's SDK/runtime and build the native C probe on Linux.

Requires Python 3, cabextract, and x86_64-w64-mingw32-gcc. No installer,
updater, .NET, prefix-wide DLL overrides, or registry changes are needed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import urllib.request
from urllib.parse import urlparse
import zipfile


def fetch(url, destination):
    metadata = destination.with_suffix(destination.suffix + ".json")
    cached = json.loads(metadata.read_text()) if metadata.exists() else None
    if not destination.exists():
        temporary = destination.with_suffix(destination.suffix + ".partial")
        print("Downloading", url, flush=True)
        with urllib.request.urlopen(url, timeout=60) as source, temporary.open("wb") as target:
            shutil.copyfileobj(source, target)
        temporary.replace(destination)
    with destination.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    if cached and (cached["url"] != url or cached["sha256"] != digest):
        raise RuntimeError(f"Cached download does not match its provenance: {destination}")
    metadata.write_text(json.dumps({"url": url, "sha256": digest}, indent=2) + "\n")
    return digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.home() / "webview2-lab")
    parser.add_argument("--sdk-version", default="1.0.4258.31")
    parser.add_argument("--runtime-version", default="155.0.4283.45")
    args = parser.parse_args()
    for tool in ("cabextract", "x86_64-w64-mingw32-gcc"):
        if not shutil.which(tool):
            parser.error(f"Missing {tool}; install the distribution's package first")
    for version in (args.sdk_version, args.runtime_version):
        if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){3}", version):
            parser.error("Versions must have four numeric components")
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    sdk = root / ("sdk-" + args.sdk_version)
    package = root / ("sdk-" + args.sdk_version + ".nupkg")
    sdk_url = ("https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/"
               f"{args.sdk_version}/microsoft.web.webview2.{args.sdk_version}.nupkg")
    sdk_hash = fetch(sdk_url, package)
    if not sdk.exists():
        with zipfile.ZipFile(package) as archive:
            for entry in archive.namelist():
                if not (sdk / entry).resolve().is_relative_to(sdk.resolve()):
                    raise ValueError("SDK archive path escapes its directory")
            archive.extractall(sdk)
    page_url = "https://developer.microsoft.com/en-us/microsoft-edge/webview2"
    filename = f"Microsoft.WebView2.FixedVersionRuntime.{args.runtime_version}.x64.cab"
    cabinet = root / filename
    provenance = cabinet.with_suffix(cabinet.suffix + ".json")
    if cabinet.exists() and provenance.exists():
        runtime_url = json.loads(provenance.read_text())["url"]
    else:
        with urllib.request.urlopen(page_url, timeout=60) as response:
            page = response.read().decode("utf-8").replace(r"\u002F", "/")
        urls = re.findall(r'https://[^"\s<>]+\.x64\.cab', page)
        runtime_url = next((url for url in urls if url.endswith("/" + filename)), None)
    if not runtime_url or urlparse(runtime_url).hostname != "msedge.sf.dl.delivery.mp.microsoft.com":
        raise RuntimeError("Requested runtime is not listed on Microsoft's download page")
    runtime_hash = fetch(runtime_url, cabinet)
    runtime_root = root / ("runtime-" + args.runtime_version)
    runtime_root.mkdir(exist_ok=True)
    executables = list(runtime_root.rglob("msedgewebview2.exe"))
    if not executables:
        subprocess.run(["cabextract", "-q", "-d", str(runtime_root), str(cabinet)], check=True)
        executables = list(runtime_root.rglob("msedgewebview2.exe"))
    if len(executables) != 1:
        raise RuntimeError("Expected one extracted WebView2 runtime executable")
    runtime = executables[0].parent
    source = Path(__file__).with_name("webview2-probe.c")
    subprocess.run(["x86_64-w64-mingw32-gcc", "-std=c11", "-Wall", "-Wextra", "-Wno-unknown-pragmas", "-DUNICODE",
                    "-D_UNICODE", "-DCOBJMACROS", "-isystem", str(sdk / "build/native/include"),
                    "-I" + str(source.parent / "webview2-compat"),
                    str(source), "-o", str(root / "webview2-probe.exe"), "-mwindows", "-municode",
                    "-lole32", "-luuid", "-lshell32"], check=True)
    shutil.copy2(sdk / "build/native/x64/WebView2Loader.dll", root / "WebView2Loader.dll")
    (root / "runtime-path.txt").write_text(str(runtime) + "\n")
    (root / "tooling.json").write_text(json.dumps({
        "sdk": args.sdk_version, "sdk_url": sdk_url, "sdk_sha256": sdk_hash,
        "runtime": args.runtime_version, "runtime_url": runtime_url, "runtime_sha256": runtime_hash,
    }, indent=2) + "\n")
    print("Built", root / "webview2-probe.exe", "\nRuntime:", runtime)


if __name__ == "__main__":
    main()
