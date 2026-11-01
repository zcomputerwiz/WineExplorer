#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Read actual surfaceless OpenGL capabilities; no Wine/pixel acceptance claim."""
import argparse
import ctypes as c
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--expect-renderer', required=True, help='Explicit renderer regular expression')
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=False)
    egl = c.CDLL('libEGL.so.1')
    gl = c.CDLL('libGL.so.1')
    for name, result, arguments in (
        ('eglGetPlatformDisplay', c.c_void_p, [c.c_uint, c.c_void_p, c.POINTER(c.c_ssize_t)]),
        ('eglInitialize', c.c_uint, [c.c_void_p, c.POINTER(c.c_int), c.POINTER(c.c_int)]),
        ('eglBindAPI', c.c_uint, [c.c_uint]),
        ('eglChooseConfig', c.c_uint, [c.c_void_p, c.POINTER(c.c_int), c.POINTER(c.c_void_p), c.c_int, c.POINTER(c.c_int)]),
        ('eglCreateContext', c.c_void_p, [c.c_void_p, c.c_void_p, c.c_void_p, c.POINTER(c.c_int)]),
        ('eglMakeCurrent', c.c_uint, [c.c_void_p, c.c_void_p, c.c_void_p, c.c_void_p]),
        ('eglDestroyContext', c.c_uint, [c.c_void_p, c.c_void_p]),
        ('eglTerminate', c.c_uint, [c.c_void_p]),
        ('eglGetError', c.c_uint, [])):
        function = getattr(egl, name)
        function.restype, function.argtypes = result, arguments
    gl.glGetString.restype, gl.glGetString.argtypes = c.c_char_p, [c.c_uint]
    gl.glGetStringi.restype, gl.glGetStringi.argtypes = c.c_char_p, [c.c_uint, c.c_uint]
    gl.glGetIntegerv.argtypes = [c.c_uint, c.POINTER(c.c_int)]
    display = context = None
    record = {'classification': 'EGL capability inventory only, not D3D or Chromium acceptance', 'passed': False}

    def require(value, label):
        if not value:
            raise RuntimeError('%s: EGL error 0x%x' % (label, egl.eglGetError()))
        return value

    try:
        display = require(egl.eglGetPlatformDisplay(0x31DD, None, None), 'Surfaceless display')
        major, minor = c.c_int(), c.c_int()
        require(egl.eglInitialize(display, c.byref(major), c.byref(minor)), 'Initialize')
        require(egl.eglBindAPI(0x30A2), 'OpenGL API')
        attributes = (c.c_int * 5)(0x3040, 8, 0x3033, 1, 0x3038)
        config, count = c.c_void_p(), c.c_int()
        require(egl.eglChooseConfig(display, attributes, c.byref(config), 1, c.byref(count)) and count.value, 'Choose config')
        attributes = (c.c_int * 7)(0x3098, 4, 0x30FB, 1, 0x30FD, 1, 0x3038)
        context = require(egl.eglCreateContext(display, config, None, attributes), 'OpenGL4.1 core context')
        require(egl.eglMakeCurrent(display, None, None, context), 'Make current')
        for label, enum in (('vendor', 0x1F00), ('renderer', 0x1F01), ('version', 0x1F02)):
            value = gl.glGetString(enum)
            if not value:
                raise RuntimeError('Missing GL ' + label)
            record[label] = value.decode('utf-8', errors='replace')
        gl.glGetIntegerv(0x821D, c.byref(count))
        if not 0 < count.value < 10000:
            raise RuntimeError('Invalid extension count')
        record['extensions'] = sorted(gl.glGetStringi(0x1F03, i).decode('ascii') for i in range(count.value))
        record['wine_feature_gate_inputs'] = {name: name in record['extensions'] for name in (
            'GL_ARB_polygon_offset_clamp', 'GL_EXT_polygon_offset_clamp', 'GL_ARB_sampler_objects',
            'GL_ARB_tessellation_shader', 'GL_ARB_draw_indirect', 'GL_ARB_texture_compression_bptc')}
        renderer = record['renderer']
        record['passed'] = bool(re.search(args.expect_renderer, renderer) and
                                not re.search('llvmpipe|softpipe|swiftshader|Microsoft Basic Render', renderer, re.I))
    except Exception as error:
        record['failure'] = repr(error)
    finally:
        if display:
            egl.eglMakeCurrent(display, None, None, None)
            if context:
                egl.eglDestroyContext(display, context)
            egl.eglTerminate(display)
        (args.output_dir / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({key: value for key, value in record.items() if key != 'extensions'}, indent=2))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
