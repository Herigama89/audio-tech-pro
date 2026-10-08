"""
Builds the Master Bus Compressor panel artwork from the user's mockup:
crops the compressor section, enlarges it 3x, and paints out everything the
app draws itself (brand text, the meter's needle and scale, knob pointers,
labels and numbers, the LED ladder, the IN lamp).

Usage: python3 make_panel.py <mockup.jpg> <out.png> <preview.png>
"""
import sys
import cv2
import numpy as np
from PIL import Image, ImageFilter

src, out_path, preview_path = sys.argv[1], sys.argv[2], sys.argv[3]

S = 3                      # enlargement
ORIGIN = (1080, 185)       # crop origin in the mockup
SIZE = (220, 505)          # crop size in the mockup

im = Image.open(src).convert("RGB")
crop = im.crop((ORIGIN[0], ORIGIN[1], ORIGIN[0] + SIZE[0], ORIGIN[1] + SIZE[1]))
big = crop.resize((SIZE[0] * S, SIZE[1] * S), Image.LANCZOS)
big = big.filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2))
img = cv2.cvtColor(np.array(big), cv2.COLOR_RGB2BGR)

gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
mask = np.zeros(gray.shape, np.uint8)
local = cv2.GaussianBlur(gray, (0, 0), 9).astype(np.int16)
marks = gray.astype(np.int16) < local - 22          # darker than what's around it: text, dots
highlights = gray.astype(np.int16) > local + 14    # embossed dots and letters have a light edge too

def dark_in(rect, limit):
    x0, y0, x1, y1 = rect
    region = gray[y0:y1, x0:x1] < limit
    mask[y0:y1, x0:x1][region] = 255

def small_marks(region_mask, max_area=900):
    """Keeps only small dark marks (text, dots), not shadows or frames."""
    n, labels, stats, _ = cv2.connectedComponentsWithStats(region_mask.astype(np.uint8), 8)
    keep = np.zeros_like(region_mask, dtype=bool)
    for i in range(1, n):
        if stats[i, cv2.CC_STAT_AREA] <= max_area:
            keep |= labels == i
    return keep

def smooth_fill(image, m, sigma):
    """Fills masked pixels from their surroundings (normalised blur), so flat panels stay flat."""
    w = (m == 0).astype(np.float32)
    out = image.astype(np.float32).copy()
    acc = np.zeros_like(out)
    for s_ in (sigma, sigma * 3, sigma * 9):
        num = cv2.GaussianBlur(image.astype(np.float32) * w[..., None], (0, 0), s_)
        den = cv2.GaussianBlur(w, (0, 0), s_)[..., None]
        est = num / np.maximum(den, 1e-4)
        ok = (den > 0.15) & (acc.sum(axis=2, keepdims=True) == 0)
        acc = np.where(ok, est, acc)
    acc = np.where(acc.sum(axis=2, keepdims=True) == 0, out, acc)
    noise = np.random.default_rng(7).normal(0, 1.4, out.shape).astype(np.float32)
    fill = np.clip(acc + noise, 0, 255)
    out[m > 0] = fill[m > 0]
    return out.astype(np.uint8)

# 1. Meter face: every marking (scale, numbers, "VU", red zone, needle).
face = (100, 70, 545, 266)
x0, y0, x1, y1 = face
fg = gray[y0:y1, x0:x1]
red = (hsv[y0:y1, x0:x1, 1] > 110) & (hsv[y0:y1, x0:x1, 2] > 90) & ((hsv[y0:y1, x0:x1, 0] < 12) | (hsv[y0:y1, x0:x1, 0] > 165))
mask[y0:y1, x0:x1][(fg < 150) | red] = 255

# 2. Brand title: filled below, row by row

# 3. Knobs: labels, numbers and dots around them, and the pointers on the caps.
knobs = [  # centre x, centre y, skirt radius
    (158, 581, 56), (481, 580, 56),
    (159, 790, 56), (481, 791, 56),
    (161, 1009, 56), (481, 1006, 56),
    (477, 1268, 76),
]
yy, xx = np.mgrid[0:gray.shape[0], 0:gray.shape[1]]
labelRects = []
pointerAngles = [0, 0, 0, 55, 0, 0, 0]   # degrees clockwise from straight up, as painted
for (cx, cy, r) in knobs:
    d = np.hypot(xx - cx, yy - cy)
    zone = (d > r + 2) & (d < r + 50) & (yy > 515)
    mask[small_marks(zone & marks)] = 255
    mask[small_marks(zone & highlights, 300)] = 255
    # label under the knob
    half = 135 if r > 70 else 100
    lx0, ly0, lx1, ly1 = cx - half, cy + r + 4, cx + half, cy + r + 64
    labelRects.append ((lx0, lx1, ly0, ly1))
    # pointer: the bright line on the cap (brighter than the cap around it)
    capArea = d < r * 0.78
    capLevel = np.median(gray[capArea])
    pointer = capArea & (gray > capLevel + 22)
    mask[pointer] = 255
    # and the whole stroke, with its shading, along the direction it points
    angle = np.radians(pointerAngles[len(labelRects) - 1])
    tip = (int(cx + np.sin(angle) * r * 0.82), int(cy - np.cos(angle) * r * 0.82))
    cv2.line(mask, (cx, cy), tip, 255, 22 if r < 70 else 28)
    cv2.circle(mask, (cx, cy), 12, 255, -1)       # the pointer's root

# 4. LED ladder: filled below, row by row

# 5. IN lamp (drawn in code, lit or not)
mask[1250:1296, 296:342] = 255

# 6. "Bypass/In" caption


mask = cv2.dilate(mask, np.ones((9, 9), np.uint8), iterations=1)

# Keep the knob bodies and the meter frame exactly as they were.
protect = np.zeros_like(mask)
for (cx, cy, r) in knobs:
    cv2.circle(protect, (cx, cy), int(r * 0.98), 255, -1)
    cv2.circle(protect, (cx, cy), int(r * 0.78), 0, -1)       # ...but the cap may be repainted
mask[protect > 0] = 0

# The meter face is redrawn from its own glow: blur it with the markings left out.
faceMask = np.zeros_like(mask)
faceMask[face[1]:face[3], face[0]:face[2]] = mask[face[1]:face[3], face[0]:face[2]]
img = smooth_fill(img, faceMask, 10)
img[face[1]:face[3], face[0]:face[2]] = cv2.GaussianBlur(img[face[1]:face[3], face[0]:face[2]], (0, 0), 6)
mask[face[1]:face[3], face[0]:face[2]] = 0

# Knob caps: fill the old pointer from the cap's own shading around it.
capMask = np.zeros_like(mask)
for (cx, cy, r) in knobs:
    cv2.circle(capMask, (cx, cy), int(r * 0.8), 255, -1)
capMask &= mask
for (cx, cy, r) in knobs:
    disc = np.zeros_like(mask)
    cv2.circle(disc, (cx, cy), int(r * 0.8), 255, -1)
    w = ((disc > 0) & (capMask == 0)).astype(np.float32)
    est = np.zeros(img.shape, np.float32)
    done = np.zeros(mask.shape, bool)
    for sg in (4, 9, 20, 60):
        num = cv2.GaussianBlur(img.astype(np.float32) * w[..., None], (0, 0), sg)
        den = cv2.GaussianBlur(w, (0, 0), sg)
        ok = (den > (0.2 if sg < 60 else 1e-4)) & ~done
        est[ok] = (num[ok] / den[ok][..., None])
        done |= ok
    target = (disc > 0) & (capMask > 0)
    img[target] = np.clip(est[target], 0, 255).astype(np.uint8)
mask[capMask > 0] = 0

clean = smooth_fill(img, mask, 6)

def fill_rows(image, x0, x1, y0, y1, seed, only_marks=False):
    """Plain panel areas: each row blends from its left edge to its right edge."""
    rng = np.random.default_rng(seed)
    # sample just inside the edges (text never reaches them), in a light vertical blur
    src_ = cv2.GaussianBlur(image, (0, 0), 2).astype(np.float32)
    a = np.median(src_[y0:y1, x0:x0 + 5], axis=1)
    b = np.median(src_[y0:y1, x1 - 5:x1], axis=1)
    t = np.linspace(0.0, 1.0, x1 - x0)[None, :, None]
    rows = a[:, None, :] * (1 - t) + b[:, None, :] * t
    rows = cv2.GaussianBlur(rows, (0, 0), 1.5) + rng.normal(0, 1.3, rows.shape)
    if not only_marks:
        image[y0:y1, x0:x1] = np.clip(rows, 0, 255).astype(np.uint8)
        return
    # Only where there's lettering, feathered, so no rectangle shows.
    g_ = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY).astype(np.int16)
    loc = cv2.GaussianBlur(g_.astype(np.float32), (0, 0), 9)
    m = ((g_ < loc - 14) | (g_ > loc + 14))[y0:y1, x0:x1].astype(np.uint8) * 255
    m = cv2.dilate(m, np.ones((11, 11), np.uint8))
    alpha = cv2.GaussianBlur(m.astype(np.float32) / 255.0, (0, 0), 3)[..., None]
    region = image[y0:y1, x0:x1].astype(np.float32)
    image[y0:y1, x0:x1] = np.clip(region * (1 - alpha) + rows * alpha, 0, 255).astype(np.uint8)

fill_rows(clean, 128, 512, 392, 490, 1)     # brand title
for i, (lx0, lx1, ly0, ly1) in enumerate(labelRects):
    fill_rows(clean, lx0, lx1, ly0, ly1, 10 + i, True)   # knob captions
fill_rows(clean, 60, 262, 1352, 1405, 20, True)   # "Bypass/In"
fill_rows(clean, 268, 370, 612, 968, 2)     # LED ladder

# --- Rebuild the lower panel cleanly: a smooth plate, soft knob shadows, then the
#     knobs and the IN key from the picture. Frame, dots, text and LEDs are drawn by the app.
top, bottom, left, right = 474, 1445, 14, 630
keep = np.zeros(gray.shape, np.uint8)
for (cx, cy, r) in knobs:
    cv2.circle(keep, (cx, cy), int(r * 1.02), 255, -1)
cv2.rectangle(keep, (93, 1203), (232, 1342), 255, -1)       # IN key

plate = clean.astype(np.float32)
w = np.ones(gray.shape, np.float32)
w[keep > 0] = 0
w[:top] = 0
w[bottom:] = 0
# leave out anything that isn't plain plate: text, frame, dots, smudges
cg = cv2.cvtColor(clean, cv2.COLOR_BGR2GRAY).astype(np.int16)
cl = cv2.GaussianBlur(cg.astype(np.float32), (0, 0), 15)
w[np.abs(cg - cl) > 6] = 0
w = cv2.erode(w, np.ones((7, 7), np.uint8))
num = cv2.GaussianBlur(plate * w[..., None], (0, 0), 30)
den = cv2.GaussianBlur(w, (0, 0), 30)[..., None]
smooth = num / np.maximum(den, 1e-3)
noise = np.random.default_rng(3).normal(0, 1.2, smooth.shape).astype(np.float32)
smooth = cv2.GaussianBlur(smooth, (0, 0), 4) + noise

# Soft shadow under each knob and the key (light from the top left)
shade = np.zeros(gray.shape, np.float32)
for (cx, cy, r) in knobs:
    cv2.circle(shade, (cx + 5, cy + 9), int(r * 1.0), 1.0, -1)
cv2.rectangle(shade, (99, 1213), (238, 1350), 1.0, -1)
shade = cv2.GaussianBlur(shade, (0, 0), 9) * 0.38
smooth = smooth * (1.0 - shade[..., None])

region = np.zeros(gray.shape, np.float32)
region[top:bottom, left:right] = 1.0
region = cv2.GaussianBlur(region, (0, 0), 3)
keepSoft = cv2.GaussianBlur((keep > 0).astype(np.float32), (0, 0), 1.2)
alpha = (region * (1.0 - keepSoft))[..., None]
clean = np.clip(clean.astype(np.float32) * (1 - alpha) + smooth * alpha, 0, 255).astype(np.uint8)

# Trim to the panel itself (the mockup's neighbours sit at the edges).
x0, y0, x1, y1 = 4, 6, 640, 1490
clean = clean[y0:y1, x0:x1]
cv2.imwrite(out_path, clean)

preview = cv2.resize(clean, (clean.shape[1] // 2, clean.shape[0] // 2), interpolation=cv2.INTER_AREA)
cv2.imwrite(preview_path, preview)
print("panel", clean.shape[1], "x", clean.shape[0], "offset", x0, y0)
