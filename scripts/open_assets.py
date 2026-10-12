#!/usr/bin/env python3
"""Build the open replacements of the Windows XP pictures the UI was designed against.

Every icon and bitmap in this table comes from ReactOS (references/reactos, GPL; its icons are mostly Tango-based),
from Wine, or is drawn here.  The genuine XP files are never committed: a developer with a local copy puts them over
these (see docs/ASSETS.md).  Entries marked "wanted" still need art of their own; docs/ASSETS.md lists them.

  open_assets.py build    write the files under wine/ and assets/open_assets.json
  open_assets.py list     print the table
"""
import json, os, sys
from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ROS = os.path.join(ROOT, "references", "reactos")
WINE = os.path.join(ROOT, "wine")

# target (under wine/) -> (kind, source, note); kinds:
#   copy        copy the file (source under references/reactos, or wine:<path> for Wine's own files)
#   icon2bmp    render the icon's largest frame centered on a bitmap: source, size, background
#   draw        drawn by a function below
#   wanted      no equivalent yet: a neutral placeholder until someone draws one
R = "dll/cpl/main/resources/"
TABLE = {
    "dlls/desk.cpl/desk.ico": ("copy", "dll/cpl/desk/resources/applet.ico"),
    "dlls/desk.cpl/monitor.bmp": ("copy", "dll/cpl/desk/resources/monitor.bmp"),
    "dlls/main.cpl/admfldsh.ico": ("copy", R + "admintools_folder.ico"),
    "dlls/main.cpl/clklock.ico": ("copy", R + "lock_key.ico"),
    "dlls/main.cpl/dblclk.ico": ("copy", R + "mouse_button.ico"),
    "dlls/main.cpl/delay.ico": ("copy", R + "repeat_delay.ico"),
    "dlls/main.cpl/fldr_clo.ico": ("copy", R + "folder_closed.ico"),
    "dlls/main.cpl/fldr_opn.ico": ("copy", R + "folder_open.ico"),
    "dlls/main.cpl/fntfldsh.ico": ("copy", "dll/win32/shell32/res/icons/39.ico"),
    "dlls/main.cpl/keybd.ico": ("copy", R + "keyboard.ico"),
    "dlls/main.cpl/mouse.bmp": ("icon2bmp", (R + "mouse.ico", (83, 80), (0, 255, 255))),
    "dlls/main.cpl/mouse.ico": ("copy", R + "mouse.ico"),
    "dlls/main.cpl/prtfldsh.ico": ("copy", "dll/win32/shell32/res/icons/38.ico"),
    "dlls/main.cpl/pspeed.ico": ("copy", R + "mouse_speed.ico"),
    "dlls/main.cpl/pt02.ico": ("copy", R + "mouse_trails.ico"),
    "dlls/main.cpl/pt03.ico": ("copy", R + "mouse_speed.ico"),
    "dlls/main.cpl/pt04.ico": ("copy", R + "mouse_button.ico"),
    "dlls/main.cpl/repeat.ico": ("copy", R + "repeat_rate.ico"),
    "dlls/main.cpl/sglclk.ico": ("copy", R + "mouse_left.ico"),
    "dlls/main.cpl/snapto.ico": ("copy", R + "mouse_button.ico"),
    "dlls/main.cpl/sonar.ico": ("copy", R + "mouse_pointer.ico"),
    "dlls/main.cpl/vanish.ico": ("copy", R + "mouse_hide.ico"),
    "dlls/main.cpl/wheel.ico": ("copy", R + "mouse_wheel.ico"),
    "dlls/mmsys.cpl/play.bmp": ("draw", "play"),
    "dlls/mmsys.cpl/stop.bmp": ("draw", "stop"),
    "dlls/mmsys.cpl/playback.ico": ("copy", "dll/cpl/mmsys/resources/3004.ico"),
    "dlls/mmsys.cpl/speaker.ico": ("copy", "dll/cpl/mmsys/resources/speaker.ico"),
    "dlls/mmsys.cpl/syssound.ico": ("copy", "dll/cpl/mmsys/resources/4353.ico"),
    "dlls/mmsys.cpl/volspkr.bmp": ("icon2bmp", ("dll/cpl/mmsys/resources/speaker.ico", (72, 72), (192, 192, 192))),
    "dlls/ncpa.cpl/netfol.ico": ("copy", "dll/shellext/netshell/res/netshell.ico"),
    "dlls/netshell/foldwiz.ico": ("copy", "dll/shellext/netshell/res/netsetup.ico"),
    "dlls/netshell/netfol.ico": ("copy", "dll/shellext/netshell/res/netshell.ico"),
    "dlls/powercfg.cpl/batt.ico": ("copy", "dll/cpl/powercfg/resources/batteryfull.ico"),
    "dlls/powercfg.cpl/hibernate.ico": ("copy", "dll/cpl/powercfg/resources/hibernate.ico"),
    "dlls/powercfg.cpl/plug.ico": ("copy", "dll/cpl/powercfg/resources/ac.ico"),
    "dlls/powercfg.cpl/pwrmng.ico": ("copy", "dll/cpl/powercfg/resources/acdc.ico"),
    "dlls/sysdm.cpl/sysdm.ico": ("copy", "dll/cpl/sysdm/resources/applet.ico"),
    "dlls/sysdm.cpl/windows.bmp": ("wanted", ("the product logo on System Properties > General", (118, 113), (255, 1, 255))),
    "dlls/timedate.cpl/timedate.ico": ("copy", "dll/cpl/timedate/resources/applet.ico"),
    "dlls/timedate.cpl/world.bmp": ("copy", "dll/cpl/timedate/resources/world.bmp"),
    # ReactOS's 21, 22, 25 and 41 are one file, identical to Windows XP's blank icon: ours are drawn or wanted
    "dlls/devmgr/icons/21.ico": ("wanted", ("Device Manager class icon: IEEE 1394 Bus host controllers, 16/32 px", None, None)),
    "dlls/devmgr/icons/22.ico": ("wanted", ("Device Manager class icon: Infrared devices and Modems, 16/32 px", None, None)),
    "dlls/devmgr/icons/25.ico": ("wanted", ("Device Manager class icon: Smart card readers, 16/32 px", None, None)),
    "dlls/devmgr/icons/41.ico": ("draw", "blank"),
    "programs/explorer/startflag.bmp": ("wanted", ("Start button logo as a 25 x 20 bitmap on black (the Start "
                                                   "button draws it); until then Wine's logo",
                                                   "wine:dlls/user32/resources/oic_winlogo.ico", (25, 20))),
    "programs/explorer/batt.ico": ("copy", "dll/shellext/stobject/resources/battery/5.ico"),
    "programs/explorer/battplug.ico": ("copy", "dll/shellext/stobject/resources/battery/charging4.ico"),
    "programs/explorer/explorer.ico": ("copy", "dll/win32/shell32/res/icons/46.ico"),
    "programs/explorer/hotplug.ico": ("copy", "dll/shellext/stobject/resources/2.ico"),
    "programs/explorer/mute.ico": ("copy", "dll/shellext/stobject/resources/4.ico"),
    "programs/explorer/volume.ico": ("copy", "dll/shellext/stobject/resources/3.ico"),
    "programs/explorer/net_adapter.ico": ("copy", "dll/win32/setupapi/res/5.ico"),
    "programs/explorer/net_protocol.ico": ("copy", "dll/win32/setupapi/res/6.ico"),
    "programs/explorer/net_client.ico": ("copy", "dll/win32/setupapi/res/7.ico"),
    "programs/explorer/net_service.ico": ("copy", "dll/win32/setupapi/res/8.ico"),
    "programs/explorer/net_lan.ico": ("copy", "dll/shellext/netshell/res/netidle.ico"),
    "programs/explorer/net_blank.ico": ("copy", "dll/shellext/netshell/res/netidle.ico"),
    "programs/explorer/net_recv.ico": ("copy", "dll/shellext/netshell/res/netrec.ico"),
    "programs/explorer/net_xmit.ico": ("copy", "dll/shellext/netshell/res/nettrans.ico"),
    "programs/explorer/net_xmitrecv.ico": ("copy", "dll/shellext/netshell/res/nettrrec.ico"),
    "programs/explorer/net_disconnect.ico": ("copy", "dll/shellext/netshell/res/netoff.ico"),
    "programs/explorer/net_plugged.ico": ("copy", "dll/shellext/netshell/res/neterror.ico"),
    "programs/explorer/net_sig0.ico": ("draw", 0),
    "programs/explorer/net_sig1.ico": ("draw", 1),
    "programs/explorer/net_sig2.ico": ("draw", 2),
    "programs/explorer/net_sig3.ico": ("draw", 3),
    "programs/explorer/net_sig4.ico": ("draw", 4),
    "programs/explorer/net_sig5.ico": ("draw", 5),
    "programs/explorer/net_wireless.ico": ("wanted", ("wireless network adapter (antenna), 16/32/48 px", None, None)),
    "programs/explorer/wzcui.bmp": ("wanted", ("wireless network list strip, 8 x 16 px: checkbox off, checkbox on, "
                                               "access point, access point preferred (blue ring), access point "
                                               "unavailable (red X), ad hoc, ad hoc preferred, ad hoc unavailable",
                                               (128, 16), (0, 128, 128))),
    "programs/explorer/rover.bmp": ("wanted", ("Search Companion still character (shown when no animated character is "
                                               "installed), 56 x 68, magenta background", (56, 68), (255, 0, 255))),
    "programs/explorer/start.ico": ("wanted", ("Start button logo, 16/24/32/48 px; until then Wine's logo", "wine:dlls/user32/resources/oic_winlogo.ico", None)),
    # stock Wine ships these few as Windows' own files: ReactOS's instead (a stock-Wine change, see STOCK_CHANGES.md)
    "programs/regedit/bin.ico": ("copy", "base/applications/regedit/res/bin.ico"),
    "programs/regedit/string.ico": ("copy", "base/applications/regedit/res/string.ico"),
    "programs/regedit/folder.ico": ("copy", "dll/win32/shell32/res/icons/4.ico"),
    "programs/regedit/folderopen.ico": ("copy", "dll/win32/shell32/res/icons/5.ico"),
    "programs/regedit/computer.ico": ("copy", "dll/win32/shell32/res/icons/16.ico"),
    "programs/taskmgr/windowsm.ico": ("copy", "base/applications/taskmgr/res/windowsm.ico"),
    "dlls/comctl32/idi_dragarrow.ico": ("draw", "dragarrow"),
    "programs/sndvol32/asound.ico": ("copy", "base/applications/sndvol32/resources/sndvol32.ico"),
    "programs/sndvol32/lspeak.ico": ("copy", "base/applications/sndvol32/resources/speaker_left.ico"),
    "programs/sndvol32/rspeak.ico": ("copy", "base/applications/sndvol32/resources/speaker_right.ico"),
    "programs/sndvol32/mixrcont.ico": ("copy", "base/applications/sndvol32/resources/sndvol32.ico"),
    "programs/sndvol32/mute.ico": ("copy", "dll/shellext/stobject/resources/4.ico"),
}


def source_path(src):
    return os.path.join(WINE, src[5:]) if src.startswith("wine:") else os.path.join(ROS, src)


def largest_frame(path):
    im = Image.open(path)
    sizes = sorted(im.info.get("sizes", {im.size}), key=lambda s: s[0])
    small = [s for s in sizes if s[0] <= 48]
    im.size = small[-1] if small else sizes[0]
    return im.convert("RGBA")


def icon2bmp(src, size, bg, out):
    canvas = Image.new("RGB", size, bg)
    icon = largest_frame(source_path(src))
    icon.thumbnail((size[0] - 8, size[1] - 8))
    canvas.paste(icon, ((size[0] - icon.width) // 2, (size[1] - icon.height) // 2), icon)
    canvas.save(out)


def save_ico(frames, out):
    frames[0].save(out, format="ICO", sizes=[f.size for f in frames], append_images=frames[1:])


def signal_icon(level, out):
    """Signal strength: an antenna mast and five rising bars, `level` of them green."""
    frames = []
    for s in (48, 32, 16):
        im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        u = s / 16.0
        d.line([(2 * u, 5 * u), (2 * u, 12 * u)], fill=(40, 40, 40, 255), width=max(1, int(u)))
        d.ellipse([1 * u, 3.5 * u, 3 * u, 5.5 * u], outline=(40, 40, 40, 255), width=max(1, int(u / 2)))
        for i in range(5):
            x0, h = (4.5 + i * 2.3) * u, (2 + i * 2) * u
            box = [x0, 12 * u - h, x0 + 1.6 * u, 12 * u]
            on = i < level
            d.rectangle(box, fill=(40, 170, 40, 255) if on else (200, 200, 200, 255), outline=(60, 60, 60, 255))
        frames.append(im)
    save_ico(frames, out)


def drag_arrow(out):
    """The drag list's insertion mark: a black arrow pointing right, 32 x 32."""
    im = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    ImageDraw.Draw(im).polygon([(6, 12), (16, 12), (16, 6), (26, 16), (16, 26), (16, 20), (6, 20)], fill=(0, 0, 0, 255))
    save_ico([im], out)


def glyph(kind, out):
    """1-bit 23 x 23 button glyphs (black on white): a play triangle or a stop square."""
    im = Image.new("1", (23, 23), 1)
    d = ImageDraw.Draw(im)
    if kind == "play":
        d.polygon([(8, 6), (8, 16), (14, 11)], fill=0)
    else:
        d.rectangle([7, 7, 15, 15], fill=0)
    im.save(out)


def placeholder(desc, size, bg, out):
    """A neutral stand-in: a gray frame with a question mark, so missing art is obvious."""
    if out.endswith(".ico"):
        frames = []
        for s in (48, 32, 16):
            im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            d.rectangle([1, 1, s - 2, s - 2], outline=(110, 110, 110, 255), fill=(230, 230, 230, 255))
            d.text((s // 2 - 3, s // 2 - 6), "?", fill=(80, 80, 80, 255))
            frames.append(im)
        save_ico(frames, out)
    else:
        im = Image.new("RGB", size, bg)
        d = ImageDraw.Draw(im)
        d.rectangle([2, 2, size[0] - 3, size[1] - 3], outline=(110, 110, 110))
        if size[1] >= 20: d.text((size[0] // 2 - 3, size[1] // 2 - 6), "?", fill=(80, 80, 80))
        im.save(out)


def build():
    manifest = []
    for target, (kind, src) in sorted(TABLE.items()):
        out = os.path.join(WINE, target)
        entry = {"path": "wine/" + target, "kind": kind}
        if kind == "copy":
            with open(source_path(src), "rb") as f: data = f.read()
            with open(out, "wb") as f: f.write(data)
            entry["source"] = ("wine/" + src[5:]) if src.startswith("wine:") else "reactos/" + src
        elif kind == "icon2bmp":
            icon2bmp(src[0], src[1], src[2], out)
            entry["source"] = "reactos/" + src[0]
        elif kind == "draw":
            if isinstance(src, int): signal_icon(src, out)
            elif src == "dragarrow": drag_arrow(out)
            elif src == "blank": save_ico([Image.new("RGBA", (s, s), (0, 0, 0, 0)) for s in (32, 16)], out)
            else: glyph(src, out)
            entry["source"] = "drawn by scripts/open_assets.py"
        else:
            desc, size, bg = src
            if isinstance(size, str) and bg:   # an interim stand-in from Wine, as a bitmap
                icon2bmp(size, bg, (0, 0, 0), out)
                entry["source"] = "wine/" + size[5:] + " (interim)"
            elif isinstance(size, str):   # an interim stand-in from Wine
                with open(source_path(size), "rb") as f: data = f.read()
                with open(out, "wb") as f: f.write(data)
                entry["source"] = "wine/" + size[5:] + " (interim)"
            else:
                placeholder(desc, size, bg, out)
                entry["source"] = "placeholder"
            entry["wanted"] = desc
        manifest.append(entry)
    with open(os.path.join(ROOT, "assets", "open_assets.json"), "w") as f:
        json.dump(manifest, f, indent=1)
        f.write("\n")
    print("wrote %d files; %d still wanted" % (len(manifest), sum(1 for e in manifest if "wanted" in e)))


if __name__ == "__main__":
    if sys.argv[1:] == ["build"]: build()
    elif sys.argv[1:] == ["list"]:
        for t, (k, s) in sorted(TABLE.items()): print("%-40s %-9s %s" % (t, k, s if isinstance(s, str) else s[0]))
    else: print(__doc__)
