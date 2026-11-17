#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Common helpers for WineExplorer boot and recovery scripts.

Provides shared configuration reading/writing for boot.conf, recovery settings,
and atomic file replacement.
"""

import os
from pathlib import Path
import tempfile

BOOT_CONF = os.environ.get("WEX_BOOT_CONF", "/etc/wineexplorer/boot.conf")
BOOT_CONF_HEADER = """\
# WineExplorer's startup settings (System Properties > Advanced > Startup and Recovery). Written by wex-boot1.
# recovery_menu: 1 shows the "did not start successfully" screen after a failed start, 0 starts normally instead.
# recovery_timeout: seconds that screen counts down before it starts normally.
"""

BOOT_CONF_DEFAULTS = {
    "recovery_menu": "1",
    "recovery_timeout": "30",
}

COUNTDOWN_DEFAULT = 30
COUNTDOWN_MAX = 999


def _is_symlink_dir(directory):
    norm = directory.rstrip("/\\") if directory not in ("/", "\\") else directory
    if os.path.islink(norm) or os.path.islink(directory):
        return True
    try:
        if Path(norm).is_symlink():
            return True
    except OSError:
        pass
    if hasattr(os.path, "isjunction") and os.path.isjunction(norm):
        return True
    return False


def atomic_write(path, data, mode=0o644):
    """Replace path atomically: a temporary file in its directory, write, fsync, os.replace, fsync the directory.
    Refuses a symlinked target directory.
    """
    path = os.fspath(path)
    directory = os.path.dirname(path) or "."
    if _is_symlink_dir(directory):
        raise ValueError(f"refusing symlinked target directory: {directory}")

    os.makedirs(directory, mode=0o755, exist_ok=True)
    if _is_symlink_dir(directory):
        raise ValueError(f"refusing symlinked target directory: {directory}")

    prefix = f".{os.path.basename(path)}.tmp-"
    fd, temp = tempfile.mkstemp(prefix=prefix, dir=directory)
    try:
        if isinstance(data, str):
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
        else:
            with os.fdopen(fd, "wb") as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
        try:
            os.chmod(temp, mode)
        except OSError:
            pass
        os.replace(temp, path)
    except BaseException:
        try:
            os.unlink(temp)
        except OSError:
            pass
        raise

    try:
        flags = os.O_RDONLY
        if hasattr(os, "O_DIRECTORY"):
            flags |= os.O_DIRECTORY
        dir_fd = os.open(directory, flags)
        try:
            os.fsync(dir_fd)
        finally:
            os.close(dir_fd)
    except OSError:
        pass


def read_boot_conf(path=None):
    """Read boot.conf (key=value lines); blank lines and # comments are ignored.
    A key that appears more than once is malformed -> use the documented default for that key.
    """
    if path is None:
        path = BOOT_CONF
    values = {}
    counts = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                k = k.strip()
                v = v.strip()
                counts[k] = counts.get(k, 0) + 1
                values[k] = v
    except OSError:
        pass

    for k, count in counts.items():
        if count > 1:
            if k in BOOT_CONF_DEFAULTS:
                values[k] = BOOT_CONF_DEFAULTS[k]
            else:
                values.pop(k, None)

    return values


def boot_conf_value(key, path=None, default=None):
    """A value from boot.conf, or default if missing.
    If the key appears more than once, returns the documented default for that key.
    """
    conf = read_boot_conf(path)
    if key in conf:
        return conf[key]
    if default is not None:
        return default
    return BOOT_CONF_DEFAULTS.get(key)


def set_boot_conf_value(path, key, value=None, header=""):
    """Set key=value in path, keeping other lines and comments.
    Rewrites the single occurrence of key, or appends if missing.
    If key occurs multiple times, rewrites the first and removes subsequent duplicates.
    A new file starts with header.
    """
    if value is None:
        path, key, value = BOOT_CONF, path, key
    if path is None:
        path = BOOT_CONF
    path = os.fspath(path)
    lines = []
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except OSError:
        lines = header.splitlines() if header else []

    new_line = f"{key}={value}"
    found = False
    new_lines = []
    for line in lines:
        stripped = line.strip()
        if not stripped.startswith("#") and "=" in stripped and stripped.split("=", 1)[0].strip() == key:
            if not found:
                new_lines.append(new_line)
                found = True
            # duplicate lines for the same key are removed
        else:
            new_lines.append(line)

    if not found:
        new_lines.append(new_line)

    atomic_write(path, "\n".join(new_lines) + "\n", mode=0o644)


def recovery_enabled(path=None):
    """Whether a failed start shows the failure screen: recovery_menu= in boot.conf, on unless it says 0."""
    return boot_conf_value("recovery_menu", path) != "0"
