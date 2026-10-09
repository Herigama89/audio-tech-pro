#!/usr/bin/env python3
"""
Builds the mixer skin from the reference picture (Resources/skin/source.jpg):
cuts out one channel strip, the knobs, the fader cap, the slots, the level
box, the VU meter and the black panel, cleans off the printed text the app
draws itself (channel numbers, band names, values), and writes the pieces to
Resources/skin/ for the app to use.

    python3 Tools/make_skin_from_image.py

Needs Pillow, NumPy and OpenCV (pip install pillow numpy opencv-python-headless).
Replace source.jpg with a sharper version of the same picture and run this
again for a crisper skin (the coordinates below are for a 1024 x 676 picture;
a bigger one is scaled down to that first, then cut at full resolution).
"""
import os
import sys
import numpy as np
import cv2
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SKIN = os.path.join(HERE, '..', 'Resources', 'skin')
SRC = os.path.join(SKIN, 'source.jpg')

BASE_W, BASE_H = 1024, 676          # the coordinates below are for this size
ART = 3.5                           # the strip artwork is drawn at 3.5x (56 px -> 196 wide)

src = Image.open(SRC).convert('RGB')
F = src.width / BASE_W              # cut a larger picture at its own resolution
img = np.asarray(src).copy()


def box(x0, y0, x1, y1):
    """Base coordinates -> pixel slice of the source."""
    return slice(int(round(y0 * F)), int(round(y1 * F))), slice(int(round(x0 * F)), int(round(x1 * F)))


def crop(x0, y0, x1, y1):
    ys, xs = box(x0, y0, x1, y1)
    return img[ys, xs].copy()


def inpaint(a, mask, radius=3):
    return cv2.inpaint(a, (mask > 0).astype(np.uint8) * 255, radius, cv2.INPAINT_TELEA)


def text_mask(a, rect, darker=28, lighter=None):
    """Pixels of printed text inside rect (x0, y0, x1, y1 in the crop's base coords): darker than the face around them."""
    x0, y0, x1, y1 = [int(round(v * F)) for v in rect]
    region = a[y0:y1, x0:x1].astype(int).mean(axis=2)
    face = np.median(region)
    m = np.zeros(a.shape[:2], np.uint8)
    sel = region < face - darker
    if lighter is not None:
        sel |= region > face + lighter
    m[y0:y1, x0:x1] = sel
    return cv2.dilate(m, np.ones((3, 3), np.uint8), iterations=1)


def rect_mask(a, rect):
    x0, y0, x1, y1 = [int(round(v * F)) for v in rect]
    m = np.zeros(a.shape[:2], np.uint8)
    m[y0:y1, x0:x1] = 1
    return m


def flatten(a, rect, inset=1):
    """Fills a key or badge with its own (median) colour, keeping its edge: removes what's printed on it."""
    x0, y0, x1, y1 = [int(round(v * F)) for v in rect]
    i = max(1, int(round(inset * F)))
    inner = a[y0 + i:y1 - i, x0 + i:x1 - i]
    colour = np.median(inner.reshape(-1, 3), axis=0)
    # keep a soft top-to-bottom shade
    rows = np.median(inner, axis=1)
    for r in range(inner.shape[0]):
        inner[r, :] = (rows[r] * 0.5 + colour * 0.5)
    a[y0 + i:y1 - i, x0 + i:x1 - i] = inner


def save(a, name, scale, alpha=None):
    h, w = a.shape[:2]
    out = Image.fromarray(a).resize((int(round(w / F * scale)), int(round(h / F * scale))), Image.LANCZOS)
    if alpha is not None:
        al = Image.fromarray(alpha).resize(out.size, Image.LANCZOS)
        out = out.convert('RGBA')
        out.putalpha(al)
    out.save(os.path.join(SKIN, name), optimize=True)
    print('wrote', name, out.size)


def circle_alpha(shape, cx, cy, r, soft=0.6):
    h, w = shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.sqrt((xx + 0.5 - cx * F) ** 2 + (yy + 0.5 - cy * F) ** 2)
    return (np.clip((r * F - d) / (soft * F) + 0.5, 0, 1) * 255).astype(np.uint8)


# ---------------------------------------------------------------------------
# The channel strip: strip 2 of the picture, x 60..116, from its top (183).
# ---------------------------------------------------------------------------
SX, SY = 60, 183
top = crop(SX, SY, SX + 56, 429)              # knobs, keys, Dynamics, the two values


def local(r):
    return (r[0] - SX, r[1] - SY, r[2] - SX, r[3] - SY)


m = np.zeros(top.shape[:2], np.uint8)
m |= text_mask(top, local((61, 187, 71, 202)), darker=40)                 # the printed channel number
for y0, y1 in ((209, 236), (245, 272), (280, 306), (318, 343)):          # band names and values beside the knobs
    m |= text_mask(top, local((94, y0, 115, y1)), darker=30)
m |= text_mask(top, local((86, 306, 102, 318)), darker=30)                # "LAUF"
m |= text_mask(top, local((61, 407, 115, 424)), darker=30)                # the two values under Dynamics
top = inpaint(top, m, 2)

flatten(top, local((100, 189, 111, 200)))                                # the number badge -> the EQ lamp
flatten(top, local((102, 308, 111, 318)))                                # the lamp beside "LAUF" -> the DYN lamp
flatten(top, local((70, 351, 88, 368)), inset=2)                         # "<"  -> M
flatten(top, local((89, 351, 108, 368)), inset=2)                        # ">"  -> S
save(top, 'strip_top.png', ART)

# A plain stretch of the face with the strip's edges, to fill the lower half (tiled downwards).
face = crop(SX, 543, SX + 56, 553)
save(face, 'strip_face.png', ART)

# The slots: an empty insert slot, and a send slot without its arrows.
slot = crop(SX + 2, 450, SX + 54, 462)
save(slot, 'slot.png', ART)
send = crop(SX + 2, 514, SX + 54, 528)
flatten(send, (36, 1, 51, 13), inset=1)
send = inpaint(send, text_mask(send, (8, 2, 36, 13), lighter=35, darker=255), 2)
save(send, 'send.png', ART)

# The level box under the meter.
db = crop(SX + 22, 644, SX + 54, 661)
db = inpaint(db, text_mask(db, (3, 3, 29, 14), lighter=18, darker=255), 3)
save(db, 'db_box.png', ART)

# The fader cap (x 69..86, y 562..591 in the picture), with its soft shadow below and right.
cap = crop(68, 562, 89, 598)
lum = cap.astype(int).mean(axis=2)
alpha = np.clip((128 - lum) * 4, 0, 200).astype(np.uint8)              # the shadow on the face
cy0, cy1, cx0, cx1 = [int(round(v * F)) for v in (0, 30, 1, 19)]
alpha[cy0:cy1, cx0:cx1] = 255                                            # the cap itself
groove = np.zeros_like(alpha, dtype=bool)
gx0, gx1 = int(round(8 * F)), int(round(11 * F))
groove[int(round(30 * F)):, gx0:gx1] = True                              # the fader's slot under it: not part of the cap
alpha[groove] = 0
save(cap, 'fader_cap.png', 4.0, alpha)

# ---------------------------------------------------------------------------
# Knobs: the body only (the printed scale stays on the face), pointer removed.
# ---------------------------------------------------------------------------
def knob(cx, cy, r, name, pointer_bright):
    k = crop(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1)
    lum = k.astype(int).mean(axis=2)
    ccx, ccy = r + 1, r + 1
    a = circle_alpha(k.shape, ccx, ccy, r)
    inner = circle_alpha(k.shape, ccx, ccy, r * 0.82) > 128
    # The pointer: much brighter (on a black knob) or darker (on a white one) than the knob round it.
    med = np.median(lum[inner])
    p = (lum > med + 45) if pointer_bright else (lum < med - 60)
    p &= inner
    k = inpaint(k, cv2.dilate(p.astype(np.uint8), np.ones((3, 3), np.uint8)), 2)
    save(k, name, 8.0, a)


knob(81.6, 258.6, 9.0, 'knob_black.png', True)      # the EQ knobs
knob(88.0, 398.5, 12.3, 'knob_white.png', False)    # Dynamics (its dark skirt and white top)

# ---------------------------------------------------------------------------
# The centre section: the VU meter's face (needle removed) and the black panel.
# ---------------------------------------------------------------------------
vu = crop(873, 217, 951, 257)
lum = vu.astype(int).mean(axis=2)
needle = lum < np.median(lum) - 70
needle[:4, :] = False
needle[-1:, :] = False
vy0, vy1, vx0, vx1 = [int(round(v * F)) for v in (238 - 217, 249 - 217, 906 - 873, 925 - 873)]
needle[vy0:vy1, vx0:vx1] = False      # keep the printed "VU"
vu = inpaint(vu, cv2.dilate(needle.astype(np.uint8), np.ones((3, 3), np.uint8)), 2)
save(vu, 'vu_face.png', 4.0)

panel = crop(858, 285, 868, 420)
save(panel, 'panel.png', 4.0)

# The bus compressor's white knob (body only), from its Threshold knob.
knob(886.7, 310.0, 8.2, 'knob_bus.png', False)
