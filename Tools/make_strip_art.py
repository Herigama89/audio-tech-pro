"""
Builds the mixer strip artwork from the design mockup (the first strip,
"Lead Vos"): the top of the strip with its EQ and Dynamics knobs, a plain
metal plate for the rest of the strip, and the fader cap.

Everything that moves or says something (pointers, lettering, numbers, the
values in the little boxes) is painted out; the app draws it.

Usage: python3 make_strip_art.py <mockup.jpg> <output folder>
"""
import sys, os
import cv2
import numpy as np
from PIL import Image, ImageFilter

src, out = sys.argv[1], sys.argv[2]
S = 3
ORIGIN = (298, 185)          # where the crop of the strips starts in the mockup
SIZE = (202, 505)

im = Image.open(src).convert("RGB")
crop = im.crop((ORIGIN[0], ORIGIN[1], ORIGIN[0] + SIZE[0], ORIGIN[1] + SIZE[1]))
big = crop.resize((SIZE[0] * S, SIZE[1] * S), Image.LANCZOS).filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2))
img = cv2.cvtColor(np.array(big), cv2.COLOR_RGB2BGR)

# The first strip, in the enlarged crop
X0, X1, Y0 = 6, 201, 13
TOP_END = 900                     # through the Dynamics section
gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
yy, xx = np.mgrid[0:gray.shape[0], 0:gray.shape[1]]
local = cv2.GaussianBlur(gray, (0, 0), 9).astype(np.int16)
marks = gray.astype(np.int16) < local - 18
light = gray.astype(np.int16) > local + 14

knobs = [(85, 159, 41), (82, 289, 41), (83, 404, 41), (82, 542, 41), (104, 775, 43)]   # x, y, skirt radius
pointerAngles = [0, 0, 0, 0, 0]

def small(m, max_area=700):
    n, labels, stats, _ = cv2.connectedComponentsWithStats(m.astype(np.uint8), 8)
    keep = np.zeros(m.shape, bool)
    for i in range(1, n):
        if stats[i, cv2.CC_STAT_AREA] <= max_area:
            keep |= labels == i
    return keep

mask = np.zeros(gray.shape, np.uint8)
strip = (xx >= X0 + 4) & (xx < X1 - 4) & (yy >= Y0 + 4) & (yy < TOP_END)

# Lettering and dots around the EQ knobs (everything small and dark/light on the plate)
eqZone = strip & (yy < 610)
for (cx, cy, r) in knobs:
    eqZone &= np.hypot(xx - cx, yy - cy) > r + 1
redButtons = [(168, 63, 18), (168, 470, 18)]
for (bx, by, br) in redButtons:
    eqZone &= ~((np.abs(xx - bx) < br) & (np.abs(yy - by) < br))
mask[small(eqZone & marks)] = 255
mask[small(eqZone & light, 250)] = 255

# Values in the small boxes (the boxes stay; the app writes in them)
for (bx0, by0, bx1, by1) in [(44, 628, 92, 654), (112, 628, 160, 654), (36, 836, 96, 868), (120, 836, 172, 868)]:
    m = np.zeros(gray.shape, bool)
    m[by0:by1, bx0:bx1] = True
    mask[(m & (marks | light))] = 255

# Knob pointers
for (cx, cy, r) in knobs:
    d = np.hypot(xx - cx, yy - cy)
    cap = d < r * 0.78
    level = np.median(gray[cap])
    mask[cap & (np.abs(gray.astype(np.int16) - int(level)) > 16)] = 255
    cv2.line(mask, (cx, cy), (cx, int(cy - r * 0.78)), 255, 20)
    cv2.circle(mask, (cx, cy), 8, 255, -1)

mask = cv2.dilate(mask, np.ones((5, 5), np.uint8))

# Fill knob caps from the cap's own shading, the plate from the plate.
def fill(image, m, region, sigmas):
    w = (region & (m == 0)).astype(np.float32)
    est = np.zeros(image.shape, np.float32)
    done = np.zeros(m.shape, bool)
    for sg in sigmas:
        num = cv2.GaussianBlur(image.astype(np.float32) * w[..., None], (0, 0), sg)
        den = cv2.GaussianBlur(w, (0, 0), sg)
        ok = (den > (0.15 if sg != sigmas[-1] else 1e-5)) & ~done
        est[ok] = num[ok] / den[ok][..., None]
        done |= ok
    target = region & (m > 0)
    noise = np.random.default_rng(5).normal(0, 1.2, image.shape)
    image[target] = np.clip(est[target] + noise[target], 0, 255).astype(np.uint8)

for (cx, cy, r) in knobs:
    disc = np.hypot(xx - cx, yy - cy) < r * 0.8
    fill(img, mask, disc, (3, 8, 20, 60))
    mask[disc] = 0

fill(img, mask, strip, (4, 10, 30, 90))

# --- Rebuild the plate cleanly around the parts we keep ------------------------
# The parts: knobs, the red keys, the two cream keys and the two dark displays.
creamKeys = [(38, 623, 99, 663), (107, 623, 167, 663)]
darkBoxes = [(30, 829, 101, 873), (113, 829, 179, 873)]
keep = np.zeros(gray.shape, np.uint8)
for (cx, cy, r) in knobs:
    cv2.circle(keep, (cx, cy), int(r * 1.03), 255, -1)
for (bx, by, br) in redButtons:
    cv2.rectangle(keep, (bx - 17, by - 17), (bx + 17, by + 17), 255, -1)
for (x0_, y0_, x1_, y1_) in creamKeys + darkBoxes:
    cv2.rectangle(keep, (x0_, y0_), (x1_, y1_), 255, -1)

g2 = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
dev = np.abs(g2 - cv2.GaussianBlur(g2, (0, 0), 6))
# The plate's colour, column by column, from rows with nothing on them
# (above the first knob and between the EQ and Dynamics sections).
rows = list(range(Y0 + 4, 100)) + list(range(575, 606))
profile = np.zeros((img.shape[1], 3), np.float32)
for x in range(X0, X1):
    samples = [img[y, x].astype(np.float32) for y in rows if keep[y, x] == 0 and dev[y, x] < 7]
    profile[x] = np.median(np.array(samples), axis=0) if samples else img[rows[0], x]
profile[X0:X1] = cv2.GaussianBlur(profile[None, X0:X1], (0, 0), 2.0)[0]
plateImg = np.repeat(profile[None, :, :], img.shape[0], axis=0)
grain = cv2.GaussianBlur(np.random.default_rng(4).normal(0, 1.4, plateImg.shape).astype(np.float32), (0, 0), 0.7) * 1.5
plateImg = plateImg + grain

shade = np.zeros(gray.shape, np.float32)
for (cx, cy, r) in knobs:
    cv2.circle(shade, (cx + 3, cy + 6), int(r * 1.0), 1.0, -1)
for (x0_, y0_, x1_, y1_) in creamKeys + darkBoxes:
    cv2.rectangle(shade, (x0_ + 2, y0_ + 4), (x1_ + 2, y1_ + 4), 0.6, -1)
shade = cv2.GaussianBlur(shade, (0, 0), 6) * 0.32
plateImg = plateImg * (1.0 - shade[..., None])

region = cv2.GaussianBlur(strip.astype(np.float32), (0, 0), 1.0)
keepSoft = cv2.GaussianBlur((keep > 0).astype(np.float32), (0, 0), 1.0)
alpha = (region * (1.0 - keepSoft))[..., None]
img = np.clip(img.astype(np.float32) * (1 - alpha) + plateImg * alpha, 0, 255).astype(np.uint8)

# Empty the keys and displays (keep their bevels)
for (x0_, y0_, x1_, y1_) in creamKeys + darkBoxes:
    ix0, iy0, ix1, iy1 = x0_ + 7, y0_ + 7, x1_ - 7, y1_ - 7
    inner = img[iy0:iy1, ix0:ix1].astype(np.float32)
    top_ = np.median(inner[:3].reshape(-1, 3), axis=0)
    bot_ = np.median(inner[-3:].reshape(-1, 3), axis=0)
    t = np.linspace(0, 1, iy1 - iy0)[:, None, None]
    grad = top_[None, None, :] * (1 - t) + bot_[None, None, :] * t
    grad = np.repeat(grad, ix1 - ix0, axis=1) + np.random.default_rng(1).normal(0, 1.0, (iy1 - iy0, ix1 - ix0, 3))
    img[iy0:iy1, ix0:ix1] = np.clip(grad, 0, 255).astype(np.uint8)

top = img[Y0:TOP_END, X0:X1]
cv2.imwrite(os.path.join(out, "StripTop.png"), top)

# A plain plate for the rest of the strip: the rebuilt plate's column profile.
profile = profile[X0:X1]
H = 600
plate = np.repeat(profile[None, :, :], H, axis=0)
grain = np.random.default_rng(9).normal(0, 1.3, plate.shape).astype(np.float32)
grain = cv2.GaussianBlur(grain, (0, 0), 0.7) * 1.6
plate = np.clip(plate + grain, 0, 255).astype(np.uint8)
cv2.imwrite(os.path.join(out, "StripPlate.png"), plate)

# The fader cap
cap = img[1021:1128, 43:98]
cv2.imwrite(os.path.join(out, "FaderCap.png"), cap)

print("top", top.shape[1], "x", top.shape[0])
print("knobs (in StripTop pixels):", [(x - X0, y - Y0, r) for (x, y, r) in knobs])
print("red buttons:", [(x - X0, y - Y0) for (x, y, _) in redButtons])
print("cream keys:", [(a - X0, b - Y0, c - X0, d - Y0) for (a, b, c, d) in creamKeys])
print("dark boxes:", [(a - X0, b - Y0, c - X0, d - Y0) for (a, b, c, d) in darkBoxes])
