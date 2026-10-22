# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Read-only pixel oracles for native diagnostic captures."""


def frame_pixels(image, state):
    image = image.convert('RGB')
    x, y, width, height = (state[key] for key in ('x', 'y', 'width', 'height'))
    if width < 100 or height < 120 or x < 0 or y < 0 or x + width > image.width or y + height > image.height:
        raise ValueError('Diagnostic window is outside the owned capture')
    neutral = lambda pixel: min(pixel) >= 128 and max(pixel) - min(pixel) <= 4
    strips = dict(left=[image.getpixel((x + 1, row)) for row in range(y + 80, y + height - 8)],
                  right=[image.getpixel((x + width - 2, row)) for row in range(y + 80, y + height - 8)],
                  bottom=[image.getpixel((column, y + height - 2)) for column in range(x + 8, x + width - 8)])
    fractions = {name: sum(map(neutral, pixels)) / len(pixels) for name, pixels in strips.items()}
    blue = sum(pixel == (21, 101, 192) for pixel in image.getdata())
    return dict(rim_fractions=fractions, blue_pixels=blue,
                passed=all(value >= .98 for value in fractions.values()) and blue == 1600)


def recovery_pixels(image, state, scale, marker, background, baseline=None):
    """Check known frozen fixture pixels at observed window geometry and scale.

    ``state`` is the observed HWND rectangle, ``marker`` is the renderer's
    observed CSS client rectangle, and screenshot coordinates are derived from
    those observations. ``baseline`` lets recovered outer captures prove the
    same frame rim remained present without treating text rasterization as a
    content signal.
    """
    image = image.convert('RGB')
    x, y, width, height = (state[key] for key in ('x', 'y', 'width', 'height'))
    if (not isinstance(scale, (int, float)) or scale <= 0
            or width < 100 or height < 120 or x < 0 or y < 0):
        raise ValueError('Invalid observed recovery geometry or scale')
    sx, sy = round(x * scale), round(y * scale)
    sw, sh = round(width * scale), round(height * scale)
    if sw < 100 or sh < 120 or sx + sw > image.width or sy + sh > image.height:
        raise ValueError('Observed recovery window is outside the captured output')
    client = state['client_bounds']
    cx, cy, cw, ch = (client[key] for key in ('x', 'y', 'width', 'height'))
    mx, my, mw, mh = (marker[key] for key in ('x', 'y', 'width', 'height'))
    if (min(mx, my, mw, mh) < 0 or mw < 8 or mh < 8
            or mx + mw > cw or my + mh > ch):
        raise ValueError('Observed client marker is outside the Electron client geometry')
    box = (round((cx + mx + mw / 2) * scale), round((cy + my + mh / 2) * scale))
    body = (round((cx + mx + mw + 8) * scale), round((cy + my + mh / 2) * scale))
    actual_box, actual_body = image.getpixel(box), image.getpixel(body)
    marker_region = [(column, row)
                     for column in range(round((cx + mx + mw * .25) * scale),
                                         round((cx + mx + mw * .75) * scale))
                     for row in range(round((cy + my + mh * .25) * scale),
                                      round((cy + my + mh * .75) * scale))]
    blue_pixels = sum(image.getpixel(point) == (21, 101, 192) for point in marker_region)
    # Four narrow frame strips avoid title text and sample the observed HWND rim.
    strips = {
        'left': [(sx + 1, row) for row in range(sy + round(80 * scale), sy + sh - 8)],
        'right': [(sx + sw - 2, row) for row in range(sy + round(80 * scale), sy + sh - 8)],
        'bottom': [(column, sy + sh - 2) for column in range(sx + 8, sx + sw - 8)],
    }
    rim = {name: [image.getpixel(point) for point in points] for name, points in strips.items()}
    rim_match = None
    if baseline is not None:
        rim_match = {name: (len(samples) == len(baseline.get(name, [])) and bool(samples)
                            and sum(a == b for a, b in zip(samples, baseline[name])) / len(samples) >= .98)
                     for name, samples in rim.items()}
    expected_box, expected_body = (21, 101, 192), tuple(background)
    return {
        'passed': actual_box == expected_box and actual_body == expected_body
                  and blue_pixels / len(marker_region) >= .98
                  and (rim_match is None or all(rim_match.values())),
        'geometry': {'window': {'x': x, 'y': y, 'width': width, 'height': height},
                     'scale': scale, 'output_window': {'x': sx, 'y': sy, 'width': sw, 'height': sh},
                     'marker': {'x': mx, 'y': my, 'width': mw, 'height': mh}},
        'samples': {'blue_marker': {'point': box, 'expected': expected_box, 'actual': actual_box},
                    'marker_region': {'sampled_pixels': len(marker_region), 'blue_pixels': blue_pixels},
                    'background': {'point': body, 'expected': expected_body, 'actual': actual_body},
                    'rim_pixels': {name: len(points) for name, points in strips.items()},
                    'rim_match': rim_match},
        'rim': rim,
    }


def recovery_page_pixels(image, viewport, scale, marker, background):
    """Check the client-page capture in renderer CSS coordinates."""
    image = image.convert('RGB')
    width, height = viewport['width'], viewport['height']
    expected_size = (round(width * scale), round(height * scale))
    if image.size != expected_size:
        raise ValueError('Electron page capture dimensions do not match observed viewport/scale')
    mx, my, mw, mh = (marker[key] for key in ('x', 'y', 'width', 'height'))
    if min(mx, my, mw, mh) < 0 or mw < 8 or mh < 8 or mx + mw > width or my + mh > height:
        raise ValueError('Observed client marker is outside the renderer viewport')
    box = (round((mx + mw / 2) * scale), round((my + mh / 2) * scale))
    body = (round((mx + mw + 8) * scale), round((my + mh / 2) * scale))
    actual_box, actual_body = image.getpixel(box), image.getpixel(body)
    marker_region = [(column, row)
                     for column in range(round((mx + mw * .25) * scale), round((mx + mw * .75) * scale))
                     for row in range(round((my + mh * .25) * scale), round((my + mh * .75) * scale))]
    blue_pixels = sum(image.getpixel(point) == (21, 101, 192) for point in marker_region)
    return {'passed': actual_box == (21, 101, 192) and actual_body == tuple(background)
                    and blue_pixels / len(marker_region) >= .98,
            'geometry': {'viewport': viewport, 'scale': scale, 'marker': marker},
            'samples': {'blue_marker': {'point': box, 'expected': (21, 101, 192), 'actual': actual_box},
                        'marker_region': {'sampled_pixels': len(marker_region), 'blue_pixels': blue_pixels},
                        'background': {'point': body, 'expected': tuple(background), 'actual': actual_body}}}
