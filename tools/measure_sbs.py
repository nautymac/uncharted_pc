"""Horizontal parallax of a side-by-side stereo screenshot (tools/drive.py `shot`).

Usage: python tools/measure_sbs.py out/try/u1/shot.png [out/try/u1/shot2.png ...]

The picture is split into its left and right halves (each squeezed to half the width). For
horizontal bands the right half is shifted against the left and the shift with the smallest
mean absolute difference is the parallax in half-width pixels: negative when the right eye's
picture is shifted left (uncrossed, behind the screen plane), positive in front of it. Bands
with little horizontal detail (flat colour) are marked. The HUD band is the bottom-left corner
(the ammunition counter and the health marks of Uncharted).
"""
import sys

import numpy as np
from PIL import Image


def band_shift(left, right, y0, y1, x0, x1, max_shift=40):
    a = left[y0:y1, x0:x1].astype(np.float32)
    b_full = right[y0:y1].astype(np.float32)
    detail = float(np.mean(np.abs(np.diff(a, axis=1))))
    best, best_shift = None, 0
    for s in range(-max_shift, max_shift + 1):
        bx0, bx1 = x0 + s, x1 + s
        if bx0 < 0 or bx1 > right.shape[1]:
            continue
        d = float(np.mean(np.abs(a - b_full[:, bx0:bx1])))
        if best is None or d < best:
            best, best_shift = d, s
    return best_shift, best, detail


def measure(path):
    image = np.asarray(Image.open(path).convert('L'))
    h, w = image.shape
    half = w // 2
    left, right = image[:, :half], image[:, half:half * 2]
    print(f'{path}: {w}x{h}, eyes {half}x{h}')
    identical = float(np.mean(np.abs(left.astype(np.int16) - right.astype(np.int16))))
    print(f'  mean |L-R| = {identical:.2f} (0 = the halves are the same picture)')
    margin = half // 8
    bands = [
        ('top (far background)', int(h * 0.08), int(h * 0.30)),
        ('middle', int(h * 0.38), int(h * 0.58)),
        ('bottom (near floor)', int(h * 0.72), int(h * 0.92)),
    ]
    for name, y0, y1 in bands:
        shift, diff, detail = band_shift(left, right, y0, y1, margin, half - margin)
        flag = '' if detail > 2.0 else '  (little detail)'
        print(f'  {name:22s} rows {y0:4d}-{y1:4d}: shift {shift:+4d} px (mad {diff:.1f}, detail {detail:.1f}){flag}')
    # HUD: bottom-left corner, a small window with a tight search.
    shift, diff, detail = band_shift(left, right, int(h * 0.80), int(h * 0.97), int(half * 0.03),
                                     int(half * 0.30), max_shift=12)
    flag = '' if detail > 2.0 else '  (little detail)'
    print(f'  {"HUD corner":22s}: shift {shift:+4d} px (mad {diff:.1f}, detail {detail:.1f}){flag}')


if __name__ == '__main__':
    for p in sys.argv[1:]:
        measure(p)
