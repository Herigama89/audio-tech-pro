#!/usr/bin/env python3
"""
Mastering Compressors: three stereo mastering compressors whose faces follow the hardware's own - layout, the kind of
knobs and how far they turn, the meters - drawn in our own style (our knobs, our lettering, no maker's artwork or
names):

    Mastering Hill Comp    an optical stage and a discrete stage per channel, three output transformers
    Mastering Child Comp   a variable-mu valve limiter: input, threshold, time constant per channel, AGC, outputs
    Iron Master Comp       a valve mastering compressor: rectifier, tube bias, side-chain EQs, arc meters

    python3 Tools/compressor_skins/mastering_faces.py --preview OUT_DIR
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
import classic_faces as cf

PREVIEW = sys.argv[sys.argv.index ("--preview") + 1] if "--preview" in sys.argv else None
COND = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf"
COND_BOOK = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf"
SERIF_IT = "/usr/share/fonts/truetype/freefont/FreeSerifBoldItalic.ttf"
INTER = "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"
def font (p, s): return ImageFont.truetype (p, s)

def polar (c, r, a): return c[0] + math.sin (math.radians (a)) * r, c[1] - math.cos (math.radians (a)) * r

def ctext (d, x, y, s, f, fill, spacing = 0):
    """Centred on (x, y)."""
    if spacing:
        w = sum (d.textlength (ch, font = f) for ch in s) + spacing * (len (s) - 1)
        cx = x - w / 2
        for ch in s:
            d.text ((cx, y), ch, font = f, fill = fill, anchor = "lm")
            cx += d.textlength (ch, font = f) + spacing
        return
    d.text ((x, y), s, font = f, fill = fill, anchor = "mm")

# ---- our knobs ------------------------------------------------------------------------------------------------------
def knob (size, kind = "dome", pointer = "line", base = (40, 40, 44), flutes = 22):
    """A knob drawn from scratch, pointing up: a glossy black dome (a white line) or fluted bakelite (a white dot)."""
    ss = 3; N = size * ss
    yy, xx = np.mgrid[0:N, 0:N].astype (np.float32)
    c = N / 2; R = N * 0.40
    dx, dy = (xx - c) / R, (yy - c) / R
    r = np.sqrt (dx * dx + dy * dy) + 1e-6; th = np.arctan2 (dy, dx)
    edge = 1.0 - (0.045 * (0.5 + 0.5 * np.cos (flutes * th)) if kind == "fluted" else 0.0)
    inside = r < edge
    top = 0.70
    # surface normals: a gently domed top, the sides falling away (fluted: rippled round)
    s = np.where (r < top, 0.28 * r / top, 0.35 + 1.4 * np.clip ((r - top) / (1 - top), 0, None) ** 1.5)
    nx, ny = dx / r * s, dy / r * s
    if kind == "fluted":
        ripple = np.where (r > top * 0.92, 0.9 * np.sin (flutes * th), 0.0)
        nx += -np.sin (th) * ripple; ny += np.cos (th) * ripple
    nz = np.ones_like (nx)
    n = np.sqrt (nx * nx + ny * ny + nz * nz); nx, ny, nz = nx / n, ny / n, nz / n
    L = np.array ([-0.45, -0.65, 0.62]); L /= np.linalg.norm (L)
    Hh = L + np.array ([0, 0, 1.0]); Hh /= np.linalg.norm (Hh)
    diff = np.clip (nx * L[0] + ny * L[1] + nz * L[2], 0, 1)
    spec = np.clip (nx * Hh[0] + ny * Hh[1] + nz * Hh[2], 0, 1) ** 60
    b = np.array (base, np.float32)
    col = b[None, None, :] * (0.45 + 0.9 * diff[..., None]) + 255 * 0.55 * spec[..., None]
    rim = np.clip ((r - (edge - 0.03)) / 0.03, 0, 1)   # a darker rim
    col *= (1 - 0.5 * rim[..., None])
    img = np.dstack ([np.clip (col, 0, 255), inside.astype (np.float32) * 255]).astype (np.uint8)
    im = Image.fromarray (img, "RGBA")
    d = ImageDraw.Draw (im)
    if pointer == "line":
        d.line ((c, c - R * 0.12, c, c - R * 0.66), fill = (246, 246, 240, 255), width = int (R * 0.07))
    elif pointer == "dot":
        d.ellipse ((c - R * 0.07, c - R * 0.80 - R * 0.07, c + R * 0.07, c - R * 0.80 + R * 0.07), fill = (246, 246, 240, 255))
    im = im.resize ((size, size), Image.LANCZOS)
    # its shadow on the panel
    sh = Image.new ("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw (sh).ellipse ((size * 0.12 + size * 0.03, size * 0.12 + size * 0.05, size * 0.88 + size * 0.03, size * 0.88 + size * 0.05), fill = (0, 0, 0, 170))
    sh = sh.filter (ImageFilter.GaussianBlur (size * 0.035))
    sh.alpha_composite (im)
    return sh

def chicken (size):
    """The chicken-head pointer knob (your knob_66_black)."""
    return cf.sprites()["big"].resize ((size, size), Image.LANCZOS)

def put (img, im, c, angle = 0.0):
    im = im.rotate (-angle, resample = Image.BICUBIC)
    img.alpha_composite (im, (int (c[0] - im.width / 2), int (c[1] - im.height / 2)))

# ---- meters ---------------------------------------------------------------------------------------------------------
def cream_vu (w, h, value = 0.55, caption = "GAIN REDUCTION"):
    """A cream VU face: -20..+3, red past 0, black needle."""
    yy, xx = np.mgrid[0:h, 0:w].astype (np.float32)
    glow = np.exp (-(((xx - w * 0.5) / (w * 0.7)) ** 2 + ((yy - h * 0.35) / (h * 0.9)) ** 2))
    face = np.zeros ((h, w, 4), np.float32)
    face[..., 0] = 205 + 40 * glow; face[..., 1] = 190 + 40 * glow; face[..., 2] = 150 + 40 * glow; face[..., 3] = 255
    im = Image.fromarray (face.astype (np.uint8), "RGBA"); d = ImageDraw.Draw (im)
    pivot = (w * 0.5, h * 1.02); R = h * 0.80
    marks = [(-20, "20"), (-10, "10"), (-7, "7"), (-5, "5"), (-3, "3"), (-1, "1"), (0, "0"), (1, "1"), (2, "2"), (3, "3")]
    def ang (db): t = (10 ** (db / 20) - 0.1) / (10 ** (3 / 20) - 0.1); return -46 + 92 * t
    d.arc ((pivot[0] - R, pivot[1] - R, pivot[0] + R, pivot[1] + R), 270 + ang (-20), 270 + ang (0), fill = (30, 26, 20, 255), width = 3)
    d.arc ((pivot[0] - R, pivot[1] - R, pivot[0] + R, pivot[1] + R), 270 + ang (0), 270 + ang (3), fill = (196, 36, 30, 255), width = 7)
    f = font (COND, int (h * 0.085))
    for db, lab in marks:
        a = ang (db); red = db > 0
        d.line (polar (pivot, R, a) + polar (pivot, R + h * 0.05, a), fill = (196, 36, 30, 255) if red else (30, 26, 20, 255), width = 2)
        x, y = polar (pivot, R + h * 0.12, a)
        ctext (d, x, y, ("+" if red else "") + lab, f, (196, 36, 30, 255) if red else (30, 26, 20, 255))
    ctext (d, w * 0.1, h * 0.52, "VU", font (SERIF_IT, int (h * 0.1)), (30, 26, 20, 255))
    ctext (d, w * 0.9, h * 0.52, "VU", font (SERIF_IT, int (h * 0.1)), (196, 36, 30, 255))
    ctext (d, w * 0.5, h * 0.62, caption, font (COND_BOOK, int (h * 0.06)), (60, 54, 44, 255))
    a = math.radians (-46 + 92 * value)
    d.line ((pivot[0], pivot[1], pivot[0] + math.sin (a) * (R + h * 0.06), pivot[1] - math.cos (a) * (R + h * 0.06)), fill = (14, 12, 10, 255), width = 3)
    # the needle's housing
    d.rounded_rectangle ((w * 0.36, h * 0.80, w * 0.64, h + 10), 6, fill = (16, 16, 18, 255))
    return im

import vu_meter

def our_vu (w, h, value = 0.55, face = "cream"):
    """Our own VU meter (vu_meter.py)."""
    return vu_meter.vu (w, h, value, face = face)

def bezel (img, x0, y0, w, h, r = 18, depth = 14):
    """A black meter housing round a face."""
    d = ImageDraw.Draw (img)
    sh = Image.new ("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw (sh).rounded_rectangle ((x0 - depth + 6, y0 - depth + 10, x0 + w + depth + 6, y0 + h + depth + 12), r + 6, fill = (0, 0, 0, 170))
    img.alpha_composite (sh.filter (ImageFilter.GaussianBlur (12)))
    d.rounded_rectangle ((x0 - depth, y0 - depth, x0 + w + depth, y0 + h + depth), r + 6, fill = (14, 14, 15, 255), outline = (58, 58, 62, 255), width = 2)
    d.rounded_rectangle ((x0 - 4, y0 - 4, x0 + w + 4, y0 + h + 4), r, fill = (6, 6, 7, 255))

def place_meter (img, face, x0, y0, r = 16):
    m = Image.new ("L", face.size, 0); ImageDraw.Draw (m).rounded_rectangle ((0, 0, face.width - 1, face.height - 1), r, fill = 255)
    face = face.copy(); face.putalpha (m)
    img.alpha_composite (face, (int (x0), int (y0)))
    gl = Image.new ("RGBA", face.size, (0, 0, 0, 0))   # (its glass)
    ImageDraw.Draw (gl).polygon (((0, 0), (face.width * 0.55, 0), (face.width * 0.35, face.height), (0, face.height)), fill = (255, 255, 255, 22))
    gl.putalpha (Image.fromarray (np.minimum (np.array (gl.getchannel ("A")), np.array (m))))
    img.alpha_composite (gl, (int (x0), int (y0)))

def arc_meter (img, c, r0, r1, a0 = -40, a1 = 40, value = 0.5):
    """The arc meter over the threshold: a black fan-shaped window, its scale lit from inside (the glow stays in it)."""
    W, H = img.size
    yy, xx = np.mgrid[0:H, 0:W].astype (np.float32)
    rr = np.sqrt ((xx - c[0]) ** 2 + (yy - c[1]) ** 2); aa = np.degrees (np.arctan2 (xx - c[0], c[1] - yy))
    def sector (ra, rb, pa, pb):
        return ((rr > ra) & (rr < rb) & (aa > pa) & (aa < pb)).astype (np.float32)
    # the frame (a moulded black rim), then the window inside it
    rim = Image.fromarray ((sector (r0 - 12, r1 + 12, a0 - 3, a1 + 3) * 255).astype (np.uint8)).filter (ImageFilter.GaussianBlur (1.2))
    lay = Image.new ("RGBA", (W, H), (46, 46, 50, 255)); lay.putalpha (rim)
    img.alpha_composite (lay)
    inside = sector (r0, r1, a0, a1)
    mid = (r0 + r1) / 2
    glow = np.clip (1 - np.abs (rr - mid * 1.02) / ((r1 - r0) * 0.75), 0, 1) ** 2 * np.clip (1 - np.abs (aa) / (a1 * 1.3), 0, 1)
    win = np.zeros ((H, W, 4), np.float32)
    win[..., 0] = 12 + 46 * glow; win[..., 1] = 9 + 20 * glow; win[..., 2] = 8 + 6 * glow
    win[..., 3] = inside * 255
    img.alpha_composite (Image.fromarray (win.astype (np.uint8), "RGBA"))
    # the scale, printed in lit orange; red past 0
    d = ImageDraw.Draw (img)
    ink = (255, 158, 72, 255); red = (255, 64, 36, 255)
    labels = ["20", "10", "7", "5", "4", "3", "2", "1", "0", "1", "2", "3"]
    span = lambda t: a0 + 5 + (a1 - a0 - 10) * t
    rs = r0 + (r1 - r0) * 0.42
    d.arc ((c[0] - rs, c[1] - rs, c[0] + rs, c[1] + rs), 270 + span (0), 270 + span (8 / 11), fill = ink, width = 2)
    d.arc ((c[0] - rs - 3, c[1] - rs - 3, c[0] + rs + 3, c[1] + rs + 3), 270 + span (8 / 11), 270 + span (1), fill = red, width = 7)
    for i, lab in enumerate (labels):
        a = span (i / (len (labels) - 1))
        col = red if i > 8 else ink
        d.line (polar (c, rs, a) + polar (c, rs + 14, a), fill = col, width = 2)
        x, y = polar (c, rs + 32, a)
        ctext (d, x, y, lab, font (COND, 17), col)
    for i in range (33):
        a = span (i / 32)
        d.line (polar (c, rs, a) + polar (c, rs + 7, a), fill = ink, width = 1)
    ctext (d, c[0] - 70, c[1] - r0 - 22, "−", font (COND, 20), ink)
    ctext (d, c[0] + 70, c[1] - r0 - 22, "+", font (COND, 20), red)
    ctext (d, c[0] + 86, c[1] - r0 - 20, "VU", font (COND, 15), red)
    # the needle (with a faint shadow), from below the window
    a = span (value)
    d.line (polar ((c[0] + 3, c[1] + 4), r0 + 2, a) + polar ((c[0] + 3, c[1] + 4), r1 - 8, a), fill = (0, 0, 0, 160), width = 3)
    d.line (polar (c, r0 + 2, a) + polar (c, r1 - 8, a), fill = (255, 226, 190, 255), width = 3)
    # the glass: a dark inner edge and a soft sheen across the top
    edge = np.clip (np.minimum (np.minimum (rr - r0, r1 - rr), np.minimum (aa - a0, a1 - aa) * (math.pi / 180) * rr) / 14, 0, 1)
    sh = np.zeros ((H, W, 4), np.float32); sh[..., 3] = inside * (1 - edge) * 150
    img.alpha_composite (Image.fromarray (sh.astype (np.uint8), "RGBA"))
    sheen = np.zeros ((H, W, 4), np.float32); sheen[..., :3] = 255
    sheen[..., 3] = inside * np.clip ((rr - (r1 - (r1 - r0) * 0.3)) / ((r1 - r0) * 0.3), 0, 1) * 22
    img.alpha_composite (Image.fromarray (sheen.astype (np.uint8), "RGBA"))

# ---- panels ---------------------------------------------------------------------------------------------------------
def enamel (W, H, base, grain = 2.4, seed = 3, vignette = 0.25):
    rng = np.random.default_rng (seed)
    yy, xx = np.mgrid[0:H, 0:W].astype (np.float32)
    v = 1 - vignette * (((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2) * 0.5
    body = np.array (base, np.float32)[None, None, :] * v[..., None]
    body += rng.normal (0, 1, (H, W, 1)).astype (np.float32) * grain
    return Image.fromarray (np.clip (body, 0, 255).astype (np.uint8)).convert ("RGBA")

def rails (img, x0, x1):
    """Grey steel rack rails with their slots and screws."""
    W, H = img.size
    d = ImageDraw.Draw (img)
    for a, b in ((0, x0), (x1, W)):
        rail = enamel (b - a, H, (150, 152, 152), grain = 3.5, seed = a + 7, vignette = 0.1)
        img.alpha_composite (rail, (a, 0))
        d.line ((a + 2, 0, a + 2, H), fill = (196, 198, 198, 255), width = 2)
        d.line ((b - 2, 0, b - 2, H), fill = (90, 92, 92, 255), width = 2)
        cx = (a + b) // 2
        for y in range (60, H - 30, 140):
            d.rounded_rectangle ((cx - 18, y, cx + 18, y + 22), 11, fill = (20, 20, 22, 255))
            cf.screw (img, cx, y + 80) if y + 80 < H - 20 else None

def plate_screws (img, pts):
    for x, y in pts:
        cf.screw (img, x, y)

def ring_labels (d, c, r, labels, a0, a1, f, ink, dots = True, tick = 0):
    n = len (labels)
    for i, lab in enumerate (labels):
        a = a0 + (a1 - a0) * i / max (1, n - 1)
        x, y = polar (c, r, a)
        ctext (d, x, y, lab, f, ink)
        if dots:
            x2, y2 = polar (c, r - 18, a)
            d.ellipse ((x2 - 2.2, y2 - 2.2, x2 + 2.2, y2 + 2.2), fill = ink)
    if tick:
        for i in range ((n - 1) * tick + 1):
            a = a0 + (a1 - a0) * i / ((n - 1) * tick)
            x2, y2 = polar (c, r - 18, a)
            d.ellipse ((x2 - 1.2, y2 - 1.2, x2 + 1.2, y2 + 1.2), fill = ink)

def led (img, c, on = True, colour = (90, 255, 170)):
    d = ImageDraw.Draw (img)
    if on:
        g = Image.new ("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw (g).ellipse ((c[0] - 26, c[1] - 26, c[0] + 26, c[1] + 26), fill = colour + (120,))
        img.alpha_composite (g.filter (ImageFilter.GaussianBlur (10)))
    d.ellipse ((c[0] - 11, c[1] - 11, c[0] + 11, c[1] + 11), fill = (30, 30, 32, 255))
    d.ellipse ((c[0] - 8, c[1] - 8, c[0] + 8, c[1] + 8), fill = colour + (255,) if on else (40, 60, 50, 255))
    d.ellipse ((c[0] - 4, c[1] - 6, c[0] + 1, c[1] - 2), fill = (255, 255, 255, 180))

def toggle (img, c, up = True, size = 70):
    sw = cf.sprites()["switch_up" if up else "switch_down"].resize ((size, size), Image.LANCZOS)
    img.alpha_composite (sw, (int (c[0] - size / 2), int (c[1] - size / 2)))

def lamp (img, c, r, colour = (255, 170, 60)):
    g = Image.new ("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw (g).ellipse ((c[0] - r * 1.6, c[1] - r * 1.6, c[0] + r * 1.6, c[1] + r * 1.6), fill = colour + (140,))
    img.alpha_composite (g.filter (ImageFilter.GaussianBlur (r * 0.6)))
    yy, xx = np.mgrid[0:2 * r, 0:2 * r].astype (np.float32)
    rr = np.sqrt ((xx - r) ** 2 + (yy - r) ** 2) / r
    col = np.zeros ((2 * r, 2 * r, 4), np.float32)
    core = np.clip (1 - rr, 0, 1)
    cc = np.array (colour, np.float32)
    col[..., 0] = cc[0] * 0.75 + (255 - cc[0] * 0.75) * core ** 1.5
    col[..., 1] = cc[1] * 0.55 + (255 - cc[1] * 0.55) * core ** 0.9
    col[..., 2] = cc[2] * 0.5 + (255 - cc[2] * 0.5) * core ** 2
    col[..., 3] = (rr < 1) * 255
    img.alpha_composite (Image.fromarray (col.astype (np.uint8), "RGBA"), (int (c[0] - r), int (c[1] - r)))
    ImageDraw.Draw (img).ellipse ((c[0] - r - 6, c[1] - r - 6, c[0] + r + 6, c[1] + r + 6), outline = (60, 60, 64, 255), width = 6)

# ---- Mastering Child Comp: the variable-mu valve limiter ---------------------------------------------------------------
def child():
    W, H = 1500, 1000
    img = Image.new ("RGBA", (W, H), (16, 16, 18, 255))
    main = enamel (W - 200, 690, (30, 30, 31), grain = 3.0, seed = 5)
    img.alpha_composite (main, (100, 16))
    low = enamel (W - 200, 270, (24, 24, 26), grain = 3.0, seed = 6)
    img.alpha_composite (low, (100, 720))
    rails (img, 92, W - 92)
    d = ImageDraw.Draw (img)
    ink = (236, 232, 222, 255); red = (214, 50, 44, 255)
    lab = font (COND, 19); small = font (COND, 16); nums = font (COND_BOOK, 17)
    d.rectangle ((100, 16, W - 100, 706), outline = (8, 8, 8, 255), width = 3)
    d.rectangle ((100, 720, W - 100, 990), outline = (8, 8, 8, 255), width = 3)
    # meters, left (left-lat) above right (right-vert)
    for k, y in enumerate ((90, 390)):
        bezel (img, 250, y, 250, 200)
        place_meter (img, our_vu (250, 200, 0.45 + 0.1 * k, face = "amber"), 250, y)
    toggle (img, (175, 200)); ctext (d, 175, 150, "ON", small, ink)
    lamp (img, (175, 360), 16)
    d.ellipse ((150, 510, 200, 560), fill = (12, 12, 12, 255), outline = (70, 70, 72, 255), width = 3); ctext (d, 175, 585, "FUSE", small, ink)
    big = knob (170, "fluted", "dot"); mid = knob (150, "fluted", "dot")
    for k, (y, side, colour) in enumerate (((190, "LEFT", "LAT"), (490, "RIGHT", "VERT"))):
        # metering: in / gr / out
        ctext (d, 650, y - 115, side + "–" + colour, lab, ink); ctext (d, 650, y - 92, "METERING", lab, ink)
        for a, s in zip ((-55, 0, 55), ("IN", "GR", "OUT")):
            x, yy2 = polar ((650, y + 10), 62, a); ctext (d, x, yy2, s, small, ink)
        put (img, chicken (120), (650, y + 10), -12 + 12)
        ctext (d, 595, y + 95, "HR", small, ink); ctext (d, 705, y + 95, "BAL", small, ink)
        # input gain: 20 (left) down to 0 (right)
        ctext (d, 880, y - 115, side + " CHANNEL", lab, ink); ctext (d, 880, y - 92, "INPUT GAIN", lab, ink)
        ring_labels (d, (880, y + 20), 92, [str (v) for v in range (20, -1, -2)], -150, 150, nums, ink, dots = False)
        put (img, big, (880, y + 20), 60)
        # threshold 0..10
        ctext (d, 1120, y - 115, side + "–" + colour, lab, ink); ctext (d, 1120, y - 92, "THRESHOLD", lab, ink)
        ring_labels (d, (1120, y + 20), 92, [str (v) for v in range (0, 11)], -150, 150, nums, ink, dots = False)
        put (img, big, (1120, y + 20), 120)
        # time constant 1..6 over the top
        ty = y - 10 if k == 0 else y + 50
        ctext (d, 1325, ty - 95, side + "–" + colour, lab, ink); ctext (d, 1325, ty - 72, "TIME CONSTANT", lab, ink)
        ring_labels (d, (1325, ty + 20), 62, [str (v) for v in range (1, 7)], -75, 75, nums, ink, dots = False)
        put (img, chicken (110), (1325, ty + 20), 45)
    # AGC: left/right or lateral/vertical
    ctext (d, 1325, 300, "AGC", lab, ink)
    ctext (d, 1262, 330, "LEFT", small, ink); ctext (d, 1262, 350, "RIGHT", small, ink)
    ctext (d, 1410, 330, "LAT", small, red); ctext (d, 1410, 350, "VERT", small, red)
    put (img, chicken (100), (1325, 375), -40)
    # our name, as the maker's would be
    d.text ((960, 652), "Mastering Child Comp", font = font (SERIF_IT, 34), fill = red, anchor = "lm")
    d.text ((1290, 656), "VARIABLE-MU LIMITER", font = font (COND, 15), fill = ink, anchor = "lm")
    # the lower plate: side-chain filters, link, outputs, mix
    ctext (d, 330, 745, "SIDECHAIN FILTER", lab, ink)
    sm = knob (130, "fluted", "dot")
    for x, s in ((230, "LEFT–LAT"), (430, "RIGHT–VERT")):
        ctext (d, x, 775, s, small, ink)
        put (img, sm, (x, 860), -120)
        d.rectangle ((x - 52, 925, x - 18, 945), outline = ink, width = 2); ctext (d, x - 35, 935, "OFF", font (COND, 12), ink)
        ctext (d, x + 50, 935, "500 Hz", small, ink)
    ctext (d, 640, 790, "— LINK —", small, ink)
    toggle (img, (590, 850), True, 66); toggle (img, (690, 850), True, 66)
    ctext (d, 590, 905, "S.CHAIN", small, ink); ctext (d, 690, 905, "CONTROLS", small, ink); ctext (d, 640, 935, "— UNLINK —", small, ink)
    for x, s in ((900, "LEFT OUTPUT"), (1100, "RIGHT OUTPUT")):
        ctext (d, x, 755, s, lab, ink)
        ring_labels (d, (x, 860), 80, ["-20", "-15", "-10", "-5", "0", "5", "10", "15", "20dB"], -135, 135, nums, ink, dots = False)
        put (img, sm, (x, 860), 0)
    ctext (d, 1290, 755, "MIX", lab, ink)
    ring_labels (d, (1290, 860), 80, ["0", "50", "100%"], -135, 135, nums, ink, dots = False)
    put (img, sm, (1290, 860), 135)
    return img

# ---- Mastering Hill Comp: optical and discrete -------------------------------------------------------------------------
def hill():
    # the hardware's layout, in its own proportions (2:1, the controls mirrored for the two channels)
    W, H, R = 2220, 960, 80          # R: each rack rail
    img = Image.new ("RGBA", (W, H), (14, 14, 15, 255))
    img.alpha_composite (enamel (W - 2 * R, H, (24, 24, 26), grain = 2.2, seed = 9), (R, 0))
    rails (img, R - 6, W - R + 6)
    d = ImageDraw.Draw (img)
    ink = (234, 234, 232, 255)
    lab = font (COND, 22); small = font (COND, 17); nums = font (COND_BOOK, 18); tiny = font (COND, 14)
    cx = W / 2
    ctext (d, cx, 40, "MASTERING HILL COMP", font (COND, 28), ink, spacing = 3)
    ctext (d, cx, 924, "MASTERING COMPRESSOR", font (COND, 26), ink, spacing = 3)
    # the meters' black housing, its grilles under the meters
    d.rounded_rectangle ((cx - 460, 86, cx + 460, 500), 30, fill = (10, 10, 11, 255), outline = (46, 46, 50, 255), width = 3)
    for x0 in (cx - 430, cx + 36):
        bezel (img, x0, 116, 394, 214, r = 18, depth = 12)
        place_meter (img, our_vu (394, 214, 0.55, face = "cream"), x0, 116, r = 16)
        d.rounded_rectangle ((x0 + 50, 360, x0 + 344, 480), 10, fill = (16, 16, 18, 255))
        for k in range (6):
            d.line ((x0 + 80 + k * 46, 372, x0 + 80 + k * 46, 470), fill = (32, 32, 36, 255), width = 6)
    T = [str (v) for v in range (0, 21, 2)]
    TT = [str (v) for v in range (0, 21)]
    outer = knob (176); inner = knob (150); big = knob (250); sel = knob (128)
    for side in (-1, 1):
        X = (lambda x: R + x) if side < 0 else (lambda x: W - R - x)
        def ring (c, r, labels, a0 = -140, a1 = 140, f = nums, tick = 0):
            ring_labels (d, c, r, labels, a0, a1, f, ink, dots = True, tick = tick)
        # outer column
        for (y, title, labels, ang) in ((150, "OPTICAL THRESHOLD", TT, -60), (564, "DISCRETE RATIO", ["1.2", "2", "3", "4", "FLOOD"], -30),
                                        (810, "DISCRETE THRESHOLD", TT, -150)):
            ctext (d, X (136), y - 110, title, lab, ink)
            ring ((X (136), y), 96, labels if len (labels) < 8 else labels[::2], tick = 2 if len (labels) > 8 else 0)
            put (img, outer, (X (136), y), ang)
        # inner column: meter select (its three lights), optical gain, discrete attack and recovery
        for k, s in enumerate (("OPTICAL", "DISCRETE", "OUTPUT")):
            lx = X (272 + k * 80); ctext (d, lx, 66, s, tiny, ink)
            led (img, (lx, 96), k == 1)
        put (img, sel, (X (352), 186), 0); ctext (d, X (352), 262, "METER SELECT", small, ink)
        ctext (d, X (352), 312, "OPTICAL GAIN", lab, ink)
        ring ((X (352), 404), 84, TT[::2], tick = 2)
        put (img, inner, (X (352), 404), -110)
        ctext (d, X (350), 540, "DISCRETE ATTACK", lab, ink)
        ring ((X (350), 624), 78, ["1", "3", "5", "10", "15", "30"], -120, 120)
        put (img, inner, (X (350), 624), 40)
        ctext (d, X (350), 770, "DISCRETE RECOVER", lab, ink)
        ring ((X (350), 852), 78, [".1", ".25", ".5", ".8", "1.2", "DUAL"], -120, 120)
        put (img, inner, (X (350), 852), -60)
        # optical bypass and its light
        led (img, (X (106), 300), True)
        toggle (img, (X (164), 366), True, 64)
        ctext (d, X (164), 322, "OPTICAL", tiny, ink); ctext (d, X (164), 408, "BYPASS", tiny, ink)
        ctext (d, X (110), 366, "IN" if side < 0 else "OUT", tiny, ink); ctext (d, X (218), 366, "OUT" if side < 0 else "IN", tiny, ink)
        # the big discrete gain
        ctext (d, X (622), 646, "DISCRETE GAIN", lab, ink)
        ring ((X (622), 800), 138, TT[::2], tick = 2)
        put (img, big, (X (622), 800), -100)
        # side-chain filter and discrete bypass, their lights
        toggle (img, (X (550), 570), True, 58); ctext (d, X (550), 532, "SIDECHAIN", tiny, ink); ctext (d, X (550), 610, "FILTER", tiny, ink)
        led (img, (X (656), 540), side > 0)
        toggle (img, (X (740), 570), True, 58); ctext (d, X (740), 530, "DISCRETE", tiny, ink); ctext (d, X (740), 608, "BYPASS", tiny, ink)
        # the transformer: nickel, iron, steel
        for k, s in enumerate (("NICKEL", "IRON", "STEEL")):
            led (img, (X (850), 716 + k * 46), k == (0 if side < 0 else 1))
            ctext (d, X (850) + (58 if side < 0 else -58), 716 + k * 46, s, tiny, ink)
        toggle (img, (X (960), 762), True, 58)
    # the middle: dual mono / stereo, hardwire bypass and its light
    ctext (d, cx - 96, 560, "DUAL MONO", small, ink); ctext (d, cx + 96, 560, "STEREO", small, ink)
    put (img, sel, (cx, 566), 40)
    led (img, (cx - 60, 650), True); led (img, (cx + 60, 650), False)
    toggle (img, (cx, 752), True, 60); ctext (d, cx, 712, "HARDWIRE", small, ink); ctext (d, cx, 792, "BYPASS", small, ink)
    lamp (img, (cx, 860), 34, (80, 255, 140))
    return img

# ---- Iron Master Comp: the valve mastering compressor ------------------------------------------------------------------
def iron():
    W, H = 2620, 940
    img = Image.new ("RGBA", (W, H), (0, 0, 0, 0))
    m = Image.new ("L", (W, H), 0); ImageDraw.Draw (m).rounded_rectangle ((0, 0, W - 1, H - 1), 40, fill = 255)
    panel = enamel (W, H, (27, 27, 28), grain = 3.4, seed = 12, vignette = 0.35)
    img.paste (panel, (0, 0), m)
    d = ImageDraw.Draw (img)
    ink = (238, 238, 234, 255)
    lab = font (COND, 24); small = font (COND, 19); nums = font (COND_BOOK, 19); tiny = font (COND, 16)
    plate_screws (img, [(44, 70), (44, 470), (44, 870), (W - 44, 70), (W - 44, 470), (W - 44, 870)])
    big = knob (250); mid = knob (150); sel = knob (140)
    for k, c in enumerate ((640, 1980)):
        # the channel's dish: the threshold in its well, the arc meter set into its top
        d.ellipse ((c - 300, 236, c + 300, 836), fill = (17, 17, 18, 255), outline = (50, 50, 54, 255), width = 5)
        d.ellipse ((c - 236, 300, c + 236, 772), fill = (21, 21, 22, 255), outline = (40, 40, 44, 255), width = 3)
        arc_meter (img, (c, 580), 262, 382, -33, 33, value = 0.42 + 0.1 * k)
        ctext (d, c, 116, str (k + 1), font (COND, 56), ink)
        ctext (d, c, 352, "THRESHOLD", lab, ink)
        ring_labels (d, (c, 560), 168, [str (v) for v in range (0, 41, 2)], -150, 150, nums, ink, dots = True)
        put (img, big, (c, 560), 140)
        # attack, release, input, output
        for (x, y, title, labels, ang) in ((c - 310, 190, "ATTACK", ["fast", "slow"], -30), (c - 420, 470, "RELEASE", ["fast", "slow"], -20),
                                           (c - 300, 770, "INPUT", ["2", "4", "6", "8", "10", "12"], -60), (c + 300, 770, "OUTPUT", ["2", "4", "6", "8", "10", "12"], -40)):
            ctext (d, x, y - 92, title, lab, ink)
            ring_labels (d, (x, y), 84, labels, -120, 120, nums if len (labels) > 2 else small, ink, dots = len (labels) > 2)
            put (img, mid, (x, y), ang)
            if title in ("INPUT", "OUTPUT"):
                ctext (d, x + 30, y + 86, "dB", small, ink)
        # rectifier, side-chain EQs, tube bias, the VU switch
        ctext (d, c + 380, 286, "RECTIFIER", lab, ink)
        ring_labels (d, (c + 380, 186), 100, ["GERMANIUM", "LED", "SILICON", "Ge/Si"], -95, 95, tiny, ink, dots = False)
        put (img, sel, (c + 380, 186), -20)
        ctext (d, c + 430, 372, "SIDE CHAIN EQs", lab, ink)
        ring_labels (d, (c + 430, 480), 86, ["OFF", "EQ1", "EQ2", "EQ3", "EQ4", "EXT."], -100, 100, tiny, ink, dots = False)
        put (img, sel, (c + 430, 480), -90)
        toggle (img, (c + 380, 640), True, 60); ctext (d, c + 380, 596, "TUBE BIAS", small, ink)
        for j, s in enumerate (("Low", "Mid", "High")):
            ctext (d, c + 446, 618 + j * 24, s, small, ink)
        tx = c - 470 if k == 0 else c + 540
        ty = 330 if k == 0 else 290
        toggle (img, (tx, ty), True, 56)
        for j, s in enumerate (("GR", "VU 0", "VU +10")):
            ctext (d, tx - 65, ty - 24 + j * 24, s, small, ink)
    # the middle: our plates, airbass, tape roll-off, the valves' glow
    cx = W / 2
    d.rounded_rectangle ((cx - 90, 80, cx + 90, 196), 28, fill = (12, 12, 13, 255), outline = (64, 64, 68, 255), width = 4)
    ctext (d, cx, 138, "IRON", font (COND, 42), ink, spacing = 3)
    d.rounded_rectangle ((cx - 96, 290, cx + 96, 680), 12, fill = (18, 18, 19, 255), outline = (64, 64, 68, 255), width = 3)
    ctext (d, cx, 326, "IRON", font (COND, 38), ink); ctext (d, cx, 362, "MASTER COMP", font (COND, 18), ink)
    ctext (d, cx, 404, "AirBass", small, ink); toggle (img, (cx + 4, 440), True, 50); ctext (d, cx + 50, 440, "Byp", small, ink)
    ctext (d, cx, 492, "Tape Roll-Off", small, ink)
    put (img, knob (110), (cx, 556), -30)
    for j, (s1, s2) in enumerate ((("Auto", "Byp"), ("SC", "Link"), ("Host", "Sync"))):
        toggle (img, (cx - 60 + j * 60, 652), True, 42)
        ctext (d, cx - 60 + j * 60, 598, s1, font (COND, 14), ink); ctext (d, cx - 60 + j * 60, 616, s2, font (COND, 14), ink)
    d.rounded_rectangle ((cx - 130, 720, cx + 130, 840), 50, fill = (12, 12, 13, 255), outline = (64, 64, 68, 255), width = 4)
    lamp (img, (cx - 54, 780), 36); lamp (img, (cx + 54, 780), 36)
    ctext (d, cx, 890, "VALVE MASTERING COMPRESSOR", small, ink)
    return img

def main():
    os.makedirs (PREVIEW, exist_ok = True)
    for name, fn in (("hill", hill), ("child", child), ("iron", iron)):
        fn().convert ("RGB").save (os.path.join (PREVIEW, "%s_hw.png" % name))
    print ("done")

if __name__ == "__main__":
    main()
