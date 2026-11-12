#!/usr/bin/env python3
"""
Vintage Opto (Classic Compressors): its face, drawn from scratch - a grey rack panel, its scales and labels, the
amber backlit VU with the Nemix wordmark - and its moving parts, from the knob and switch art you gave
(JKnobMan renders: knob_66_black, knob_vintage_black, switch_metal, LED_Red, rendered at 3x into KNOBS).

    python3 Tools/compressor_skins/classic_opto.py KNOBS_DIR

Writes Resources/effects/copto_*.png. Positions here are the panel's pixels (1600 x 480); EffectDesigns.cpp uses
the same numbers.
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname (os.path.dirname (os.path.dirname (os.path.abspath (__file__))))
OUT = os.path.join (ROOT, "Resources", "effects")
KNOBS = sys.argv[1] if len (sys.argv) > 1 else "/tmp/claude-0/knobs/out"
W, H = 1600, 480
FONT = "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"
FONT_BOLD = "/usr/share/fonts/opentype/inter/Inter-Bold.otf"
FONT_MED = "/usr/share/fonts/opentype/inter/Inter-Medium.otf"
DARK = os.environ.get ("COPTO_DARK") == "1"   # (the black panel: a preview of how it would look)
INK = (214, 216, 220, 255) if DARK else (44, 46, 50, 255)
ACCENT = (226, 140, 38, 255)

def font (path, size): return ImageFont.truetype (path, size)

# ---- the places (shared with EffectDesigns.cpp) --------------------------------------------------------------
GAIN = (470, 250); PEAK = (1130, 250); BIG_R = 72
LIMIT_SW = (165, 300); EMPH = (275, 300)
METER = (1360, 150); POWER_SW = (1360, 318); LED = (1440, 318)
VOICE = (690, 380); BUMP = (800, 380); TEXTURE = (910, 380)
VU = (636, 62, 964, 268)   # the bezel

def vu_angle (db):
    """The needle's angle (degrees from upright) for a VU reading, -20..+3."""
    t = (10 ** (db / 20.0) - 10 ** (-1.0)) / (10 ** (3 / 20.0) - 10 ** (-1.0))
    return -48.0 + 96.0 * t

def panel():
    rng = np.random.default_rng (7)
    # brushed grey: fine horizontal grain, a soft top light
    base = np.zeros ((H, W, 3), np.float32)
    base[...] = (34, 35, 38) if DARK else (201, 203, 200)
    grain = rng.normal (0, 2.4, (H, W)).astype (np.float32)
    grain = np.array (Image.fromarray (((grain - grain.min()) / (np.ptp (grain) + 1e-6) * 255).astype (np.uint8)).filter (ImageFilter.BoxBlur (0)).resize ((W, H))).astype (np.float32) / 255.0 * 6 - 3
    streak = rng.normal (0, 1.0, (H, 1)).astype (np.float32) * 1.4
    shade = np.linspace (8, -10, H, dtype = np.float32)[:, None] * (0.6 if DARK else 1.0)
    base += (grain + streak + shade)[..., None]
    img = Image.fromarray (np.clip (base, 0, 255).astype (np.uint8)).convert ("RGBA")
    d = ImageDraw.Draw (img)
    # rack ears: a little darker, two slots each
    for x0, x1 in ((0, 70), (W - 70, W)):
        d.rectangle ((x0, 0, x1, H), fill = (24, 25, 27, 255) if DARK else (186, 188, 186, 255))
        for y in (96, 360):
            d.rounded_rectangle ((x0 + 14 if x0 == 0 else x0 + 8, y, x0 + 62 if x0 == 0 else x1 - 14, y + 24), 12, fill = (0, 0, 0, 0))
    # the plate's edges
    edge = (12, 12, 14, 255) if DARK else (150, 152, 150, 255)
    d.line ((70, 0, 70, H), fill = edge, width = 2)
    d.line ((W - 70, 0, W - 70, H), fill = edge, width = 2)
    d.line ((70, 1, W - 70, 1), fill = (70, 72, 76, 255) if DARK else (232, 233, 231, 255), width = 2)
    # the hinge along the bottom
    d.rectangle ((70, 446, W - 70, 480), fill = (22, 23, 25, 255) if DARK else (176, 178, 176, 255))
    for x in range (90, W - 90, 92):
        d.rectangle ((x, 452, x + 84, 474), fill = (40, 41, 44, 255) if DARK else (196, 198, 196, 255), outline = (14, 14, 16, 255) if DARK else (150, 152, 150, 255))
    d.line ((70, 446, W - 70, 446), fill = (10, 10, 12, 255) if DARK else (140, 142, 140, 255), width = 2)
    # screws
    for (x, y) in ((110, 28), (W - 110, 28), (110, 420), (W - 110, 420), (590, 420), (1010, 420)):
        screw (img, x, y)
    return img

def screw (img, x, y):
    d = ImageDraw.Draw (img)
    d.ellipse ((x - 11, y - 11, x + 11, y + 11), fill = (150, 152, 152, 255))
    d.ellipse ((x - 9, y - 9, x + 9, y + 9), fill = (196, 198, 198, 255))
    d.line ((x - 6, y - 6, x + 6, y + 6), fill = (90, 92, 94, 255), width = 3)
    d.line ((x - 6, y + 6, x + 6, y - 6), fill = (90, 92, 94, 255), width = 3)

def ctext (d, x, y, s, f, fill = INK, spacing = 0):
    if spacing:
        w = sum (d.textlength (ch, font = f) for ch in s) + spacing * (len (s) - 1)
        cx = x - w / 2
        for ch in s:
            d.text ((cx, y), ch, font = f, fill = fill)
            cx += d.textlength (ch, font = f) + spacing
        return
    w = d.textlength (s, font = f)
    d.text ((x - w / 2, y), s, font = f, fill = fill)

def scale_ring (d, c, r, labels, a0 = -135.0, a1 = 135.0, size = 15):
    f = font (FONT, size)
    n = len (labels)
    for i, lab in enumerate (labels):
        a = math.radians (a0 + (a1 - a0) * i / (n - 1))
        x, y = c[0] + math.sin (a) * r, c[1] - math.cos (a) * r
        tw = d.textlength (lab, font = f)
        d.text ((x - tw / 2, y - size * 0.62), lab, font = f, fill = INK)
        t0, t1 = r - 22, r - 14
        d.line ((c[0] + math.sin (a) * t0, c[1] - math.cos (a) * t0, c[0] + math.sin (a) * t1, c[1] - math.cos (a) * t1), fill = INK, width = 2)
    # the fine ticks between
    for i in range ((n - 1) * 5 + 1):
        a = math.radians (a0 + (a1 - a0) * i / ((n - 1) * 5))
        t0, t1 = r - 20, r - 16
        d.line ((c[0] + math.sin (a) * t0, c[1] - math.cos (a) * t0, c[0] + math.sin (a) * t1, c[1] - math.cos (a) * t1), fill = INK, width = 1)

def vu (img):
    d = ImageDraw.Draw (img)
    x0, y0, x1, y1 = VU
    # the bezel: dark, bevelled
    shadow = Image.new ("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw (shadow).rounded_rectangle ((x0 + 4, y0 + 8, x1 + 4, y1 + 10), 10, fill = (0, 0, 0, 110))
    img.alpha_composite (shadow.filter (ImageFilter.GaussianBlur (8)))
    d.rounded_rectangle ((x0, y0, x1, y1), 10, fill = (40, 41, 43, 255))
    d.rounded_rectangle ((x0 + 3, y0 + 3, x1 - 3, y1 - 3), 8, outline = (70, 72, 75, 255), width = 2)
    fx0, fy0, fx1, fy1 = x0 + 24, y0 + 22, x1 - 24, y1 - 22
    # the face: amber, lit from behind (brighter in the middle)
    fw, fh = fx1 - fx0, fy1 - fy0
    yy, xx = np.mgrid[0:fh, 0:fw].astype (np.float32)
    glow = np.exp (-(((xx - fw * 0.5) / (fw * 0.62)) ** 2 + ((yy - fh * 0.62) / (fh * 0.8)) ** 2))
    face = np.zeros ((fh, fw, 4), np.float32)
    face[..., 0] = 215 + 40 * glow
    face[..., 1] = 140 + 70 * glow
    face[..., 2] = 50 + 60 * glow
    face[..., 3] = 255
    fimg = Image.fromarray (np.clip (face, 0, 255).astype (np.uint8), "RGBA")
    fd = ImageDraw.Draw (fimg)
    R = 136
    pivot = (fw * 0.5, R + 44)   # (below the window: only the arc shows)
    ink = (40, 32, 24, 255); red = (200, 34, 24, 255)
    # the scale's arc: black to 0, red above
    def arc (db_from, db_to, colour, width, rr):
        pts = []
        for i in range (60):
            db = db_from + (db_to - db_from) * i / 59
            a = math.radians (vu_angle (db))
            pts.append ((pivot[0] + math.sin (a) * rr, pivot[1] - math.cos (a) * rr))
        fd.line (pts, fill = colour, width = width, joint = "curve")
    arc (-20, 0, ink, 2, R)
    arc (0, 3, red, 6, R + 2)
    f = font (FONT, 15); fs = font (FONT_MED, 10)
    for db, lab in ((-20, "20"), (-10, "10"), (-7, "7"), (-5, "5"), (-3, "3"), (-2, "2"), (-1, "1"), (0, "0"), (1, "1"), (2, "2"), (3, "3")):
        a = math.radians (vu_angle (db))
        col = red if db > 0 else ink
        fd.line ((pivot[0] + math.sin (a) * (R - 2), pivot[1] - math.cos (a) * (R - 2), pivot[0] + math.sin (a) * (R + 10), pivot[1] - math.cos (a) * (R + 10)), fill = col, width = 2)
        tx, ty = pivot[0] + math.sin (a) * (R + 22), pivot[1] - math.cos (a) * (R + 22)
        tw = fd.textlength (lab, font = f)
        fd.text ((tx - tw / 2, ty - 10), lab, font = f, fill = col)
    # the percentage scale under it
    for pc in (0, 20, 40, 60, 80, 100):
        g = pc / 100.0
        db = 20 * math.log10 (max (g, 0.1)) if pc > 0 else -20
        a = math.radians (vu_angle (db))
        tx, ty = pivot[0] + math.sin (a) * (R - 18), pivot[1] - math.cos (a) * (R - 18)
        fd.ellipse ((tx - 2, ty - 2, tx + 2, ty + 2), fill = ink)
        tw = fd.textlength (str (pc), font = fs)
        fd.text ((tx - tw / 2, ty + 3), str (pc), font = fs, fill = ink)
    fd.text ((12, fh - 40), "-", font = font (FONT_BOLD, 20), fill = ink)
    fd.text ((fw - 24, fh - 62), "+", font = font (FONT_BOLD, 20), fill = red)
    fd.text ((fw - 46, fh - 34), "VU", font = font (FONT_MED, 22), fill = ink)
    # the Nemix wordmark (the synth's logo), under the scale
    logo = Image.open (os.path.join (ROOT, "Resources", "nemix", "logo.jpg")).convert ("RGB").crop ((20, 20, 360, 110))
    arr = np.array (logo)
    import colorsys
    hsv = np.array (Image.fromarray (arr).convert ("HSV")).astype (np.float32) / 255.0
    mask = (((hsv[..., 1] > 0.45) & (hsv[..., 2] > 0.35)) | ((hsv[..., 2] > 0.86) & (hsv[..., 1] < 0.25))).astype (np.uint8) * 255
    mask[88:, :] = 0
    word = Image.fromarray (np.dstack ([arr, mask]), "RGBA").filter (ImageFilter.SMOOTH)
    ww = 104
    word = word.resize ((ww, int (ww * word.size[1] / word.size[0])), Image.LANCZOS)
    fimg.alpha_composite (word, (int (fw / 2 - ww / 2), int (fh - word.size[1] - 18)))
    # glass on the window
    sheen = Image.new ("RGBA", (fw, fh), (0, 0, 0, 0))
    ImageDraw.Draw (sheen).polygon (((0, 0), (fw * 0.55, 0), (fw * 0.3, fh), (0, fh)), fill = (255, 255, 255, 22))
    fimg.alpha_composite (sheen)
    img.alpha_composite (fimg, (fx0, fy0))
    d.rectangle ((fx0, fy0, fx1, fy1), outline = (25, 25, 27, 255), width = 2)
    return (fx0, fy0, fx1, fy1, pivot, R)

def labels (img):
    d = ImageDraw.Draw (img)
    # the title: our own, with the accent rule
    d.text ((110, 54), "VINTAGE OPTO", font = font (FONT_BOLD, 36), fill = INK)
    d.rectangle ((112, 102, 116, 124), fill = ACCENT)
    d.text ((126, 104), "OPTICAL LEVELLING AMPLIFIER", font = font (FONT_MED, 14), fill = (150, 152, 158, 255) if DARK else (70, 72, 76, 255))
    f = font (FONT, 14); fsm = font (FONT, 12)
    scale_ring (d, GAIN, 104, [str (v) for v in range (0, 101, 10)])
    scale_ring (d, PEAK, 104, [str (v) for v in range (0, 101, 10)])
    ctext (d, GAIN[0], 372, "GAIN", f, spacing = 2)
    ctext (d, PEAK[0], 372, "PEAK REDUCTION", f, spacing = 2)
    ctext (d, LIMIT_SW[0], 236, "LIMIT", fsm, spacing = 1)
    ctext (d, LIMIT_SW[0], 352, "COMPRESS", fsm, spacing = 1)
    ctext (d, EMPH[0], 248, "HF", fsm, spacing = 1)
    ctext (d, EMPH[0], 352, "EMPHASIS", fsm, spacing = 1)
    ctext (d, METER[0], 226, "METER", f, spacing = 2)
    for a, s in ((-48, "+10"), (0, "GR"), (48, "+4")):
        r = 78
        x, y = METER[0] + math.sin (math.radians (a)) * r, METER[1] - math.cos (math.radians (a)) * r
        ctext (d, x, y - 8, s, fsm)
    ctext (d, POWER_SW[0], 254, "ON", fsm, spacing = 1)
    ctext (d, POWER_SW[0], 370, "POWER", fsm, spacing = 1)
    # the colour section: a recessed strip under the meter
    x0, y0, x1, y1 = 626, 296, 974, 440
    d.rounded_rectangle ((x0, y0, x1, y1), 12, fill = (24, 25, 27, 255) if DARK else (190, 192, 189, 255), outline = (60, 62, 66, 255) if DARK else (160, 162, 160, 255), width = 2)
    d.line ((x0 + 10, y0 + 2, x1 - 10, y0 + 2), fill = (14, 14, 16, 255) if DARK else (172, 174, 172, 255), width = 2)
    ctext (d, 800, 304, "COLOUR", font (FONT, 11), fill = (140, 142, 148, 255) if DARK else (96, 98, 102, 255), spacing = 3)
    ft = font (FONT_MED, 10)
    for a, s in ((-120, "OFF"), (-60, "CLEAN"), (0, "OPTO"), (60, "VARI-MU")):
        r = 54
        x, y = VOICE[0] + math.sin (math.radians (a)) * r, VOICE[1] - math.cos (math.radians (a)) * r
        ctext (d, x, y - 6, s, ft)
    for a, s in ((-135, "0"), (0, "+3"), (135, "+6")):
        r = 46
        x, y = BUMP[0] + math.sin (math.radians (a)) * r, BUMP[1] - math.cos (math.radians (a)) * r
        ctext (d, x, y - 6, s, ft)
    for a, s in ((-62, "WARM"), (0, "OFF"), (62, "BRIGHT")):
        r = 54
        x, y = TEXTURE[0] + math.sin (math.radians (a)) * r, TEXTURE[1] - math.cos (math.radians (a)) * r
        ctext (d, x, y - 6, s, ft)
    for c, s in ((VOICE, "TUBE VOICE"), (BUMP, "LOW BUMP"), (TEXTURE, "TEXTURE")):
        ctext (d, c[0], 420, s, font (FONT, 11), spacing = 1)

def frame (path, index, size):
    s = Image.open (path).convert ("RGBA")
    w = s.size[0]
    return s.crop ((0, index * w, w, (index + 1) * w))

def main():
    os.makedirs (OUT, exist_ok = True)
    img = panel()
    labels (img)
    vu (img)
    img.save (os.environ.get ("COPTO_PANEL_OUT", os.path.join (OUT, "copto_panel.png")))
    k = KNOBS
    frame (os.path.join (k, "knob_66_black_x3.png"), 4, 198).save (os.path.join (OUT, "copto_knob_big.png"))
    frame (os.path.join (k, "knob_vintage_black_x3.png"), 2, 288).save (os.path.join (OUT, "copto_knob_small.png"))
    sw = Image.open (os.path.join (k, "switch_metal_x3.png")).convert ("RGBA")
    sw.crop ((0, 0, 192, 192)).save (os.path.join (OUT, "copto_switch_up.png"))
    sw.crop ((0, 192, 192, 384)).save (os.path.join (OUT, "copto_switch_down.png"))
    led = Image.open (os.path.join (k, "LED_Red_x3.png")).convert ("RGBA")
    led.crop ((0, 0, 192, 192)).save (os.path.join (OUT, "copto_led_off.png"))
    led.crop ((0, 192, 192, 384)).save (os.path.join (OUT, "copto_led_on.png"))
    print ("wrote", OUT)

if __name__ == "__main__":
    main()
