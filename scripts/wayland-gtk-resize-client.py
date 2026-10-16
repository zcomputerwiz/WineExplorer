# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Real GTK3 client for isolated compositor/controller acceptance."""
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk

window = Gtk.Window(title="Wex real native client")
window.set_default_size(240, 180)
area = Gtk.DrawingArea()


def draw(widget, context):
    context.set_source_rgb(0x33 / 255, 0x88 / 255, 0x55 / 255)
    context.paint()
    return False


area.connect("draw", draw)
window.add(area)
window.connect("destroy", Gtk.main_quit)
window.show_all()
Gtk.main()
