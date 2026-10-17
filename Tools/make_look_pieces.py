#!/usr/bin/env python3
"""
Cuts the Themes > Faders / Knobs / Buttons / Bus Comp meter pieces out of the reference
photos (only the pieces: no maker's names or logos), into Resources/skin/looks.

    python3 Tools/make_look_pieces.py <folder with the photos>

The photos (as sent, by size): faders.png (1581x585, the 16-fader unit), meter.png (1080x1080,
the black compression meter), rack.webp (2000x368, the stereo compressor), interface.webp
(750x618), preamp.png (1632x809, the blue preamp), console.png (1351x888, the digital console).
"""
import sys, os, math
import numpy as np, cv2
from PIL import Image

src = sys.argv[1]
out = os.path.join(os.path.dirname(__file__), '..', 'Resources', 'skin', 'looks')
os.makedirs(out, exist_ok=True)
P = lambda n: os.path.join(src, n)
O = lambda n: os.path.join(out, n)

def lum(c): return c @ np.array([0.299, 0.587, 0.114])

def cap(path, box, thresh, light=False, gap=1):
    """A fader cap: for each row, the run of cap through the middle column (smoothed)."""
    c = np.array(Image.open(path).convert('RGB')).astype(np.float32)[box[1]:box[3], box[0]:box[2]]
    L = lum(c); h, w = L.shape; cx = w // 2
    ext = []
    for y in range(h):
        on = (L[y] > thresh) if light else (L[y] < thresh)
        if not on[cx-3:cx+4].any(): ext.append(None); continue
        l = r = cx
        while l > 0 and (on[l-1] or on[max(0, l-1-gap):l-1].any()): l -= 1
        while r < w-1 and (on[r+1] or on[r+2:min(w, r+2+gap)].any()): r += 1
        ext.append((l, r))
    widths = [e[1]-e[0] if e else 0 for e in ext]; mx = max(widths)
    rows = [y for y, wd in enumerate(widths) if wd > mx*0.6]; top, bottom = min(rows), max(rows)
    for y in range(top, bottom+1):
        if ext[y] is None or widths[y] < mx*0.6: ext[y] = ext[y-1] if y > top and ext[y-1] else ext[y+1]
    ls = np.array([ext[y][0] for y in range(top, bottom+1)]); rs = np.array([ext[y][1] for y in range(top, bottom+1)])
    ls = np.maximum(ls, np.median(ls)-4); rs = np.minimum(rs, np.median(rs)+4)
    k = 5
    ls = [np.median(ls[max(0,i-k):i+k+1]) for i in range(len(ls))]; rs = [np.median(rs[max(0,i-k):i+k+1]) for i in range(len(rs))]
    m = np.zeros((h, w), np.float32)
    for i, y in enumerate(range(top, bottom+1)): m[y, int(ls[i]):int(rs[i])+1] = 1
    m = cv2.GaussianBlur(m, (3,3), 0.6)
    a = np.dstack([c, m*255])[top:bottom+1, int(min(ls)):int(max(rs))+1]
    return Image.fromarray(a.clip(0,255).astype(np.uint8), 'RGBA')

def disc(path, cx, cy, r, pointer_deg=0.0, size=None, edge=1.5):
    """A round piece (knob, lamp): the disc round (cx, cy), turned so its pointer points up."""
    im = Image.open(path).convert('RGBA'); pad = int(r*1.5)
    c = im.crop((int(cx-pad), int(cy-pad), int(cx+pad), int(cy+pad)))
    c = c.transform(c.size, Image.AFFINE, (1,0,cx-int(cx),0,1,cy-int(cy)), resample=Image.BICUBIC)
    c = c.rotate(pointer_deg, resample=Image.BICUBIC, center=(pad, pad))
    a = np.array(c).astype(np.float32); yy, xx = np.mgrid[0:a.shape[0], 0:a.shape[1]]
    a[...,3] = np.clip((r - np.sqrt((xx-pad)**2 + (yy-pad)**2)) / edge + 0.5, 0, 1) * 255
    R = int(math.ceil(r)) + 1
    img = Image.fromarray(a[pad-R:pad+R, pad-R:pad+R].clip(0,255).astype(np.uint8), 'RGBA')
    return img.resize((size, size), Image.LANCZOS) if size else img

# Faders
cap(P('faders.png'), (118,318,178,420), 75).save(O('look_cap_black.png'))
cap(P('faders.png'), (1434,272,1490,372), 75).save(O('look_cap_black_red.png'))
cap(P('console.png'), (212,684,262,760), 120, light=True, gap=0).save(O('look_cap_silver.png'))

# Knobs (pointer up)
disc(P('interface.webp'), 453.5, 373.5, 59.5, 0).save(O('look_knob_black.png'))
disc(P('rack.webp'), 175.5, 94, 44, -12).save(O('look_knob_blackyellow.png'))
disc(P('preamp.png'), 253, 210, 43, 6).save(O('look_knob_yellow.png'))
disc(P('interface.webp'), 151.75, 361.75, 17.5, 152, size=72).save(O('look_knob_red.png'))

# Buttons
Image.open(P('faders.png')).convert('RGBA').crop((126,55,178,107)).save(O('look_btn_square_white.png'))
Image.open(P('rack.webp')).convert('RGBA').crop((566,51,591,76)).save(O('look_btn_square_grey.png'))
disc(P('preamp.png'), 335, 415, 27).save(O('look_btn_round.png'))

# Meters: the needles (and a logo) painted out; the app draws its own needle.
im = cv2.cvtColor(np.array(Image.open(P('meter.png')).convert('RGB')), cv2.COLOR_RGB2BGR)
mask = np.zeros(im.shape[:2], np.uint8)
cv2.line(mask, (368,416), (480,562), 255, 8)
res = cv2.inpaint(im, mask, 5, cv2.INPAINT_TELEA)
col = tuple(int(v) for v in np.percentile(im[395:415,428:445].reshape(-1,3), 95, axis=0))
cv2.line(res, (377,416), (394,440), col, 4, cv2.LINE_AA)       # the 0 mark, beside the needle
cv2.imwrite(O('look_vu_black.png'), res[292:600, 262:812])

im2 = cv2.cvtColor(np.array(Image.open(P('preamp.png')).convert('RGB')), cv2.COLOR_RGB2BGR)
m2 = np.zeros(im2.shape[:2], np.uint8)
cv2.polylines(m2, [np.array([(768,254),(778,263),(787,274),(793,283),(810,300),(830,320),(845,337),(852,342)], np.int32)], False, 255, 7)
cv2.line(m2, (769,255), (785,268), 255, 9)
cv2.circle(m2, (793,281), 6, 255, -1)
for dx in (0, 6, 12):
    cv2.polylines(m2, [np.array([(772+dx,276),(788+dx,298),(808+dx,320),(828+dx,345)], np.int32)], False, 255, 7)
cv2.rectangle(m2, (728,298), (768,328), 255, -1)
res2 = cv2.inpaint(im2, m2, 6, cv2.INPAINT_TELEA)
cv2.line(res2, (765,274), (786,266), (38,40,42), 2, cv2.LINE_AA)
cv2.imwrite(O('look_vu_cream.png'), res2[117:352, 700:1120])
print('done:', sorted(os.listdir(out)))
