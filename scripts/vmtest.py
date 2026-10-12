#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Drive a QEMU/KVM test VM for installer and session tests (runs in WSL2 or on Linux).

Each VM lives in a state directory (default /tmp/vmtest/<name>) holding its disk, QMP socket, serial socket and
screenshots, so every command is a separate invocation:

  vmtest.py start NAME [--iso PATH] [--disk-gb 20] [--mem 4096] [--efi]   boot (CD first when --iso is given)
  vmtest.py stop NAME                                                     power off
  vmtest.py eject NAME                                                    remove the CD
  vmtest.py shot NAME [FILE.png]                                          screenshot (default STATE/last.png)
  vmtest.py key NAME KEY...                                               QEMU key names: ret f8 alt-n ctrl-alt-f2
  vmtest.py click NAME X Y [right]                                        click at screen pixel X,Y
  vmtest.py clicktext NAME TEXT [--timeout 30]                            click the words TEXT (found by OCR)
  vmtest.py type NAME TEXT                                                type text (US layout)
  vmtest.py wait NAME TEXT [--timeout 120]                                wait until OCR finds TEXT on screen
  vmtest.py text NAME                                                     print the OCR text of the screen
  vmtest.py sh NAME USER PASSWORD_FILE COMMAND [--timeout 60]             run COMMAND on the serial console

The serial console needs a getty on ttyS0 in the guest (systemctl enable serial-getty@ttyS0).
"""
import argparse, json, os, re, secrets, socket, subprocess, sys, time

ROOT = os.environ.get("VMTEST_DIR", "/tmp/vmtest")

def state(name):
    d = os.path.join(ROOT, name)
    os.makedirs(d, exist_ok=True)
    return d

def qmp(name, cmd, **args):
    s = socket.socket(socket.AF_UNIX)
    s.connect(os.path.join(state(name), "qmp.sock"))
    f = s.makefile("rw")
    f.readline()
    for msg in ({"execute": "qmp_capabilities"}, {"execute": cmd, "arguments": args}):
        f.write(json.dumps(msg) + "\n")
        f.flush()
        while True:   # skip asynchronous events
            reply = json.loads(f.readline())
            if "return" in reply or "error" in reply:
                break
    s.close()
    if "error" in reply:
        sys.exit(f"{cmd}: {reply['error'].get('desc')}")
    return reply["return"]

def start(a):
    d = state(a.name)
    disk = os.path.join(d, "disk.qcow2")
    if not os.path.exists(disk):
        subprocess.run(["qemu-img", "create", "-q", "-f", "qcow2", disk, f"{a.disk_gb}G"], check=True)
    for sock in ("qmp.sock", "serial.sock"):
        if os.path.exists(os.path.join(d, sock)):
            os.remove(os.path.join(d, sock))
    cmd = ["qemu-system-x86_64", "-enable-kvm", "-cpu", "host", "-smp", "2", "-m", str(a.mem), "-machine", "q35",
           "-drive", f"file={disk},if=virtio", "-vga", "std", "-display", "none",
           "-nic", "user,model=virtio-net-pci", "-device", "qemu-xhci", "-device", "usb-tablet",
           "-qmp", f"unix:{d}/qmp.sock,server,nowait",
           "-serial", f"unix:{d}/serial.sock,server,nowait",
           "-drive", "if=none,id=cd,media=cdrom" + (f",file={a.iso}" if a.iso else ""),
           "-device", "ide-cd,drive=cd,bus=ide.1,id=cdrom", "-boot", "order=dc" if a.iso else "order=c"]
    if a.efi:
        cmd += ["-bios", "/usr/share/ovmf/OVMF.fd"]
    log = open(os.path.join(d, "qemu.log"), "w")
    proc = subprocess.Popen(cmd, stdout=log, stderr=log, start_new_session=True)
    for _ in range(50):
        time.sleep(0.2)
        if proc.poll() is not None:
            break
        if os.path.exists(os.path.join(d, "qmp.sock")):
            time.sleep(1)
            if proc.poll() is None:
                print(f"started {a.name}")
                return
            break
    sys.exit("qemu did not start: " + open(os.path.join(d, "qemu.log")).read())

def shot(name, path=None):
    d = state(name)
    ppm = os.path.join(d, "screen.ppm")
    qmp(name, "screendump", filename=ppm)
    path = path or os.path.join(d, "last.png")
    subprocess.run(["convert", ppm, path], check=True)
    return path

SCALE = 3   # tesseract misses 8 pt GUI text and the 8x16 console font at 1x

def ocr(name, fmt=None):
    png = shot(name)
    big = png[:-4] + "-ocr.png"
    subprocess.run(["convert", png, "-resize", f"{SCALE * 100}%", "-colorspace", "Gray", big], check=True)
    return subprocess.run(["tesseract", big, "-", "--psm", "3"] + ([fmt] if fmt else []),
                          capture_output=True, text=True).stdout

def find_text(name, text):
    """Screen position (centre) of TEXT, matched case-insensitively against consecutive OCR words on one line."""
    rows = [r.split("\t") for r in ocr(name, "tsv").splitlines()[1:]]
    words = [r for r in rows if len(r) == 12 and r[11].strip()]
    want = text.lower().split()
    for i in range(len(words) - len(want) + 1):
        run = words[i:i + len(want)]
        if [w[11].lower() for w in run] == want and len({tuple(w[2:5]) for w in run}) == 1:
            left = int(run[0][6]); top = int(run[0][7])
            right = int(run[-1][6]) + int(run[-1][8]); bottom = top + int(run[0][9])
            return (left + right) // 2 // SCALE, (top + bottom) // 2 // SCALE
    return None

US = {**{c: c for c in "abcdefghijklmnopqrstuvwxyz0123456789"},
      " ": "spc", "-": "minus", "=": "equal", "[": "bracket_left", "]": "bracket_right", ";": "semicolon",
      "'": "apostrophe", "`": "grave_accent", "\\": "backslash", ",": "comma", ".": "dot", "/": "slash",
      "\n": "ret", "\t": "tab"}
SHIFTED = dict(zip('~!@#$%^&*()_+{}|:"<>?', "`1234567890-=[]\\;',./"))

def keys(name, combos, hold=80):
    for combo in combos:
        qmp(name, "send-key", keys=[{"type": "qcode", "data": k} for k in combo.split("-")], **{"hold-time": hold})
        time.sleep(0.05)

def click(name, x, y, button="left"):
    w, h = screen_size(name)
    pos = [{"type": "abs", "data": {"axis": "x", "value": x * 32767 // (w - 1)}},
           {"type": "abs", "data": {"axis": "y", "value": y * 32767 // (h - 1)}}]
    qmp(name, "input-send-event", events=pos)
    for down in (True, False):
        qmp(name, "input-send-event", events=[{"type": "btn", "data": {"down": down, "button": button}}])
        time.sleep(0.08)

def screen_size(name):
    ppm = os.path.join(state(name), "size.ppm")
    qmp(name, "screendump", filename=ppm)
    with open(ppm, "rb") as f:
        f.readline()
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
    w, h = map(int, line.split())
    return w, h

def type_text(name, text):
    seq = []
    for ch in text:
        if ch.isupper():
            seq.append("shift-" + ch.lower())
        elif ch in SHIFTED:
            seq.append("shift-" + US[SHIFTED[ch]])
        else:
            seq.append(US[ch])
    keys(name, seq, hold=30)

def serial(name, user, pwfile, command, timeout):
    password = open(pwfile).read().strip()
    s = socket.socket(socket.AF_UNIX)
    s.connect(os.path.join(state(name), "serial.sock"))
    s.settimeout(0.3)
    buf = b""

    def until(pattern, limit):
        nonlocal buf
        end = time.time() + limit
        while time.time() < end:
            try:
                buf += s.recv(65536)
            except socket.timeout:
                pass
            m = re.search(pattern, buf)
            if m:
                return m
        return None

    def send(text):
        s.sendall(text.encode() + b"\r")

    send("")
    m = until(rb"login: $|[#$] $", 15)
    if m and m.group(0).startswith(b"login"):
        send(user)
        until(rb"Password: $", 10)
        send(password)
        if not until(rb"[#$] $", 20):
            sys.exit("login failed")
    buf = b""
    send("stty -echo cols 250; export PAGER=cat SYSTEMD_PAGER= TERM=dumb")
    until(rb"[#$] $", 5)
    buf = b""
    tag = secrets.token_hex(4)
    send(f"echo B{tag}; {command}; echo E{tag} $?")
    m = until(rb"E" + tag.encode() + rb" (\d+)", timeout)
    out = buf.decode(errors="replace").replace("\r", "")
    out = out.split(f"B{tag}\n", 1)[-1].split(f"E{tag}")[0]
    print(out, end="")
    send("stty echo")
    return int(m.group(1)) if m else 124

def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="op", required=True)
    s = sub.add_parser("start"); s.add_argument("name"); s.add_argument("--iso"); s.add_argument("--efi", action="store_true")
    s.add_argument("--disk-gb", type=int, default=20); s.add_argument("--mem", type=int, default=4096)
    for op in ("stop", "eject", "text"):
        sub.add_parser(op).add_argument("name")
    s = sub.add_parser("shot"); s.add_argument("name"); s.add_argument("file", nargs="?")
    s = sub.add_parser("key"); s.add_argument("name"); s.add_argument("keys", nargs="+")
    s = sub.add_parser("type"); s.add_argument("name"); s.add_argument("text")
    s = sub.add_parser("click"); s.add_argument("name"); s.add_argument("x", type=int); s.add_argument("y", type=int)
    s.add_argument("button", nargs="?", default="left")
    s = sub.add_parser("clicktext"); s.add_argument("name"); s.add_argument("text")
    s.add_argument("--timeout", type=float, default=30)
    s = sub.add_parser("wait"); s.add_argument("name"); s.add_argument("text"); s.add_argument("--timeout", type=float, default=120)
    s = sub.add_parser("sh"); s.add_argument("name"); s.add_argument("user"); s.add_argument("pwfile"); s.add_argument("command")
    s.add_argument("--timeout", type=float, default=60)
    a = p.parse_args()

    if a.op == "start":
        start(a)
    elif a.op == "stop":
        qmp(a.name, "quit")
    elif a.op == "eject":
        qmp(a.name, "eject", id="cdrom", force=True)
    elif a.op == "shot":
        print(shot(a.name, a.file))
    elif a.op == "key":
        keys(a.name, a.keys)
    elif a.op == "type":
        type_text(a.name, a.text.rstrip("\r\n"))
    elif a.op == "click":
        click(a.name, a.x, a.y, a.button)
    elif a.op == "clicktext":
        end = time.time() + a.timeout
        while time.time() < end:
            pos = find_text(a.name, a.text)
            if pos:
                click(a.name, *pos)
                print(f"clicked {pos[0]},{pos[1]}")
                return
            time.sleep(2)
        sys.exit(f"text not found: {a.text!r}")
    elif a.op == "text":
        print(ocr(a.name))
    elif a.op == "wait":
        end = time.time() + a.timeout
        want = a.text.lower()
        while time.time() < end:
            if want in ocr(a.name).lower():
                print("found")
                return
            time.sleep(2)
        sys.exit(f"timeout waiting for {a.text!r}")
    elif a.op == "sh":
        sys.exit(serial(a.name, a.user, a.pwfile, a.command, a.timeout))

if __name__ == "__main__":
    main()
