#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Drive a QEMU/KVM test VM for installer and session tests (runs in WSL2 or on Linux).

Each VM lives in a state directory (default /tmp/vmtest/<name>) holding its disk, QMP socket, serial socket and
screenshots, so every command is a separate invocation:
Set VMTEST_DIR to a persistent directory if /tmp is a RAM filesystem.

  vmtest.py start NAME [--iso PATH] [--disk-gb 20] [--mem 4096] [--efi]   boot (CD first when --iso is given)
  vmtest.py stop NAME                                                     power off
  vmtest.py reset NAME                                                    press the Reset button (QMP system_reset)
  vmtest.py shutdown NAME [--timeout 120]                                 ACPI shutdown and wait for exit
  vmtest.py eject NAME                                                    remove the CD
  vmtest.py shot NAME [FILE.png]                                          screenshot (default STATE/last.png)
  vmtest.py key NAME KEY...                                               QEMU key names: ret f8 alt-n ctrl-alt-f2
  vmtest.py click NAME X Y [right]                                        click at screen pixel X,Y
  vmtest.py drag NAME X1 Y1 X2 Y2                                        left-button drag between screen positions
  vmtest.py clicktext NAME TEXT [--timeout 30]                            click the words TEXT (found by OCR)
  vmtest.py type NAME TEXT                                                type text (US layout)
  vmtest.py wait NAME TEXT [--timeout 120]                                wait until OCR finds TEXT on screen
  vmtest.py text NAME                                                     print the OCR text of the screen
  vmtest.py sh NAME USER PASSWORD_FILE COMMAND [--timeout 60]             run COMMAND on the serial console
  vmtest.py put NAME USER PASSWORD_FILE LOCAL GUESTPATH [--timeout 300]  copy a file into the guest: the guest downloads
                                                                          it from a one-shot server on this host (seconds
                                                                          for megabytes, against base64 over the console)
  vmtest.py loader-timeout NAME SECONDS                                   (UEFI, VM off) the boot menu shows once,
                                                                          with this timeout (LoaderConfigTimeoutOneShot)

The serial console needs a getty on ttyS0 in the guest (systemctl enable serial-getty@ttyS0).  Machines started here
carry the fw_cfg entry opt/org.wineexplorer/serial-login, on which textsetup enables that getty when installing.
"""
import argparse, json, os, re, secrets, shlex, socket, subprocess, sys, time

ROOT = os.environ.get("VMTEST_DIR", "/tmp/vmtest")

def state(name):
    d = os.path.join(ROOT, name)
    os.makedirs(d, exist_ok=True)
    return d

class QMPError(Exception):
    pass

class QMP:
    """One QMP session. QEMU serves one client at a time: a session left open blocks every other vmtest call."""
    def __init__(self, name):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(os.path.join(state(name), "qmp.sock"))
        self.f = self.s.makefile("rw")
        self.f.readline()
        self.call("qmp_capabilities")

    def call(self, cmd, **args):
        self.f.write(json.dumps({"execute": cmd, "arguments": args}) + "\n")
        self.f.flush()
        while True:   # skip asynchronous events
            reply = json.loads(self.f.readline())
            if "return" in reply or "error" in reply:
                break
        if "error" in reply:
            raise QMPError(f"{cmd}: {reply['error'].get('desc')}")
        return reply["return"]

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        # The file object keeps the socket open until it is closed too.
        self.f.close()
        self.s.close()

def qmp(name, cmd, **args):
    try:
        with QMP(name) as q:
            return q.call(cmd, **args)
    except QMPError as e:
        sys.exit(str(e))

# UEFI: OVMF's code read-only and a variable store of the VM's own (STATE/efivars.fd, made from the template at the
# first start and kept), so that a variable written while the VM is off (loader_timeout) is there at the next start.
# Without the split firmware files, the combined image as before (no variables kept).
OVMF_CODE = "/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS = "/usr/share/OVMF/OVMF_VARS_4M.fd"

def efi_firmware(d):
    if not (os.path.exists(OVMF_CODE) and os.path.exists(OVMF_VARS)):
        return ["-bios", "/usr/share/ovmf/OVMF.fd"]
    store = os.path.join(d, "efivars.fd")
    if not os.path.exists(store):
        with open(OVMF_VARS, "rb") as src, open(store, "wb") as dst:
            dst.write(src.read())
    return ["-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={OVMF_CODE}",
            "-drive", f"if=pflash,format=raw,unit=1,file={store}"]

# systemd-boot's LoaderConfigTimeoutOneShot (vendor GUID of the Boot Loader Interface): the menu shows at the next start
# with this timeout, whatever the configured one, and the loader deletes the variable. Vintner Boot's menu otherwise
# shows only after a key press that OVMF has buffered at the one moment systemd-boot looks, which a busy host misses.
LOADER_GUID = "4a67b082-0a4c-41cf-b6c7-440b29bb8c4f"

def loader_timeout(name, seconds):
    d = state(name)
    store = os.path.join(d, "efivars.fd")
    if not os.path.exists(store):
        sys.exit(f"{name} has no variable store (a UEFI VM started with the split OVMF files makes one)")
    if os.path.exists(os.path.join(d, "qmp.sock")) and subprocess.run(
            ["pgrep", "-f", f"file={store}"], stdout=subprocess.DEVNULL).returncode == 0:
        sys.exit(f"{name} is running; the variable store is written while it is off")
    data = (str(int(seconds)) + "\0").encode("utf-16-le").hex()
    attr = 7  # non-volatile, boot service and runtime access, as bootctl sets it
    variables = {"version": 2, "variables": [{"name": "LoaderConfigTimeoutOneShot", "guid": LOADER_GUID,
                                              "attr": attr, "data": data}]}
    path = os.path.join(d, "loader-timeout.json")
    with open(path, "w") as f:
        json.dump(variables, f)
    result = subprocess.run(["virt-fw-vars", "-i", store, "-o", store, "--set-json", path],
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    os.remove(path)
    if result.returncode:
        sys.exit("virt-fw-vars failed: " + result.stderr.strip())

def start(a):
    d = state(a.name)
    disk = os.path.join(d, "disk.qcow2")
    if not os.path.exists(disk):
        subprocess.run(["qemu-img", "create", "-q", "-f", "qcow2", disk, f"{a.disk_gb}G"], check=True)
    for sock in ("qmp.sock", "serial.sock"):
        if os.path.exists(os.path.join(d, sock)):
            os.remove(os.path.join(d, sock))
    # Memory: the test machine's WSL VM (15 GB) runs several of these at once (installer/tests/run-chain.sh), and WSL
    # does not give its page cache back to Windows. cache=none keeps the disk image out of the host's page cache (the
    # guest caches it anyway); the balloon's free page reporting hands the pages a guest frees back to the host.
    # VMTEST_HOST_CACHE=1 restores QEMU's default cache mode, for a disk on a file system without O_DIRECT.
    drive_cache = "" if os.environ.get("VMTEST_HOST_CACHE") == "1" else ",cache=none"
    cmd = ["qemu-system-x86_64", "-enable-kvm", "-cpu", "host", "-smp", "2", "-m", str(a.mem), "-machine", "q35",
           "-drive", f"file={disk},if=virtio{drive_cache}", "-vga", "std", "-display", "none",
           "-nic", "user,model=virtio-net-pci", "-device", "qemu-xhci", "-device", "usb-tablet",
           "-qmp", f"unix:{d}/qmp.sock,server,nowait",
           "-serial", f"unix:{d}/serial.sock,server,nowait",
           # textsetup gives the installed system a serial login only on machines with this mark
           "-fw_cfg", "name=opt/org.wineexplorer/serial-login,string=1",
           "-drive", "if=none,id=cd,media=cdrom" + (f",file={a.iso}" if a.iso else ""),
           "-device", "ide-cd,drive=cd,bus=ide.1,id=cdrom", "-boot", "order=dc" if a.iso else "order=c",
           # last, so the devices before it keep the PCI addresses the installed disks were set up with
           "-device", "virtio-balloon-pci,free-page-reporting=on"]
    if a.efi:
        cmd += efi_firmware(d)
    # more QEMU arguments for one test, for example a second network card (installer/tests/ad/machine.sh)
    cmd += shlex.split(os.environ.get("VMTEST_QEMU_EXTRA", ""))
    if a.paused:   # the CPU waits for "cont": input that must already be there when the firmware or loader runs
        cmd += ["-S"]
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


def shutdown(name, timeout):
    """Flush the installed guest through normal shutdown before disk inspection."""
    qmp(name, "system_powerdown")
    end = time.time() + timeout
    while time.time() < end:
        if not os.path.exists(os.path.join(state(name), "qmp.sock")):
            print(f"shut down {name}")
            return
        time.sleep(0.5)
    sys.exit("guest did not shut down cleanly; disk inspection must not proceed")

SCALE = 3   # tesseract misses 8 pt GUI text and the 8x16 console font at 1x

def ocr(name, fmt=None, white_text=False):
    png = shot(name)
    big = png[:-4] + ("-white-ocr.png" if white_text else "-ocr.png")
    # Tesseract's automatic threshold loses white Welcome-screen text against
    # its blue background. Retry only missed matches with an explicit threshold.
    contrast = ["-threshold", "60%"] if white_text else []
    subprocess.run(["convert", png, "-resize", f"{SCALE * 100}%", "-colorspace", "Gray", *contrast, big], check=True)
    return subprocess.run(["tesseract", big, "-", "--psm", "3"] + ([fmt] if fmt else []),
                          capture_output=True, text=True).stdout

def find_text(name, text):
    """Screen position (centre) of TEXT, matched case-insensitively against consecutive OCR words on one line."""
    want = text.lower().split()
    for white_text in (False, True):
        rows = [r.split("\t") for r in ocr(name, "tsv", white_text).splitlines()[1:]]
        words = [r for r in rows if len(r) == 12 and r[11].strip()]
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
        # QMP releases these keys asynchronously. Do not start the next chord
        # or text while Ctrl/Alt/Shift from this one is still held.
        time.sleep(max(0.05, hold / 1000 + 0.01))

def hold(name, key, seconds, resume=False, tap=False):
    # A held key: repeated key-down events with no key-up, as a keyboard's typematic repeat sends, then the release.
    # Vintner Boot's BIOS GRUB shows its menu while Shift is down (keystatus --shift), which send-key cannot do.
    # QEMU takes no input while the VM is paused, and GRUB checks Shift within 2 seconds of power-on, too soon for
    # a separate call after "start": RESUME runs a VM started with --paused and holds the key in the same session.
    # Other vmtest calls wait until the key is released.
    # TAP: separate presses (down, then up) every 150 ms instead, for a loader that only looks for a key waiting in
    # the firmware's input buffer when it starts (systemd-boot on OVMF, whose drivers do not repeat a held key).
    event = lambda down: [{"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": key}}}]
    try:
        with QMP(name) as q:
            if resume:
                q.call("cont")
            end = time.time() + seconds
            try:
                while time.time() < end:
                    q.call("input-send-event", events=event(True))
                    if tap:
                        time.sleep(0.05)
                        q.call("input-send-event", events=event(False))
                        time.sleep(0.1)
                    else:
                        time.sleep(0.1)
            finally:
                q.call("input-send-event", events=event(False))
    except QMPError as e:
        sys.exit(str(e))

def move(name, x, y):
    # A compositor's headless second output is outside QEMU's primary scanout,
    # but the USB tablet spans the whole layout. Keep ordinary VM defaults.
    layout = os.environ.get("VMTEST_INPUT_SIZE")
    w, h = map(int, layout.split("x")) if layout else screen_size(name)
    if w < 2 or h < 2 or not (0 <= x < w and 0 <= y < h):
        raise ValueError(f"Pointer {x},{y} outside input layout {w}x{h}")
    pos = [{"type": "abs", "data": {"axis": "x", "value": x * 32767 // (w - 1)}},
           {"type": "abs", "data": {"axis": "y", "value": y * 32767 // (h - 1)}}]
    qmp(name, "input-send-event", events=pos)


def click(name, x, y, button="left", count=1):
    move(name, x, y)
    time.sleep(0.1)  # let the guest process surface enter/motion before pressing
    for down in (True, False) * count:
        qmp(name, "input-send-event", events=[{"type": "btn", "data": {"down": down, "button": button}}])
        time.sleep(0.08)


def drag(name, x1, y1, x2, y2):
    move(name, x1, y1)
    time.sleep(0.1)
    qmp(name, "input-send-event", events=[{"type": "btn", "data": {"down": True, "button": "left"}}])
    try:
        for step in range(1, 13):
            move(name, x1 + (x2 - x1) * step // 12, y1 + (y2 - y1) * step // 12)
            time.sleep(0.05)
    finally:
        qmp(name, "input-send-event", events=[{"type": "btn", "data": {"down": False, "button": "left"}}])

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

def put(name, user, pwfile, local, guest, timeout):
    """Copy LOCAL into the running guest as GUEST. QEMU's user networking shows this host to the guest as 10.0.2.2, so
    the guest downloads the file (python3 in the guest) from a one-shot web server here that listens on 127.0.0.1
    only, serves this one file under a random name, and stops afterwards; the guest's SHA-256 must match."""
    import contextlib, hashlib, http.server, io, threading

    path = os.path.abspath(local)
    if not os.path.isfile(path):
        print(f"put: {local} is not a file", file=sys.stderr)
        return 2
    size = os.path.getsize(path)
    sha = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            sha.update(block)
    token = secrets.token_hex(8)

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != "/" + token:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Length", str(size))
            self.end_headers()
            with open(path, "rb") as f:
                for block in iter(lambda: f.read(1 << 20), b""):
                    self.wfile.write(block)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    command = ("python3 -c 'import sys, urllib.request; urllib.request.urlretrieve(sys.argv[1], sys.argv[2])' "
               f"http://10.0.2.2:{server.server_address[1]}/{token} {shlex.quote(guest)} && sha256sum {shlex.quote(guest)}")
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            rc = serial(name, user, pwfile, command, timeout)
    finally:
        server.shutdown()
    if rc == 0 and out.getvalue().split()[:1] == [sha.hexdigest()]:
        print(f"{local} -> {guest}: {size} bytes, sha256 ok")
        return 0
    print(f"put failed (status {rc}): {out.getvalue().strip()[-300:]}", file=sys.stderr)
    return 1

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

    def note(what):
        # VMTEST_SERIAL_LOG=FILE: what the serial console showed when it did not answer (the bytes read so far)
        path = os.environ.get("VMTEST_SERIAL_LOG")
        if path:
            with open(path, "a") as f:
                f.write("%s %s (%s): %s\n    read so far: %r\n" % (time.strftime("%H:%M:%S"), name, command[:40], what, buf[-1500:]))

    send("")
    m = until(rb"login: $|[#$] $", 15)
    if not m:
        # Once more (a getty left at "Password:" by an earlier call answers an empty line with "Login incorrect" and a
        # new prompt); then give up without typing the command. Typed into a login prompt it became the user name,
        # left the getty at "Password:", and a long call then waited for nothing (2026-11-23, a desktop probe made
        # while the machine was starting).
        send("")
        m = until(rb"login: $|[#$] $", 15)
    if not m:
        note("no login or shell prompt within 30 s; the command was not sent")
        return 124
    if m and m.group(0).startswith(b"login"):
        send(user)
        until(rb"Password: $", 10)
        send(password)
        if not until(rb"[#$] $", 20):
            note("login failed")
            sys.exit("login failed")
    buf = b""
    send("stty -echo cols 250; export PAGER=cat SYSTEMD_PAGER= TERM=dumb")
    until(rb"[#$] $", 5)
    buf = b""
    tag = secrets.token_hex(4)
    send(f"echo B{tag}; {command}; echo E{tag} $?")
    m = until(rb"E" + tag.encode() + rb" (\d+)", timeout)
    if not m:
        note("the command did not finish within %s s" % timeout)
    out = buf.decode(errors="replace").replace("\r", "")
    out = out.split(f"B{tag}\n", 1)[-1].split(f"E{tag}")[0]
    print(out, end="")
    send("stty echo")
    return int(m.group(1)) if m else 124

def listen(name, seconds):
    """What the guest writes to its first serial port during SECONDS, sending nothing (Debugging Mode's output)."""
    s = socket.socket(socket.AF_UNIX)
    s.connect(os.path.join(state(name), "serial.sock"))
    s.settimeout(0.3)
    buf = b""
    end = time.time() + seconds
    while time.time() < end:
        try:
            data = s.recv(65536)
        except socket.timeout:
            continue
        if not data:
            break
        buf += data
    s.close()
    return buf.decode(errors="replace").replace("\r", "")

def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="op", required=True)
    s = sub.add_parser("start"); s.add_argument("name"); s.add_argument("--iso"); s.add_argument("--efi", action="store_true")
    s.add_argument("--disk-gb", type=int, default=20); s.add_argument("--mem", type=int, default=4096)
    s.add_argument("--paused", action="store_true")
    for op in ("stop", "reset", "eject", "cont"):
        sub.add_parser(op).add_argument("name")
    s = sub.add_parser("text"); s.add_argument("name")
    s.add_argument("--white", action="store_true", help="the explicit threshold pass (white or light-on-dark text)")
    s = sub.add_parser("shutdown"); s.add_argument("name"); s.add_argument("--timeout", type=float, default=120)
    s = sub.add_parser("shot"); s.add_argument("name"); s.add_argument("file", nargs="?")
    s = sub.add_parser("key"); s.add_argument("name"); s.add_argument("keys", nargs="+")
    s = sub.add_parser("hold"); s.add_argument("name"); s.add_argument("key"); s.add_argument("seconds", type=float)
    s.add_argument("--cont", action="store_true", help="run a VM started with --paused first")
    s.add_argument("--tap", action="store_true", help="separate presses every 150 ms instead of one held key")
    s = sub.add_parser("type"); s.add_argument("name"); s.add_argument("text")
    s = sub.add_parser("click"); s.add_argument("name"); s.add_argument("x", type=int); s.add_argument("y", type=int)
    s.add_argument("button", nargs="?", default="left")
    s = sub.add_parser("doubleclick"); s.add_argument("name")
    s.add_argument("x", type=int); s.add_argument("y", type=int)
    s = sub.add_parser("drag"); s.add_argument("name")
    for coordinate in ("x1", "y1", "x2", "y2"):
        s.add_argument(coordinate, type=int)
    s = sub.add_parser("clicktext"); s.add_argument("name"); s.add_argument("text")
    s.add_argument("--timeout", type=float, default=30)
    s = sub.add_parser("wait"); s.add_argument("name"); s.add_argument("text"); s.add_argument("--timeout", type=float, default=120)
    s = sub.add_parser("sh"); s.add_argument("name"); s.add_argument("user"); s.add_argument("pwfile"); s.add_argument("command")
    s.add_argument("--timeout", type=float, default=60)
    s = sub.add_parser("put"); s.add_argument("name"); s.add_argument("user"); s.add_argument("pwfile")
    s.add_argument("local"); s.add_argument("guest"); s.add_argument("--timeout", type=float, default=300)
    s = sub.add_parser("listen"); s.add_argument("name"); s.add_argument("seconds", type=float)
    s = sub.add_parser("loader-timeout"); s.add_argument("name"); s.add_argument("seconds", type=int)
    a = p.parse_args()

    if a.op == "start":
        start(a)
    elif a.op == "loader-timeout":
        loader_timeout(a.name, a.seconds)
    elif a.op == "stop":
        qmp(a.name, "quit")
    elif a.op == "reset":
        qmp(a.name, "system_reset")
    elif a.op == "cont":
        qmp(a.name, "cont")
    elif a.op == "shutdown":
        shutdown(a.name, a.timeout)
    elif a.op == "eject":
        qmp(a.name, "eject", id="cdrom", force=True)
    elif a.op == "shot":
        print(shot(a.name, a.file))
    elif a.op == "key":
        keys(a.name, a.keys)
    elif a.op == "hold":
        hold(a.name, a.key, a.seconds, a.cont, a.tap)
    elif a.op == "type":
        type_text(a.name, a.text.rstrip("\r\n"))
    elif a.op == "click":
        click(a.name, a.x, a.y, a.button)
    elif a.op == "doubleclick":
        click(a.name, a.x, a.y, count=2)
    elif a.op == "drag":
        drag(a.name, a.x1, a.y1, a.x2, a.y2)
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
        print(ocr(a.name, white_text=a.white))
    elif a.op == "wait":
        end = time.time() + a.timeout
        want = a.text.lower()
        while time.time() < end:
            if want in ocr(a.name).lower() or want in ocr(a.name, white_text=True).lower():
                print("found")
                return
            time.sleep(2)
        sys.exit(f"timeout waiting for {a.text!r}")
    elif a.op == "sh":
        sys.exit(serial(a.name, a.user, a.pwfile, a.command, a.timeout))
    elif a.op == "put":
        sys.exit(put(a.name, a.user, a.pwfile, a.local, a.guest, a.timeout))
    elif a.op == "listen":
        print(listen(a.name, a.seconds), end="")

if __name__ == "__main__":
    main()
