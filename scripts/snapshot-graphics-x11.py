#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Read-only geometry/map-state snapshot of the caller-selected X display."""
import ctypes as c
import json
import os


def snapshot():
    x = c.CDLL('libX11.so.6')
    D, W, I, U = c.c_void_p, c.c_ulong, c.c_int, c.c_uint

    class Attributes(c.Structure):
        _fields_ = [(name, I) for name in ('x', 'y', 'width', 'height', 'border_width', 'depth')] + [
            ('visual', D), ('root', W), ('window_class', I), ('bit_gravity', I), ('win_gravity', I),
            ('backing_store', I), ('backing_planes', W), ('backing_pixel', W), ('save_under', I),
            ('colormap', W), ('map_installed', I), ('map_state', I), ('all_event_masks', c.c_long),
            ('your_event_mask', c.c_long), ('do_not_propagate_mask', c.c_long), ('override_redirect', I), ('screen', D)]

    x.XOpenDisplay.argtypes = [c.c_char_p]
    x.XOpenDisplay.restype = D
    x.XDefaultRootWindow.argtypes = [D]
    x.XDefaultRootWindow.restype = W
    x.XQueryTree.argtypes = [D, W, c.POINTER(W), c.POINTER(W), c.POINTER(c.POINTER(W)), c.POINTER(U)]
    x.XFetchName.argtypes = [D, W, c.POINTER(D)]
    x.XGetWindowAttributes.argtypes = [D, W, c.POINTER(Attributes)]
    x.XTranslateCoordinates.argtypes = [D, W, W, I, I, c.POINTER(I), c.POINTER(I), c.POINTER(W)]
    x.XFree.argtypes = [D]
    x.XCloseDisplay.argtypes = [D]
    display = x.XOpenDisplay(os.environ['DISPLAY'].encode())
    if not display:
        raise RuntimeError('Cannot inspect selected X display')
    root = x.XDefaultRootWindow(display)
    rows, seen = [], set()

    def walk(window, depth):
        if depth > 8 or window in seen or len(seen) >= 4096:
            return
        seen.add(window)
        attributes, name = Attributes(), D()
        if not x.XGetWindowAttributes(display, window, c.byref(attributes)):
            return
        x.XFetchName(display, window, c.byref(name))
        title = c.string_at(name).decode(errors='replace') if name else ''
        if name:
            x.XFree(name)
        rx, ry, child = I(), I(), W()
        x.XTranslateCoordinates(display, window, root, 0, 0, c.byref(rx), c.byref(ry), c.byref(child))
        query_root, parent, children, count = W(), W(), c.POINTER(W)(), U()
        queried = x.XQueryTree(display, window, c.byref(query_root), c.byref(parent), c.byref(children), c.byref(count))
        if title or (attributes.width >= 100 and attributes.height >= 100):
            rows.append(dict(id=hex(window), parent=hex(parent.value), title=title, depth=depth,
                             geometry=[attributes.x, attributes.y, attributes.width, attributes.height],
                             root_origin=[rx.value, ry.value], map_state=attributes.map_state,
                             override_redirect=attributes.override_redirect))
        if queried:
            try:
                for child in children[:count.value]:
                    walk(child, depth + 1)
            finally:
                if children:
                    x.XFree(children)
    try:
        walk(root, 0)
        return rows
    finally:
        x.XCloseDisplay(display)


if __name__ == '__main__':
    print(json.dumps(snapshot(), indent=2))
