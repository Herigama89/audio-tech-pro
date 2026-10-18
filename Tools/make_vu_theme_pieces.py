#!/usr/bin/env python3
"""
Cuts the "Dark VU" theme's pieces out of the reference mixer picture (eight strips with amber VU
meters at the top), into Resources/skin/looks. Only the pieces: no plugin names, track names or text.

    python3 Tools/make_vu_theme_pieces.py <mixer.png>   (the picture as sent: 1416 x 1038)

  look_vu_amber.png   the VU meter's face. Every strip's needle sits somewhere else, so each pixel is
                      taken from the strip whose needle is farthest from it: a face with no needle.
  look_cap_dark.png   the fader cap (cut out of the dark background)
  look_knob_dark.png  the small round knob (its dot pointing up)
  look_btn_dark.png   the square channel key (its letter painted out)
"""
import sys, os, math
import numpy as np, cv2
from PIL import Image

src = sys.argv[1]
out = os.path.join(os.path.dirname(__file__), '..', 'Resources', 'skin', 'looks')
os.makedirs(out, exist_ok=True)
O = lambda n: os.path.join(out, n)

image = np.array(Image.open(src).convert('RGB')).astype(np.float64)
pitch = 176   # one strip to the next

# --- The VU face ------------------------------------------------------------------------------
x0, y0, w, h = 4, 36, 173, 88
stack = np.stack([image[y0:y0 + h, x0 + pitch * i:x0 + pitch * i + w] for i in range(8)])
pivot = (85.3, 105.0)
tips = [(78, 50), (78, 50), (68, 50), (80, 50), (61, 50), (54, 50), (23, 50), (63, 50)]   # where each needle crosses row 50
angles = [math.degrees(math.atan2(x - pivot[0], pivot[1] - y)) for x, y in tips]
yy, xx = np.mgrid[0:h, 0:w]
theta = np.degrees(np.arctan2(xx - pivot[0], pivot[1] - yy))
best = np.zeros((h, w), int)
bestd = np.full((h, w), -1.0)
for i, a in enumerate(angles):
    d = np.abs(theta - a)
    m = d > bestd
    best[m] = i
    bestd[m] = d[m]
face = np.take_along_axis(stack, best[None, ..., None].repeat(3, axis=3), axis=0)[0]
face = face[2:81, 3:170]   # (the frame's dark edge kept; the level bar under it left out)
Image.fromarray(face.clip(0, 255).astype(np.uint8)).save(O('look_vu_amber.png'))
# The needle turns round (82.3, 103) in this picture: see Looks.cpp.

# --- The fader cap -----------------------------------------------------------------------------
def cutout (box, tolerance=7.0):
    """A piece on the flat dark background: everything joined to the edges in the background's colour goes."""
    c = image[box[1]:box[3], box[0]:box[2]].astype(np.uint8)
    bg = np.median(np.concatenate([c[0], c[-1], c[:, 0], c[:, -1]]), axis=0)
    near = (np.abs(c.astype(np.float64) - bg).max(axis=2) <= tolerance).astype(np.uint8)
    hh, ww = near.shape
    mask = np.zeros((hh + 2, ww + 2), np.uint8)
    filled = near.copy()
    for sx, sy in [(0, 0), (ww - 1, 0), (0, hh - 1), (ww - 1, hh - 1), (0, hh // 2), (ww - 1, hh // 2)]:
        if near[sy, sx]:
            cv2.floodFill (filled, mask, (sx, sy), 2)
    alpha = np.where (filled == 2, 0, 255).astype(np.float32)
    alpha = cv2.GaussianBlur (alpha, (3, 3), 0.6)
    return Image.fromarray (np.dstack ([c, alpha.clip(0, 255).astype(np.uint8)]), 'RGBA')

cap = np.array (cutout ((62, 622, 113, 720), tolerance=14.0))
widths = (cap[..., 3] > 128).sum (axis=1)
rows = np.where (widths > widths.max() * 0.6)[0]                  # the cap itself, not the fader's slot above and below it
cols = np.where ((cap[rows[0]:rows[-1] + 1, :, 3] > 128).any (axis=0))[0]
Image.fromarray (cap[rows[0]:rows[-1] + 1, cols[0]:cols[-1] + 1], 'RGBA').save(O('look_cap_dark.png'))

# --- The knob -----------------------------------------------------------------------------------
def disc (cx, cy, r, edge=1.2):
    pad = int(r * 1.5)
    c = Image.fromarray(image.astype(np.uint8)).convert('RGBA').crop((int(cx - pad), int(cy - pad), int(cx + pad), int(cy + pad)))
    a = np.array(c).astype(np.float32)
    yy2, xx2 = np.mgrid[0:a.shape[0], 0:a.shape[1]]
    a[..., 3] = np.clip((r - np.sqrt((xx2 - pad - (cx - int(cx))) ** 2 + (yy2 - pad - (cy - int(cy))) ** 2)) / edge + 0.5, 0, 1) * 255
    R = int(math.ceil(r)) + 1
    return Image.fromarray(a[pad - R:pad + R, pad - R:pad + R].clip(0, 255).astype(np.uint8), 'RGBA')

disc (90.0, 513.0, 20.5).resize ((72, 72), Image.LANCZOS).save(O('look_knob_dark.png'))

# --- The channel key ---------------------------------------------------------------------------------
key = cutout ((125, 556, 168, 598))
k = np.array(key)
rgb = np.ascontiguousarray(k[..., :3])
lum = rgb.astype(np.float64) @ [0.299, 0.587, 0.114]
letter = np.zeros(lum.shape, np.uint8)
face_level = np.median (lum[8:-8, 8:-8])
letter[7:-7, 7:-7] = (lum[7:-7, 7:-7] > face_level + 10).astype(np.uint8) * 255
letter = cv2.dilate (letter, np.ones((3, 3), np.uint8))
rgb = cv2.inpaint (rgb, letter, 4, cv2.INPAINT_TELEA)
# The face is lit evenly across: each row of it as that row's middle (no trace of the letter left)
for y in range (7, rgb.shape[0] - 7):
    row = rgb[y, 7:-7].astype (np.float64)
    rgb[y, 7:-7] = np.median (row, axis=0).astype (np.uint8)
k[..., :3] = rgb
Image.fromarray(k, 'RGBA').save(O('look_btn_dark.png'))
print ("done")
