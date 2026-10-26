#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Controlled single execution of reviewed Chromium R6 startup diagnostic."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

RUN_NAME = 'chromium-registration-r6-20261026'
LAB_WIN = Path(r'D:\VirtualBox_VMs\WEX-G4-VirGL-20261022')
RUN_DIR_WIN = LAB_WIN / RUN_NAME
PREP_DIR_WIN = LAB_WIN / (RUN_NAME + '.preflight')
PREFLIGHT_FILE_WIN = PREP_DIR_WIN / 'preflight.json'

LAUNCHER_SCRIPT_WIN = r'D:\DeepThought\Worktrees\chromium-r5-preparation\WineExplorer\scripts\g4-virgl-launcher-session.ps1'
TRIAL_SCRIPT_WSL = '/mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/build/g4-virgl-20261022/chromium-registration-r6-trial.py'
STOPPER_SCRIPT_WSL = '/mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/scripts/stop_g4_virgl_launcher.py'
IDENTITY_FILE_WSL = f'/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/{RUN_NAME}/launcher-identity.json'
IDENTITY_FILE_WIN = RUN_DIR_WIN / 'launcher-identity.json'


def main():
    print(f"=== Starting Controlled R6 Diagnostic Run: {RUN_NAME} ===")
    start_time_utc = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    start_mono = time.monotonic()
    summary = {
        'run_name': RUN_NAME,
        'started_utc': start_time_utc,
        'preflight_exists': PREFLIGHT_FILE_WIN.is_file(),
        'output_exists_before': RUN_DIR_WIN.exists(),
    }

    # 1. Preflight validation
    if not PREFLIGHT_FILE_WIN.is_file():
        raise RuntimeError(f"Preflight file missing: {PREFLIGHT_FILE_WIN}")
    if RUN_DIR_WIN.exists():
        raise RuntimeError(f"Output directory already exists: {RUN_DIR_WIN}")

    # Check free disk space
    free_bytes = shutil.disk_usage(str(LAB_WIN)).free
    summary['free_bytes_before'] = free_bytes
    if free_bytes < 1024**3:
        raise RuntimeError(f"Insufficient free space: {free_bytes} bytes")
    print(f"Free space on D: {free_bytes / (1024**3):.2f} GiB")

    # 2. Launch controller
    launch_cmd = [
        'powershell.exe', '-ExecutionPolicy', 'Bypass', '-NoProfile', '-File', LAUNCHER_SCRIPT_WIN,
        '-RunName', RUN_NAME
    ]
    print(f"Starting launch controller: {' '.join(launch_cmd)}")
    launcher_proc = subprocess.Popen(
        launch_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    summary['launcher_pid'] = launcher_proc.pid

    run_exit = None
    stop_exit = None
    stop_stdout = None
    stop_stderr = None
    launcher_exit = None

    try:
        # 3. Wait for identity file
        print("Waiting for launcher identity file to be created...")
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            if launcher_proc.poll() is not None:
                out, err = launcher_proc.communicate()
                raise RuntimeError(
                    f"Launcher process exited prematurely ({launcher_proc.returncode}):\nSTDOUT: {out}\nSTDERR: {err}"
                )
            if IDENTITY_FILE_WIN.is_file():
                try:
                    data = json.loads(IDENTITY_FILE_WIN.read_text(encoding='utf-8'))
                    if data.get('qemu_pid') and data.get('xvfb_pid'):
                        print(f"Launcher identity established: QEMU PID {data.get('qemu_pid')}, Xvfb PID {data.get('xvfb_pid')}")
                        summary['initial_identity'] = data
                        break
                except Exception:
                    pass
            time.sleep(1)
        else:
            raise TimeoutError("Timed out waiting for launcher identity after 180s")

        # 4. Execute trial run in WSL
        run_cmd = [
            'wsl', '-d', 'Debian', '--exec',
            'python3', '-B', TRIAL_SCRIPT_WSL, 'run'
        ]
        print(f"Executing WSL trial run: {' '.join(run_cmd)}")
        stdout_log = RUN_DIR_WIN / 'trial-run.stdout.log'
        stderr_log = RUN_DIR_WIN / 'trial-run.stderr.log'

        with stdout_log.open('wb') as out_f, stderr_log.open('wb') as err_f:
            trial_proc = subprocess.run(
                run_cmd,
                stdout=out_f,
                stderr=err_f,
                timeout=400
            )
        run_exit = trial_proc.returncode
        summary['run_exit_code'] = run_exit
        print(f"WSL trial run completed with return code: {run_exit}")

    finally:
        # 5. Guaranteed Stop Command
        print("Executing stop command...")
        stop_cmd = [
            'wsl', '-d', 'Debian', '--exec',
            'python3', '-B', STOPPER_SCRIPT_WSL,
            '--identity', IDENTITY_FILE_WSL,
            '--timeout', '80'
        ]
        try:
            stop_res = subprocess.run(
                stop_cmd,
                capture_output=True,
                text=True,
                timeout=95
            )
            stop_exit = stop_res.returncode
            stop_stdout = stop_res.stdout
            stop_stderr = stop_res.stderr
            print(f"Stop command exit: {stop_exit}")
        except Exception as e:
            stop_stderr = repr(e)
            print(f"Stop command failed: {e}")

        summary['stop_exit_code'] = stop_exit
        summary['stop_stdout'] = stop_stdout
        summary['stop_stderr'] = stop_stderr

        # 6. Await Windows launcher process termination
        print("Awaiting Windows launcher process exit...")
        try:
            l_out, l_err = launcher_proc.communicate(timeout=90)
            launcher_exit = launcher_proc.returncode
            summary['launcher_exit_code'] = launcher_exit
            summary['launcher_stdout'] = l_out
            summary['launcher_stderr'] = l_err
            print(f"Windows launcher process exited with code: {launcher_exit}")
        except subprocess.TimeoutExpired:
            print("Windows launcher process timed out waiting for exit, terminating...")
            launcher_proc.kill()
            launcher_exit = -9
            summary['launcher_exit_code'] = launcher_exit

    summary['elapsed_seconds'] = time.monotonic() - start_mono
    summary['finished_utc'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())

    summary_file = RUN_DIR_WIN / 'wrapper-execution-summary.json'
    summary_file.write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(f"Summary written to {summary_file}")
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
