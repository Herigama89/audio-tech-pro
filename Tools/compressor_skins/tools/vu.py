# vu.py: a VU meter's scale drawn clean over a photographed meter's face (the photo's own scale was crossed by its
# needle): the face's lamp-lit colour kept from the photo, the arc, ticks and numbers drawn at the angles the app
# turns its needle to, so needle and scale agree.
import numpy as np, cv2, math
from PIL import Image, ImageDraw, ImageFont
from face import Face, FONTS

MARKS = [(-20, 0.0), (-10, 0.25), (-7, 0.39), (-5, 0.52), (-3, 0.69), (-2, 0.78), (-1, 0.88), (0, 1.0)]

def angle_of(vu, at20, at0, at3):
    if vu >= 0: return at0 + vu / 3.0 * (at3 - at0)
    for i in range(1, len(MARKS)):
        if vu <= MARKS[i][0]:
            a, fa = MARKS[i - 1]; b, fb = MARKS[i]
            f = fa + (vu - a) / (b - a) * (fb - fa)
            return at20 + f * (at0 - at20)
    return at20

def _draw_vu(self, window, pivot, r, angles, ink, red, face_mask=None, labels='vu', size=1.0, minor=True,
             title=None, vu_left=None, vu_right=None, percent=True, red_width=None, arc_width=None, font='sans', extra=(), numbers=None, mask_below=None):
    """Draws the scale (in the photo's pixels: window the meter's face, pivot the needle's, r the arc's radius)."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in window]
    px, py, r = self.S(pivot[0]), self.S(pivot[1]), self.S(r)
    at20, at0, at3 = angles
    W, H = x1 - x0, y1 - y0
    face = self.bg[y0:y1, x0:x1].copy()
    lum = face.mean(2)
    # which pixels are the face (not its bezel): bright, filled in
    if isinstance(face_mask, str) and face_mask == 'rect':
        fm = np.ones((H, W), np.uint8)
    elif isinstance(face_mask, tuple):
        # (a round face: centre and radius, in the photo's pixels)
        fm = np.zeros((H, W), np.uint8)
        cv2.circle(fm, (int(round(self.S(face_mask[0]) - x0)), int(round(self.S(face_mask[1]) - y0))), int(round(self.S(face_mask[2]))), 1, -1)
    elif face_mask == 'cream' or face_mask is None:
        sat = face.max(2) - face.min(2)
        fm = ((lum > 120) & (face[..., 1] > face[..., 0] * 0.72)).astype(np.uint8) if face_mask == 'cream' else (lum > np.percentile(lum, 35)).astype(np.uint8)
        fm = cv2.morphologyEx(fm, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
        n, lab, st, _ = cv2.connectedComponentsWithStats(fm)
        fm = (lab == 1 + np.argmax(st[1:, 4])).astype(np.uint8)
        # (the face's whole outline - its print inside it: the hull of its pale part)
        pts = cv2.findNonZero(fm)
        hull = cv2.convexHull(pts)
        fm = np.zeros_like(fm); cv2.fillConvexPoly(fm, hull, 1)
        n, lab, st, _ = cv2.connectedComponentsWithStats(fm)
        fm = (lab == 1 + np.argmax(st[1:, 4])).astype(np.uint8)
        ff = fm.copy(); m2 = np.zeros((H + 2, W + 2), np.uint8); cv2.floodFill(ff, m2, (0, 0), 1); fm = fm | (1 - ff)
    else:
        fm = face_mask
    if face_mask in (None, 'cream'): fm = cv2.erode(fm, np.ones((5, 5), np.uint8))
    if mask_below is not None:
        fm[max(0, int(round(self.S(mask_below) - y0))):] = 0   # (the face stops at the meter's dark lower band)
    # the face without its print: the lamp's glow, from the face's own light (its brightest, smoothed)
    keep = fm.astype(np.float32)
    clean = face.copy()
    for c in range(3):
        ch = clean[..., c].astype(np.uint8)
        ch = cv2.dilate(ch, np.ones((13, 13), np.uint8))        # (print is darker: taken by its lighter surround)
        ch = cv2.erode(ch, np.ones((7, 7), np.uint8))           # (not brighter than the face itself)
        clean[..., c] = cv2.GaussianBlur(ch.astype(np.float32), (0, 0), 10)
    num = cv2.GaussianBlur(clean * keep[..., None], (0, 0), 6); den = cv2.GaussianBlur(keep, (0, 0), 6)[..., None]
    clean = num / np.maximum(den, 1e-4)
    clean = clean * 0.985 + self.rng.normal(0, 0.9, clean.shape)
    # the scale, drawn four times as fine and brought down
    S = 4
    layer = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy = (px - x0) * S, (py - y0) * S
    R = r * S
    def at(angle, rad):
        t = math.radians(angle)
        return cx + rad * math.sin(t), cy - rad * math.cos(t)
    def arc(a0, a1, rad, width, colour):
        # (an arc as a polygon: PIL's arc is thin and blotchy)
        pts_o = [at(a0 + (a1 - a0) * k / 60, rad + width / 2) for k in range(61)]
        pts_i = [at(a1 - (a1 - a0) * k / 60, rad - width / 2) for k in range(61)]
        d.polygon(pts_o + pts_i, fill=colour)
    def line(a, r0, r1, width, colour):
        p0, p1 = at(a, r0), at(a, r1)
        d.line([p0, p1], fill=colour, width=int(round(width)))
    def text(s, a, rad, sz, colour, fnt=font):
        f = ImageFont.truetype(FONTS[fnt], int(round(sz * S)))
        x, y = at(a, rad)
        bb = d.textbbox((0, 0), s, font=f)
        d.text((x - (bb[0] + bb[2]) / 2, y - (bb[1] + bb[3]) / 2), s, font=f, fill=colour)
    def textxy(s, x, y, sz, colour, fnt=font):
        f = ImageFont.truetype(FONTS[fnt], int(round(sz * S)))
        bb = d.textbbox((0, 0), s, font=f)
        d.text(((x - x0) * S - (bb[0] + bb[2]) / 2, (y - y0) * S - (bb[1] + bb[3]) / 2), s, font=f, fill=colour)
    ink = tuple(ink) + (255,); red = tuple(red) + (255,)
    k = size * S
    aw = (arc_width or 1.3) * k
    rw = (red_width or 4.5) * k
    a20, a0_, a3 = angle_of(-20, *angles), angle_of(0, *angles), angle_of(3, *angles)
    arc(a20, a0_, R, aw, ink)
    arc(a0_, a3, R + rw / 2 - aw / 2, rw, red)
    major = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3]
    for v in major:
        a = angle_of(v, *angles)
        col = red if v > 0 else ink
        line(a, R - aw / 2, R + 9 * k, 1.5 * k, col)
    if minor:
        for v in [-15, -8, -6, -4, -2.5, -1.5, -0.5, 0.5, 1.5, 2.5]:
            a = angle_of(v, *angles)
            line(a, R - aw / 2, R + 5 * k, 1.1 * k, red if v > 0 else ink)
    for v in (numbers if numbers is not None else major):
        a = angle_of(v, *angles)
        s = str(abs(v)) if labels == 'plain' else ('+' + str(v) if v > 0 else str(v))
        text(s, a, R + 17 * k, 9.5 * size, red if v > 0 else ink)
    if percent:
        # the percentage scale under the arc, dots at its marks
        for p, v in [(0, -40), (20, -14), (40, -8), (60, -4.4), (80, -1.9), (100, 0)]:
            if v < -20: v = -20.0 - 0.0
            a = angle_of(max(v, -20), *angles)
            d.ellipse([*(np.array(at(a, R - 5 * k)) - 1.4 * k), *(np.array(at(a, R - 5 * k)) + 1.4 * k)], fill=ink)
            text(str(p), a, R - 13 * k, 6.5 * size, ink)
    if title:
        textxy(title[0], title[1], title[2], title[3] * size, ink)
    if vu_left:
        textxy('VU', vu_left[0], vu_left[1], vu_left[2] * size, ink)
    if vu_right:
        textxy('VU', vu_right[0], vu_right[1], vu_right[2] * size, red if vu_right[3] else ink)
    for (s_, x, y, sz, isred) in extra:
        textxy(s_, x, y, sz * size, red if isred else ink)
    layer = layer.resize((W, H), Image.LANCZOS)
    L = np.array(layer).astype(np.float32)
    a = L[..., 3:4] / 255.0
    out = clean * (1 - a) + L[..., :3] * a
    # (the face's own glass: a touch of the photo's light and grain over it)
    m = cv2.GaussianBlur(fm.astype(np.float32), (0, 0), 1.0)[..., None]
    if face_mask == 'rect': m = np.ones_like(m)
    self.bg[y0:y1, x0:x1] = face * (1 - m) + out * m

Face.draw_vu = _draw_vu

def _draw_gr(self, window, pivot, r, a0, a20, ink=(235, 235, 235), size=1.0, caption=True, face=(14, 14, 16)):
    """A gain reduction meter's black face drawn clean: 0 to 20 dB COMPRESSION, its marks at the angles the app's
    needle uses (a0 at 0 dB, a20 at 20)."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in window]
    px, py, r = self.S(pivot[0]), self.S(pivot[1]), self.S(r)
    W, H = x1 - x0, y1 - y0
    S = 4
    lum = self.bg[y0:y1, x0:x1].mean(2)
    base = np.median(self.bg[y0:y1, x0:x1][lum < np.percentile(lum, 50)], axis=0)
    layer = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    cx, cy, R = (px - x0) * S, (py - y0) * S, r * S
    def at(angle, rad):
        t = math.radians(angle); return cx + rad * math.sin(t), cy - rad * math.cos(t)
    k = size * S
    ink = tuple(ink) + (255,)
    for v in range(0, 21, 2):
        a = a0 + (a20 - a0) * v / 20.0
        big = v % 4 == 0
        d.line([at(a, R), at(a, R + (8 if big else 4.5) * k)], fill=ink, width=int(round((1.6 if big else 1.0) * k)))
        if big:
            f = ImageFont.truetype(FONTS['sans'], int(round(8.5 * k)))
            x, y = at(a, R + 15 * k); bb = d.textbbox((0, 0), str(v), font=f)
            d.text((x - (bb[0] + bb[2]) / 2, y - (bb[1] + bb[3]) / 2), str(v), font=f, fill=ink)
    if caption:
        for s_, dy, sz in [('dB', -4, 7), ('COMPRESSION', 6, 6.5)]:
            f = ImageFont.truetype(FONTS['sans'], int(round(sz * k)))
            bb = d.textbbox((0, 0), s_, font=f)
            d.text((cx - (bb[0] + bb[2]) / 2, cy - R * 0.55 + dy * k - (bb[1] + bb[3]) / 2), s_, font=f, fill=ink)
    layer = layer.resize((W, H), Image.LANCZOS)
    L = np.array(layer).astype(np.float32); a = L[..., 3:4] / 255.0
    plain = np.broadcast_to(base, (H, W, 3)).astype(np.float32) + self.rng.normal(0, 1.2, (H, W, 3))
    # (the face's own light: a soft glow towards its top, as in the photo)
    glow = cv2.GaussianBlur(np.clip(lum - np.percentile(lum, 50), 0, 60).astype(np.float32), (0, 0), 12)[..., None] * 0.0
    out = (plain + glow) * (1 - a) + L[..., :3] * a
    m = np.ones((H, W), np.float32)
    m = np.clip(cv2.GaussianBlur(np.pad(m, 1), (0, 0), 1.0)[1:-1, 1:-1] * 1.3, 0, 1)[..., None]
    self.bg[y0:y1, x0:x1] = self.bg[y0:y1, x0:x1] * (1 - m) + out * m

Face.draw_gr = _draw_gr
