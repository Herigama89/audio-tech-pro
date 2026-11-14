#!/usr/bin/env python3
"""
The Mastering Compressors' faces in neon Liquid Glass, in three skins:
  0 Neon Glass         - dark frosted glass, a neon rim, the unit's colour glowing through
  1 Neon Glass + Panel - the same, the brushed plate (neon_src/plate.png, the user's) set inside, tinted to the unit
  2 Neon Clear         - clear glass: the face is see-through (the app blurs what's behind it)

What doesn't move is drawn here: the glass, the plate, the knobs' bodies (the user's silver knob, neon_src/silver_knob.png,
its skirt painted per section), their printed scales and faint value tracks, the meter windows with their faces
(the app's VU picture, Resources/skin/looks/look_vu_cream.png; the Iron's arc meter, neon_src/iron_arc.png), the labels.
What moves is drawn by the app: the knobs' pointers and glowing value rings, the chicken heads, the switches, the lights,
the needles.

    python3 neon_faces.py --build     -> Resources/effects/mn_<key>_<skin>.(jpg|png), mn_lamp_<rgb>.png,
                                         Source/ui/MasteringLayouts.h
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.normpath (os.path.join (HERE, "..", ".."))
OUT = os.path.join (ROOT, "Resources", "effects")
SRC = os.path.join (HERE, "neon_src")

BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf"
def F (s): return ImageFont.truetype (BOLD, int (s))

# ---------------------------------------------------------------------------------------------------------------
# The knob: the user's silver knob, centred on its axis; only the skirt (outside the silver cap) is painted
KNOB = np.array (Image.open (os.path.join (SRC, "silver_knob.png")).convert ("RGBA")).astype (np.float32)
KCX, KCY, KR = 86.57, 86.86, 88.0
KS = 352
def _centred():
    import cv2
    sc = KS / (2 * KR)
    M = np.float32 ([[sc, 0, KS / 2 - KCX * sc], [0, sc, KS / 2 - KCY * sc]])
    return cv2.warpAffine (KNOB, M, (KS, KS), flags = cv2.INTER_LANCZOS4, borderMode = cv2.BORDER_CONSTANT, borderValue = (0, 0, 0, 0))
KBODY = _centred()
_yy, _xx = np.mgrid[0:KS, 0:KS]
KRR = np.hypot (_xx - KS / 2 + .5, _yy - KS / 2 + .5) / (KS / 2)
CAP_R = 58.6 / 88.0
SKIRTS = { "black": None, "red": (176, 34, 28), "cream": (232, 220, 186), "grey": (150, 152, 158), "amber": (224, 150, 58), "green": (46, 140, 70) }
_kcache = {}
def knob_body (colour, size):
    key = (colour, size)
    if key in _kcache: return _kcache[key]
    a = KBODY.copy()
    col = SKIRTS[colour]
    if col is not None:
        rgb = a[..., :3] / 255; lum = rgb.mean (-1)
        skirt = np.clip ((KRR - CAP_R) * KS / 2 / 1.2, 0, 1)
        base = np.median (lum[(KRR > 0.72) & (KRR < 0.95)])
        shade = np.clip (lum / max (base, 1e-3), 0.25, 2.6)
        tint = np.array (col, np.float32) / 255 * (0.58 * shade)[..., None]
        spec = np.clip (lum - base * 1.6, 0, 1)[..., None] * 0.7
        rgb2 = rgb * (1 - skirt[..., None]) + np.clip (tint + spec, 0, 1) * skirt[..., None]
        a[..., :3] = rgb2 * 255
    im = Image.fromarray (a.clip (0, 255).astype ("uint8"), "RGBA").resize ((size, size), Image.LANCZOS)
    _kcache[key] = im
    return im

# ---------------------------------------------------------------------------------------------------------------
VU = Image.open (os.path.join (ROOT, "Resources", "skin", "looks", "look_vu_cream.png")).convert ("RGB")
VU_PIVOT, VU_LEN = (200.0, 322.6), 212.0
ARC = Image.open (os.path.join (SRC, "iron_arc.png")).convert ("RGB")
ARC_PIVOT, ARC_R0, ARC_R1, ARC_A0, ARC_A1 = (220.0, 370.0), 262.0, 382.0, -33.0, 33.0

def amberise (im):
    a = np.array (im).astype (float) / 255; r, g, b = a[..., 0], a[..., 1], a[..., 2]
    lum = 0.3 * r + 0.59 * g + 0.11 * b; red = np.clip ((r - np.maximum (g, b)) * 3.0, 0, 1)
    H, W = lum.shape; yy, xx = np.mgrid[0:H, 0:W]
    glow = np.exp (-(((xx - W / 2) / (W * 0.38)) ** 2 + ((yy - H * 1.1) / (H * 0.55)) ** 2))
    amber = np.stack ([np.ones_like (glow), 0.70 + 0.08 * glow, 0.36 + 0.06 * glow], -1)
    t = amber * lum[..., None] * 1.12 + np.array ([0.10, 0.04, 0.0]) * glow[..., None] * np.clip ((lum - 0.35) / 0.5, 0, 1)[..., None]
    return Image.fromarray ((np.clip (t * (1 - red[..., None]) + a * red[..., None], 0, 1) * 255).astype ("uint8"))

# ---------------------------------------------------------------------------------------------------------------
class Face:
    """One unit being drawn: its picture (per skin) and its record (what the app draws)."""
    def __init__ (self, key, W, H, neon, neon2, plate):
        self.key, self.W, self.H, self.neon, self.neon2, self.plate = key, W, H, neon, neon2, plate
        self.ops = []                              # drawing steps, replayed on each skin's base
        self.items, self.lights, self.meters, self.spots = [], [], [], []

    # -- the base of each skin
    def base (self, skin):
        W, H, m = self.W, self.H, 24
        if skin == 2:
            img = Image.new ("RGBA", (W, H), (0, 0, 0, 0))
            slab = Image.new ("RGBA", (W, H), (0, 0, 0, 0))
            ImageDraw.Draw (slab).rounded_rectangle ([m, m, W - m, H - m], 34, fill = (10, 12, 16, 118))
            img.alpha_composite (slab)
            tint = Image.new ("RGBA", (W, H), (0, 0, 0, 0))
            ImageDraw.Draw (tint).rounded_rectangle ([m, m, W - m, H - m], 34, fill = self.neon + (18,))
            img.alpha_composite (tint)
        else:
            img = Image.new ("RGBA", (W, H), (8, 9, 12, 255))
            bl = Image.new ("RGBA", (W, H), (0, 0, 0, 0)); d = ImageDraw.Draw (bl)
            d.ellipse ([-W * 0.1, -H * 0.3, W * 0.45, H * 0.7], fill = self.neon + (70,))
            d.ellipse ([W * 0.6, H * 0.2, W * 1.1, H * 1.2], fill = self.neon2 + (70,))
            img.alpha_composite (bl.filter (ImageFilter.GaussianBlur (H * 0.25)))
            sl = Image.new ("RGBA", (W, H), (0, 0, 0, 0))
            ImageDraw.Draw (sl).rounded_rectangle ([m, m, W - m, H - m], 34, fill = (22, 24, 30, 215))
            img.alpha_composite (sl)
            if skin == 1:
                import cv2
                p = np.array (Image.open (os.path.join (SRC, "plate.png")).convert ("RGB")).astype (np.float32)[100:670, 105:1270]
                pw, ph = W - 2 * m - 28, H - 2 * m - 28
                p = cv2.resize (p, (pw, ph), interpolation = cv2.INTER_CUBIC)
                p = np.clip (np.array (self.plate, np.float32) * (p.mean (-1, keepdims = True) / 60.0), 0, 255).astype (np.uint8)
                pm = Image.new ("L", (pw, ph), 0); ImageDraw.Draw (pm).rounded_rectangle ([0, 0, pw - 1, ph - 1], 24, fill = 255)
                pi = Image.fromarray (p).convert ("RGBA"); pi.putalpha (pm)
                shp = Image.new ("RGBA", (W, H), (0, 0, 0, 0))
                ImageDraw.Draw (shp).rounded_rectangle ([m + 14, m + 20, W - m - 14, H - m - 8], 24, fill = (0, 0, 0, 190))
                img.alpha_composite (shp.filter (ImageFilter.GaussianBlur (10)))
                img.alpha_composite (pi, (m + 14, m + 14))
        # the glass's sheen, then its neon rim
        sheen = Image.new ("L", (W, H), 0)
        ImageDraw.Draw (sheen).rounded_rectangle ([m, m, W - m, m + H * 0.32], 34, fill = 16 if skin != 2 else 26)
        sheen = sheen.filter (ImageFilter.GaussianBlur (20))
        white = Image.new ("RGBA", (W, H), (255, 255, 255, 255 if skin != 2 else 0))
        if skin == 2:
            s = np.array (img).astype (np.float32); k = np.array (sheen).astype (np.float32)[..., None] / 255
            s[..., :3] = s[..., :3] * (1 - k) + 255 * k; s[..., 3:] = s[..., 3:] + (255 - s[..., 3:]) * k
            img = Image.fromarray (s.clip (0, 255).astype ("uint8"), "RGBA")
        else:
            img = Image.composite (white, img, sheen)
        t = np.linspace (0, 1, W)[None, :, None]
        for blur, width, alpha in ((18, 10, 170), (6, 5, 220), (0, 2, 255)):
            rim = Image.new ("RGBA", (W, H), (0, 0, 0, 0))
            ImageDraw.Draw (rim).rounded_rectangle ([m, m, W - m, H - m], 34, outline = (255, 255, 255, 255), width = width)
            a = np.array (rim).astype (float)
            col = np.array (self.neon)[None, None, :] * (1 - t) + np.array (self.neon2)[None, None, :] * t
            if blur == 0: col = col * 0.5 + 127.5
            a[..., :3] = col; a[..., 3] = a[..., 3] * alpha / 255
            r = Image.fromarray (a.astype ("uint8"), "RGBA")
            if blur: r = r.filter (ImageFilter.GaussianBlur (blur))
            img.alpha_composite (r)
        return img

    # -- drawing helpers (recorded, replayed per skin)
    def text (self, x, y, t, s = 15, anchor = "mm", glow = True, dim = False):
        self.ops.append (("text", x, y, t, s, anchor, glow, dim))
    def _text (self, img, x, y, t, s, anchor, glow, dim):
        if glow:
            g = Image.new ("RGBA", img.size, (0, 0, 0, 0))
            ImageDraw.Draw (g).text ((x, y), t, font = F (s), fill = self.neon + (200,), anchor = anchor)
            img.alpha_composite (g.filter (ImageFilter.GaussianBlur (4)))
        lit = tuple (min (255, int (c * 0.35 + 255 * 0.65)) for c in self.neon)
        if dim: lit = tuple (int (c * 0.8) for c in lit)
        ImageDraw.Draw (img).text ((x, y), t, font = F (s), fill = lit + (255,), anchor = anchor)

    def scale (self, id, c, r, labels, a0, a1, s = 12):
        """Printed settings round a control; each a spot (a click there turns it there)."""
        n = len (labels)
        for i, t in enumerate (labels):
            if not t: continue
            a = math.radians (a0 + (a1 - a0) * i / max (1, n - 1))
            x, y = c[0] + r * math.sin (a), c[1] - r * math.cos (a)
            self.text (x, y, t, s, glow = False, dim = True)
            self.spots.append ((id, x, y, i / max (1, n - 1)))

    def knob (self, id, c, size, colour, label, scale = None, steps = 11):
        """A knob: its body and faint track drawn; its pointer and its glowing ring drawn by the app."""
        self.ops.append (("knob", c, size, colour))
        skirt = SKIRTS[colour] or (0, 0, 0)
        self.items.append ((id, "knob", c[0], c[1], size / 2, [-130.0, 130.0], skirt, self.neon))
        labels = scale if scale is not None else [str (i) if size >= 90 or i % 2 == 0 else "" for i in range (steps)]
        self.scale (id, c, size / 2 + 24, labels, -130, 130, 12 if size > 80 else 11)
        self.text (c[0], c[1] + size / 2 + 34, label, 13 if size > 70 else 12)

    def chicken (self, id, c, size, choices, label, spread = None):
        n = len (choices)
        spread = spread if spread is not None else { 2: 45, 3: 60, 4: 90, 5: 120, 6: 125 }[n]
        angles = [(-spread + 2 * spread * i / (n - 1)) for i in range (n)]
        self.ops.append (("halo", c, size))
        self.items.append ((id, "chicken", c[0], c[1], size / 2, angles, (0, 0, 0), self.neon))
        for i, t in enumerate (choices):
            a = math.radians (angles[i])
            r = size / 2 + 14 + (6 if len (t) > 3 else 0) * abs (math.sin (a))
            x, y = c[0] + r * math.sin (a), c[1] - r * math.cos (a)
            self.text (x, y, t, 11, glow = False, dim = True)
            self.spots.append ((id, x, y, i / (n - 1)))
        self.text (c[0], c[1] + size / 2 + 26, label, 12)

    def toggle (self, id, c, label, size = 40):
        self.ops.append (("ring", c, size))
        self.items.append ((id, "toggle", c[0], c[1], size / 2, [], (0, 0, 0), self.neon))
        self.text (c[0], c[1] + size / 2 + 16, label, 11)

    def light (self, id, value, c):
        self.lights.append ((id, value, c[0], c[1]))

    def vu (self, reading, x, y, w, h, kind, caption):
        self.ops.append (("vu", x, y, w, h, kind))
        # the needle: the picture's pivot and length, in this window (the face fills its width, centred on its height)
        face_w, face_h = 386.0, 194.0                     # (the picture's window: 12..398, 22..216)
        sc = w / face_w; oy = (face_h * sc - h) / 2
        px, py = x + (VU_PIVOT[0] - 12) * sc, y + (VU_PIVOT[1] - 22) * sc - oy
        self.meters.append (dict (kind = "vu", reading = reading, px = px, py = py, reach = VU_LEN * sc, window = (x, y, w, h)))
        self.text (x + w / 2, y + h + 22, caption, 15)

    def arc (self, reading, x, y, w, h, caption):
        self.ops.append (("arc", x, y, w, h))
        sc = w / ARC.width; oy = (ARC.height * sc - h) / 2
        px, py = x + ARC_PIVOT[0] * sc, y + ARC_PIVOT[1] * sc - oy
        self.meters.append (dict (kind = "arc", reading = reading, px = px, py = py, r0 = ARC_R0 * sc, r1 = ARC_R1 * sc, window = (x, y, w, h)))
        self.text (x + w / 2, y + h + 22, caption, 15)

    def title (self, a, b):
        self.text (170, 120, a, 24, anchor = "lm"); self.text (170, 150, b, 14, anchor = "lm", glow = False)

    # -- replay
    def render (self, skin):
        img = self.base (skin)
        for op in self.ops:
            kind = op[0]
            if kind == "text": self._text (img, *op[1:])
            elif kind == "knob":
                _, c, size, colour = op
                sh = Image.new ("RGBA", img.size, (0, 0, 0, 0))
                ImageDraw.Draw (sh).ellipse ([c[0] - size / 2 + 3, c[1] - size / 2 + 7, c[0] + size / 2 + 3, c[1] + size / 2 + 7], fill = (0, 0, 0, 170))
                img.alpha_composite (sh.filter (ImageFilter.GaussianBlur (size * 0.06)))
            elif kind in ("halo", "ring"):
                _, c, size = op
                k = 0.52 if kind == "halo" else 0.55
                g = Image.new ("RGBA", img.size, (0, 0, 0, 0))
                ImageDraw.Draw (g).ellipse ([c[0] - size * k, c[1] - size * k, c[0] + size * k, c[1] + size * k], outline = self.neon + (200,), width = 3)
                img.alpha_composite (g.filter (ImageFilter.GaussianBlur (4)))
                if kind == "ring": img.alpha_composite (g)
            elif kind in ("vu", "arc"):
                x, y, w, h = op[1:5]
                g = Image.new ("RGBA", img.size, (0, 0, 0, 0))
                ImageDraw.Draw (g).rounded_rectangle ([x - 4, y - 4, x + w + 4, y + h + 4], 22, outline = self.neon + (255,), width = 4)
                img.alpha_composite (g.filter (ImageFilter.GaussianBlur (9))); img.alpha_composite (g)
                if kind == "vu":
                    face = VU.crop ((12, 22, 398, 216))
                    if op[5] == "amber": face = amberise (face)
                else:
                    face = ARC
                sc = w / face.width
                face = face.resize ((w, int (round (face.height * sc))), Image.LANCZOS)
                oy = (face.height - h) // 2
                face = face.crop ((0, oy, w, oy + h))
                m = Image.new ("L", (w, h), 0); ImageDraw.Draw (m).rounded_rectangle ([0, 0, w - 1, h - 1], 18, fill = 255)
                fa = face.convert ("RGBA"); fa.putalpha (m)
                img.alpha_composite (fa, (int (x), int (y)))
                gl = Image.new ("RGBA", (w, h), (0, 0, 0, 0))
                ImageDraw.Draw (gl).polygon ([(0, 0), (w * 0.5, 0), (w * 0.3, h), (0, h)], fill = (255, 255, 255, 26))
                gl.putalpha (Image.fromarray (np.minimum (np.array (gl.getchannel ("A")), np.array (m))))
                img.alpha_composite (gl, (int (x), int (y)))
        return img

# ---------------------------------------------------------------------------------------------------------------
def hill():
    f = Face ("hill", 2000, 900, (60, 255, 140), (0, 150, 70), (34, 58, 44))
    f.title ("MASTERING HILL COMP", "OPTICAL + DISCRETE")
    W = f.W; mw, mh = 290, 150
    f.vu (0, W / 2 - mw - 20, 90, mw, mh, "cream", "LEFT"); f.vu (1, W / 2 + 20, 90, mw, mh, "cream", "RIGHT")
    for side, sx in (("L", 1), ("R", -1)):
        x = [ (150 + 172 * i) if sx == 1 else (W - 150 - 172 * i) for i in range (4) ]
        f.knob ("optThresh" + side, (x[0], 330), 96, "green", "OPT THRESHOLD")
        f.knob ("optGain" + side, (x[1], 330), 84, "green", "OPT GAIN", scale = [str (v) if v % 4 == 0 else "" for v in range (0, 21, 2)])
        f.chicken ("meter" + side, (x[2], 330), 74, ["OPT", "DISC", "OUT"], "METER")
        f.chicken ("xfmr" + side, (x[3], 330), 74, ["NI", "FE", "ST"], "IRON")
        f.knob ("discThresh" + side, (x[0], 535), 96, "black", "DISC THRESHOLD")
        f.chicken ("discRatio" + side, (x[1], 535), 74, ["1.2", "2", "3", "4", "FLD"], "RATIO")
        f.chicken ("discAttack" + side, (x[2], 535), 74, ["1", "3", "5", "10", "15", "30"], "ATTACK")
        f.chicken ("discRecover" + side, (x[3], 535), 74, [".1", ".25", ".5", ".8", "1.2", "DL"], "RECOVER")
        f.knob ("discGain" + side, (x[0], 740), 72, "black", "DISC GAIN", scale = [str (v) for v in range (0, 21, 4)])
        f.toggle ("optIn" + side, (x[1], 750), "OPT IN"); f.light ("optIn" + side, 1, (x[1], 705))
        f.toggle ("discIn" + side, (x[2], 750), "DISC IN"); f.light ("discIn" + side, 1, (x[2], 705))
        f.toggle ("scFilter" + side, (x[3], 750), "SC FILTER"); f.light ("scFilter" + side, 1, (x[3], 705))
    cx = W / 2
    f.chicken ("stereo", (cx, 400), 74, ["DUAL", "ST"], "MODE")
    f.knob ("mix", (cx - 90, 590), 76, "grey", "MIX", scale = ["0", "", "", "", "", "50", "", "", "", "", "100"])
    f.toggle ("power", (cx + 90, 590), "POWER"); f.light ("power", 1, (cx + 90, 545))
    return f

def child():
    f = Face ("child", 1600, 860, (255, 60, 70), (170, 0, 40), (112, 26, 28))
    f.title ("MASTERING CHILD COMP", "VARIABLE-MU")
    W = f.W; mw, mh = 270, 150
    f.vu (0, W / 2 - mw + 60, 90, mw, mh, "amber", "LEFT-LAT"); f.vu (1, W / 2 + 100, 90, mw, mh, "amber", "RIGHT-VERT")
    for side, sx in (("L", 1), ("R", -1)):
        x = [ (190 + 165 * i) if sx == 1 else (W - 190 - 165 * i) for i in range (3) ]
        f.knob ("input" + side, (x[0], 345), 100, "red", "INPUT GAIN", scale = [str (v) if v % 4 == 0 else "" for v in range (0, 21, 2)])
        f.knob ("thresh" + side, (x[1], 345), 100, "black", "THRESHOLD")
        f.chicken ("meter" + side, (x[2], 345), 74, ["IN", "GR", "OUT"], "METERING")
        f.chicken ("time" + side, (x[0], 570), 74, ["1", "2", "3", "4", "5", "6"], "TIME CONSTANT")
        f.knob ("scHpf" + side, (x[1], 570), 84, "grey", "SC FILTER", scale = ["OFF", "", "", "", "", "250", "", "", "", "", "500"])
        f.knob ("out" + side, (x[2], 570), 84, "black", "OUTPUT", scale = ["-20", "", "-10", "", "", "0", "", "", "+10", "", "+20"])
    cx = W / 2
    f.knob ("mix", (cx, 470), 84, "red", "MIX", scale = ["0", "", "", "", "", "50", "", "", "", "", "100"])
    f.chicken ("agc", (cx - 210, 745), 64, ["L/R", "LAT"], "AGC")
    f.toggle ("linkSc", (cx - 70, 745), "LINK SC")
    f.toggle ("linkCtl", (cx + 70, 745), "LINK CTL")
    f.toggle ("power", (cx + 210, 745), "POWER"); f.light ("power", 1, (cx + 210, 700))
    return f

def iron():
    f = Face ("iron", 1900, 900, (255, 170, 60), (140, 70, 10), (36, 36, 38))
    f.title ("IRON", "VALVE MASTERING COMPRESSOR")
    W = f.W; mw, mh = 300, 150
    f.arc (0, W / 2 - mw - 20, 90, mw, mh, "1"); f.arc (1, W / 2 + 20, 90, mw, mh, "2")
    for side, sx in (("L", 1), ("R", -1)):
        X = lambda i: (170 + 150 * i) if sx == 1 else (W - 170 - 150 * i)
        f.chicken ("attack" + side, (X (0), 320), 72, [".3", "1", "3", "10", "20", "40"], "ATTACK")
        f.chicken ("release" + side, (X (1), 320), 72, ["50", "100", "200", "400", "800", "1.6"], "RELEASE")
        f.knob ("input" + side, (X (2), 320), 80, "amber", "INPUT", scale = [str (v) for v in range (2, 13)])
        f.knob ("thresh" + side, (X (0) + sx * 75, 520), 124, "black", "THRESHOLD", scale = [str (v) for v in range (0, 41, 4)])
        f.knob ("output" + side, (X (2), 520), 80, "amber", "OUTPUT", scale = [str (v) for v in range (2, 13)])
        f.chicken ("rect" + side, (X (0), 755), 64, ["GE", "LED", "SI", "G/S"], "RECTIFIER")
        f.chicken ("scEq" + side, (X (1), 755), 64, ["OFF", "1", "2", "3", "4"], "SC EQ")
        f.chicken ("bias" + side, (X (2), 755), 64, ["LO", "MID", "HI"], "TUBE BIAS")
        f.chicken ("meter" + side, (X (3), 755), 64, ["GR", "0", "+10"], "METER")
    cx = W / 2
    f.knob ("tape", (cx - 90, 420), 76, "amber", "TAPE ROLL-OFF", scale = ["0", "", "", "", "", "50", "", "", "", "", "100"])
    f.knob ("mix", (cx + 90, 420), 76, "amber", "MIX", scale = ["0", "", "", "", "", "50", "", "", "", "", "100"])
    f.toggle ("airBass", (cx - 140, 620), "AIRBASS"); f.light ("airBass", 1, (cx - 140, 575))
    f.toggle ("link", (cx, 620), "SC LINK"); f.light ("link", 1, (cx, 575))
    f.toggle ("power", (cx + 140, 620), "POWER"); f.light ("power", 1, (cx + 140, 575))
    return f

FACES = (("masterHill", hill), ("masterChild", child), ("masterIron", iron))
SKINS = ("Neon Glass", "Neon Glass + Panel", "Neon Clear")

def lamp (rgb, size = 44):
    im = Image.new ("RGBA", (size, size), (0, 0, 0, 0)); d = ImageDraw.Draw (im); c = size / 2
    d.ellipse ([c - 8, c - 8, c + 8, c + 8], fill = rgb + (255,))
    glow = im.filter (ImageFilter.GaussianBlur (6))
    out = Image.new ("RGBA", (size, size), (0, 0, 0, 0)); out.alpha_composite (glow); out.alpha_composite (glow)
    core = Image.new ("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw (core).ellipse ([c - 5, c - 5, c + 5, c + 5], fill = tuple (min (255, v // 2 + 128) for v in rgb) + (255,))
    out.alpha_composite (core.filter (ImageFilter.GaussianBlur (1)))
    return out

def header (records):
    def rgb (c): return "0xff%02x%02x%02xu" % tuple (int (v) for v in c)
    L = ["#pragma once", "// Generated by Tools/compressor_skins/neon_faces.py - edit the faces there.", "#include <vector>",
         "namespace riff::masteringfaces", "{",
         "    enum class Part { knob, chicken, toggle };",
         "    /** A control: where it turns round (the face's pixels), its radius, its settings' angles; a knob's skirt colour (black: 0xff000000) and its ring's neon. */",
         "    struct Item { const char* id; Part part; float x, y, r; std::vector<float> angles; unsigned int skirt, neon; };",
         "    /** A light: lit while its parameter is at value (its picture: the face's neon lamp). */",
         "    struct Light { const char* id; int value; float x, y; };",
         "    /** A needle: a VU's (the picture's needle: from its pivot, reach long) or the arc meter's (between r0 and r1). */",
         "    struct Meter { int reading; bool arc; float px, py, reach, r0, r1; float window[4]; };",
         "    struct Spot { const char* id; float x, y, position; };",
         "    struct Face { const char* type; const char* key; int width, height; unsigned int neon; std::vector<Item> items; std::vector<Light> lights;",
         "                  std::vector<Meter> meters; std::vector<Spot> spots; };",
         "    /** The skins, in their menu's order (the insert's \"skin\"); the faces' pictures are mn_<key>_<skin>. */",
         "    inline const char* const skinNames[] { %s };" % ", ".join ('"%s"' % s for s in SKINS),
         "    inline const std::vector<Face>& faces()", "    {", "        static const std::vector<Face> all {"]
    for type, f in records:
        L.append ('            { "%s", "%s", %d, %d, %s, {' % (type, f.key, f.W, f.H, rgb (f.neon)))
        for (id, part, x, y, r, angles, ink, neon) in f.items:   # (ink: a knob's skirt)
            L.append ('                { "%s", Part::%s, %.1ff, %.1ff, %.1ff, { %s }, %s, %s },' % (id, part, x, y, r, ", ".join ("%.1ff" % a for a in angles), rgb (ink), rgb (neon)))
        L.append ("            }, {")
        for (id, value, x, y) in f.lights:
            L.append ('                { "%s", %d, %.1ff, %.1ff },' % (id, value, x, y))
        L.append ("            }, {")
        for m in f.meters:
            w = m["window"]
            if m["kind"] == "vu":
                L.append ('                { %d, false, %.2ff, %.2ff, %.2ff, 0, 0, { %.1ff, %.1ff, %.1ff, %.1ff } },' % (m["reading"], m["px"], m["py"], m["reach"], *w))
            else:
                L.append ('                { %d, true, %.2ff, %.2ff, 0, %.2ff, %.2ff, { %.1ff, %.1ff, %.1ff, %.1ff } },' % (m["reading"], m["px"], m["py"], m["r0"], m["r1"], *w))
        L.append ("            }, {")
        for (id, x, y, pos) in f.spots:
            L.append ('                { "%s", %.1ff, %.1ff, %.4ff },' % (id, x, y, pos))
        L.append ("            } },")
    L += ["        };", "        return all;", "    }", "}", ""]
    open (os.path.join (ROOT, "Source", "ui", "MasteringLayouts.h"), "w").write ("\n".join (L))

def build (preview = None):
    records = []; neons = set()
    for type, fn in FACES:
        f = fn(); records.append ((type, f)); neons.add (f.neon)
        for skin in range (3):
            img = f.render (skin)
            if preview:
                img.save (os.path.join (preview, "mn_%s_%d.png" % (f.key, skin)))
                continue
            if skin == 2:
                img.save (os.path.join (OUT, "mn_%s_%d.png" % (f.key, skin)), optimize = True)
            else:
                img.convert ("RGB").save (os.path.join (OUT, "mn_%s_%d.jpg" % (f.key, skin)), quality = 88, optimize = True)
    if not preview:
        Image.fromarray (KBODY.clip (0, 255).astype ("uint8"), "RGBA").save (os.path.join (ROOT, "Resources", "skin", "looks", "look_knob_silver.png"), optimize = True)
        for n in neons:
            lamp (n).save (os.path.join (OUT, "mn_lamp_%02x%02x%02x.png" % n))
        header (records)

if __name__ == "__main__":
    if "--build" in sys.argv: build()
    elif "--preview" in sys.argv:
        d = sys.argv[sys.argv.index ("--preview") + 1]; os.makedirs (d, exist_ok = True); build (d)
    print ("done")
