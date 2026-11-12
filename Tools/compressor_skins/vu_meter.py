"""
A photo-realistic VU meter, drawn from scratch in our own style (no other company's artwork):
an aged, back-lit face with its printed scale, a needle with its shadow, the pivot's hood, a glass front
and a moulded bezel. Drawn at 3x and scaled down so every edge is smooth.

    vu (w, h, value, face = "cream"|"amber"|"ivory"|"night", caption = None) -> RGBA image
    value: the needle, 0 (rest, far left) .. 1 (+3 VU, far right); 0 VU is at value_of_db (0).

    python3 vu_meter.py --preview OUT.png
"""
import math, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

SS = 3
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_B = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
SERIF = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"

FACES = {
    #         paper centre      paper edge       ink            red             glow
    "cream": ((246, 236, 205), (196, 178, 136), (28, 24, 20),  (196, 38, 30),  (255, 222, 160)),
    "amber": ((255, 214, 128), (190, 120, 40),  (40, 22, 8),   (180, 24, 16),  (255, 170, 60)),
    "ivory": ((244, 243, 236), (190, 190, 182), (22, 22, 24),  (205, 40, 34),  (255, 250, 236)),
    "night": ((40, 38, 34),    (14, 13, 12),    (236, 228, 205), (240, 70, 50), (255, 200, 120)),
}

SWING = 44.0                        # degrees each side of straight up

def value_of_db (db):
    """Where a level (VU dB) sits along the needle's swing: a VU meter follows the voltage."""
    return max (0.0, min (1.0, 10 ** (db / 20.0) / 10 ** (3 / 20.0)))

def font (path, size):
    return ImageFont.truetype (path, max (6, int (size)))

def mix (a, b, t):
    return tuple (int (a[i] + (b[i] - a[i]) * t) for i in range (len (a)))

def vu (w, h, value = 0.5, face = "cream", caption = None, lamp = 1.0, seed = 7):
    W, H = w * SS, h * SS
    paper, edge, ink, red, glow = FACES[face]
    img = Image.new ("RGBA", (W, H), (0, 0, 0, 0))

    # The bezel: a moulded black frame, lit from above, with a dark recess round the window
    b = int (min (W, H) * 0.075)
    frame = Image.new ("RGBA", (W, H))
    fd = ImageDraw.Draw (frame)
    for i in range (H):
        t = i / H
        c = mix ((74, 74, 78), (14, 14, 16), t ** 0.8)
        fd.line ([(0, i), (W, i)], fill = c + (255,))
    mask = Image.new ("L", (W, H), 0)
    ImageDraw.Draw (mask).rounded_rectangle ([0, 0, W - 1, H - 1], radius = b * 1.3, fill = 255)
    img.paste (frame, (0, 0), mask)
    d = ImageDraw.Draw (img)
    d.rounded_rectangle ([2, 2, W - 3, H - 3], radius = b * 1.3, outline = (120, 120, 126, 255), width = SS)
    d.rounded_rectangle ([b * 0.45, b * 0.45, W - b * 0.45, H - b * 0.45], radius = b * 0.9, fill = (6, 6, 7, 255))

    # The window
    x0, y0, x1, y1 = b, b, W - b, H - b
    fw, fh = x1 - x0, y1 - y0
    win = Image.new ("RGBA", (fw, fh))
    px = win.load()
    cx, cy = fw / 2, fh * 0.30        # where the back light is brightest
    rnd = random.Random (seed)
    for yy in range (fh):
        for xx in range (fw):
            dx, dy = (xx - cx) / fw, (yy - cy) / fh
            t = min (1.0, math.sqrt (dx * dx * 1.6 + dy * dy * 1.3) * 1.55)
            c = mix (paper, edge, t ** 1.6)
            px[xx, yy] = c + (255,)
    # paper grain and a little age
    grain = Image.effect_noise ((fw // 2, fh // 2), 22).resize ((fw, fh), Image.BILINEAR)
    win = Image.composite (ImageChops.multiply (win, Image.merge ("RGBA", [grain.point (lambda v: 200 + v // 4)] * 3 + [Image.new ("L", (fw, fh), 255)])),
                           win, Image.new ("L", (fw, fh), 90))
    wd = ImageDraw.Draw (win)

    # The scale: an arc round the pivot, below the window
    pvx, pvy = fw / 2, fh * 1.30
    R = fh * 1.02
    def at (v, r):
        a = math.radians (-SWING + 2 * SWING * v)
        return pvx + r * math.sin (a), pvy - r * math.cos (a)
    def arc (v0, v1, r, width, colour):
        pts = [at (v0 + (v1 - v0) * k / 80, r) for k in range (81)]
        wd.line (pts, fill = colour, width = int (width), joint = "curve")

    v0 = value_of_db (0)
    arc (0.0, v0, R * 0.80, fh * 0.012, ink)
    arc (v0, 1.0, R * 0.80, fh * 0.012, ink)
    arc (v0, 1.0, R * 0.835, fh * 0.055, red)                 # the red band past 0
    marks = [(-20, "20"), (-10, "10"), (-7, "7"), (-5, "5"), (-3, "3"), (-2, "2"), (-1, "1"), (0, "0"), (1, "1"), (2, "2"), (3, "3")]
    f = font (FONT_B, fh * 0.072)
    for db, text in marks:
        v = value_of_db (db)
        hot = db > 0
        colour = red if hot else ink
        p0, p1 = at (v, R * 0.80), at (v, R * (0.92 if db in (-20, -10, -7, -5, -3, 0, 3) else 0.89))
        wd.line ([p0, p1], fill = colour, width = int (fh * 0.012))
        tx, ty = at (v, R * 0.99)
        a = -SWING + 2 * SWING * v
        label = Image.new ("RGBA", (int (fh * 0.2), int (fh * 0.14)), (0, 0, 0, 0))
        ImageDraw.Draw (label).text ((label.width / 2, label.height / 2), text, font = f, fill = colour, anchor = "mm")
        label = label.rotate (-a, resample = Image.BICUBIC, expand = True)
        win.alpha_composite (label, (int (tx - label.width / 2), int (ty - label.height / 2)))
    for k in range (11):                                     # the minor ticks and the percent scale under the arc
        v = k / 10 * value_of_db (0)
        wd.line ([at (v, R * 0.80), at (v, R * 0.76)], fill = ink, width = int (fh * 0.008))
    fs = font (FONT, fh * 0.042)
    for pc in (0, 50, 100):
        tx, ty = at (pc / 100 * value_of_db (0), R * 0.735)
        wd.text ((tx, ty), str (pc), font = fs, fill = ink, anchor = "mm")
    wd.text ((fw * 0.07, fh * 0.12), "−", font = font (FONT_B, fh * 0.12), fill = ink, anchor = "mm")
    wd.text ((fw * 0.93, fh * 0.12), "+", font = font (FONT_B, fh * 0.12), fill = red, anchor = "mm")
    wd.text ((fw / 2, fh * 0.725), "VU", font = font (SERIF, fh * 0.15), fill = ink, anchor = "mm")
    if caption:
        wd.text ((fw / 2, fh * 0.76), caption, font = font (FONT_B, fh * 0.055), fill = ink, anchor = "mm")

    # The needle and its shadow (the light comes from above and behind, so the shadow falls down and right)
    tip = at (value, R * 0.97)
    base = (pvx, pvy)
    shadow = Image.new ("RGBA", (fw, fh), (0, 0, 0, 0))
    ImageDraw.Draw (shadow).line ([(base[0] + fh * .03, base[1]), (tip[0] + fh * .03, tip[1] + fh * .035)],
                                  fill = (0, 0, 0, 110), width = int (fh * 0.016))
    win.alpha_composite (shadow.filter (ImageFilter.GaussianBlur (fh * 0.012)))
    nd = ImageDraw.Draw (win)
    nd.line ([base, tip], fill = (14, 12, 10, 255) if face != "night" else (250, 120, 60, 255), width = int (fh * 0.011))
    mid = at (value, R * 0.55)
    nd.line ([base, mid], fill = (14, 12, 10, 255) if face != "night" else (250, 120, 60, 255), width = int (fh * 0.02))

    # The pivot's hood: a black dome over the bottom of the window
    hood = Image.new ("RGBA", (fw, fh), (0, 0, 0, 0))
    hd = ImageDraw.Draw (hood)
    hr = fh * 0.20
    for i in range (int (hr), 0, -1):
        t = i / hr
        hd.ellipse ([pvx - i * 1.3, fh * 1.04 - i, pvx + i * 1.3, fh * 1.04 + i], fill = mix ((70, 68, 66), (8, 8, 9), t ** 0.6) + (255,))
    win.alpha_composite (hood.filter (ImageFilter.GaussianBlur (1)))
    # adjust screw
    sx, sy, sr = fw * 0.9, fh * 0.86, fh * 0.045
    nd.ellipse ([sx - sr, sy - sr, sx + sr, sy + sr], fill = (20, 20, 22, 255), outline = (90, 90, 96, 255), width = SS)
    nd.line ([sx - sr * .7, sy, sx + sr * .7, sy], fill = (120, 120, 126, 255), width = SS)

    # The back light (dimmer at the edges), then the glass: inner shadow from the bezel and a soft reflection
    glowl = Image.new ("RGBA", (fw, fh), glow + (0,))
    gm = Image.new ("L", (fw, fh), 0)
    ImageDraw.Draw (gm).ellipse ([fw * 0.1, -fh * 0.25, fw * 0.9, fh * 0.75], fill = int (70 * lamp))
    glowl.putalpha (gm.filter (ImageFilter.GaussianBlur (fh * 0.15)))
    win.alpha_composite (glowl)
    inner = Image.new ("L", (fw, fh), 0)
    ImageDraw.Draw (inner).rectangle ([0, 0, fw, fh], outline = 255, width = int (fh * 0.05))
    inner = inner.filter (ImageFilter.GaussianBlur (fh * 0.05))
    shade = Image.new ("RGBA", (fw, fh), (0, 0, 0, 0)); shade.putalpha (inner.point (lambda v: v * 0.75))
    win.alpha_composite (shade)
    refl = Image.new ("L", (fw, fh), 0)
    ImageDraw.Draw (refl).polygon ([(0, 0), (fw * 0.62, 0), (fw * 0.30, fh * 0.55), (0, fh * 0.55)], fill = 255)
    refl = refl.filter (ImageFilter.GaussianBlur (fh * 0.08))
    gl = Image.new ("RGBA", (fw, fh), (255, 255, 255, 0)); gl.putalpha (refl.point (lambda v: v * 0.10))
    win.alpha_composite (gl)
    ImageDraw.Draw (win).line ([(fw * 0.04, fh * 0.03), (fw * 0.96, fh * 0.03)], fill = (255, 255, 255, 50), width = SS)

    m = Image.new ("L", (fw, fh), 0)
    ImageDraw.Draw (m).rounded_rectangle ([0, 0, fw - 1, fh - 1], radius = b * 0.5, fill = 255)
    img.paste (win, (x0, y0), m)
    return img.resize ((w, h), Image.LANCZOS)

def filmstrip (w, h, frames = 64, **kw):
    """Every needle position, top to bottom (for the plug-in's meter)."""
    strip = Image.new ("RGBA", (w, h * frames))
    for k in range (frames):
        strip.paste (vu (w, h, k / (frames - 1), **kw), (0, k * h))
    return strip

if __name__ == "__main__":
    out = sys.argv[sys.argv.index ("--preview") + 1]
    tiles = []
    for face in ("cream", "amber", "ivory", "night"):
        row = [vu (420, 300, value_of_db (db), face = face) for db in (-20, -3, 0, 2.5)]
        tiles.append (row)
    sheet = Image.new ("RGB", (4 * 440 + 20, 4 * 320 + 20), (24, 24, 26))
    for r, row in enumerate (tiles):
        for c, t in enumerate (row):
            sheet.paste (t, (20 + c * 440, 20 + r * 320), t)
    sheet.save (out)
