#!/usr/bin/env python3
"""
Classic Compressors: the faces of the hardware-style compressors and channel strips, drawn from scratch (no maker's
artwork or names), in four skins - Glass Dark (smoked glass, amber), Glass Silver (frosted grey glass, blue), Metal
Grey and Metal Black (brushed rack panels). Half & Half is drawn by the app from the two glass skins.

Every face shares one kit: the black compression meter (cut from the bus compressor's photo), its knobs (knob_66_black,
knob_vintage_black), the metal switches and the red power lamp (JKnobMan renders at 3x, in KNOBS), a meter selector,
power, and the Colour section (tube voice, low bump, texture) under the meter.

    python3 Tools/compressor_skins/classic_faces.py KNOBS_DIR [--preview OUT_DIR]

Writes Resources/effects/classic_<face>_<skin>.jpg, the shared sprites (classic_*.png), and Source/ui/ClassicLayouts.h
(where everything sits, for EffectDesigns.cpp).
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname (os.path.dirname (os.path.dirname (os.path.abspath (__file__))))
OUT = os.path.join (ROOT, "Resources", "effects")
KNOBS = sys.argv[1] if len (sys.argv) > 1 and not sys.argv[1].startswith ("--") else "/tmp/claude-0/knobs/out"
PREVIEW = sys.argv[sys.argv.index ("--preview") + 1] if "--preview" in sys.argv else None
H = 480
FONT = "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"
FONT_BOLD = "/usr/share/fonts/opentype/inter/Inter-Bold.otf"
FONT_MED = "/usr/share/fonts/opentype/inter/Inter-Medium.otf"
def font (path, size): return ImageFont.truetype (path, size)

BIG, SEL, SMALL, SWITCH = 168, 104, 84, 96      # how big each part is drawn (panel pixels)
BIG_ANGLE, SMALL_ANGLE = -12.0, 0.0             # where the knob pictures point as rendered
METER_W, METER_H = 236, 206                     # the meter (its photo scaled to this)
METER_SRC = (61, 38, 270, 220)                  # where it is in vvca_panel.jpg
BACKDROP = (20, 21, 24)                         # (what shows through the rack slots and round the corners)

def steps (n):
    """A switch's settings: 45 degrees apart, centred."""
    return [(-45.0 * (n - 1) / 2) + 45.0 * i for i in range (n)]

# ---- the faces --------------------------------------------------------------------------------------------------
# Items: ("big"|"sel"|"small"|"switch", param id, x, y, labels, caption[, angles])
#   big: a 0..10-style scale round it; small: labels at the ends (and middle); sel: one label per setting;
#   switch: labels above (on / its second setting) and below
TEN = [str (v) for v in range (0, 11)]
HUNDRED = [str (v) for v in range (0, 101, 10)]
FACES = [
    dict (key = "opto", type = "classicOpto", title = "VINTAGE OPTO 2A", sub = "OPTICAL LEVELLING AMPLIFIER", w = 1600, mx = 800, items = [
        ("switch", "limit", 165, 300, ("LIMIT", "COMPRESS"), ""),
        ("small", "emphasis", 290, 300, ("FLAT", "", "HF"), "HF EMPHASIS"),
        ("big", "gain", 500, 250, HUNDRED, "GAIN"),
        ("big", "peak", 1100, 250, HUNDRED, "PEAK REDUCTION"),
    ]),
    dict (key = "fet", tint = ((240, 150, 70), (196, 98, 34)), type = "classicFet", title = "VINTAGE FET", sub = "FET LIMITING AMPLIFIER", w = 1800, mx = 850, items = [
        ("small", "attack", 195, 230, ("1", "4", "7"), "ATTACK"),
        ("small", "release", 195, 395, ("1", "4", "7"), "RELEASE"),
        ("big", "input", 450, 250, TEN, "INPUT"),
        ("big", "output", 1180, 250, TEN, "OUTPUT"),
        ("sel", "ratio", 1395, 250, ("4", "8", "12", "20", "ALL"), "RATIO"),
    ]),
    dict (key = "vca", tint = ((44, 70, 138), (16, 28, 68)), type = "classicVca", title = "BUS VCA", sub = "STEREO BUS COMPRESSOR", w = 1900, mx = 950, items = [
        ("sel", "ratio", 205, 230, ("2", "4", "10"), "RATIO"),
        ("sel", "attack", 360, 230, (".1", ".3", "1", "3", "10", "30"), "ATTACK ms"),
        ("sel", "release", 205, 395, (".1", ".3", ".6", "1.2", "AUTO"), "RELEASE s"),
        ("sel", "scHpf", 360, 395, ("OFF", "60", "90", "150"), "S/C HPF"),
        ("big", "threshold", 620, 250, ("-20", "-15", "-10", "-5", "0", "+5", "+10", "+15", "+20"), "THRESHOLD"),
        ("big", "makeup", 1280, 250, ("0", "2", "4", "6", "8", "10", "12", "14", "16", "18", "20"), "MAKE-UP"),
    ]),
    dict (key = "punch", tint = ((182, 216, 242), (118, 164, 210)), type = "classicPunch", title = "PUNCH BUS 2500", sub = "FEED-FORWARD / FEEDBACK BUS COMPRESSOR", w = 1900, mx = 950, items = [
        ("sel", "attack", 205, 230, (".03", ".1", ".3", "1", "3", "10", "30"), "ATTACK ms"),
        ("sel", "ratio", 360, 230, ("1.5", "2", "3", "4", "6", "10", "INF"), "RATIO"),
        ("sel", "release", 205, 395, (".05", ".1", ".2", ".5", "1", "2"), "RELEASE s"),
        ("sel", "knee", 360, 395, ("HARD", "MED", "SOFT"), "KNEE"),
        ("big", "threshold", 620, 250, ("-20", "-15", "-10", "-5", "0", "+5", "+10"), "THRESHOLD"),
        ("big", "makeup", 1280, 250, ("0", "2", "4", "6", "8", "10", "12", "14", "16", "18", "20"), "OUTPUT"),
        ("sel", "thrust", 1495, 225, ("NORM", "MED", "LOUD"), "THRUST"),
        ("switch", "type", 1495, 400, ("OLD", "NEW"), ""),
    ]),
    dict (key = "vox", tint = ((134, 84, 186), (70, 38, 112)), type = "classicVox", title = "VOX TUBE STRIP", sub = "TUBE PREAMP  ·  OPTO  ·  DE-ESSER  ·  EQ  ·  LIMITER", w = 2000, mx = 1000, items = [
        ("small", "low", 195, 230, ("-10", "0", "+10"), "LOW 80"),
        ("small", "mid", 335, 230, ("-10", "0", "+10"), "MID"),
        ("sel", "midFreq", 475, 230, (".5k", "1k", "2k", "3k", "5k"), "MID FREQ"),
        ("small", "high", 195, 395, ("-10", "0", "+10"), "HIGH 10k"),
        ("small", "deEss", 335, 395, ("0", "", "10"), "DE-ESS"),
        ("sel", "deEssFreq", 475, 395, ("3k", "5k", "7k", "9k"), "S FREQ"),
        ("big", "drive", 690, 250, TEN, "TUBE DRIVE"),
        ("big", "comp", 1300, 250, TEN, "COMPRESS"),
        ("sel", "ratio", 1515, 230, ("2", "3", "4", "6", "10"), "RATIO"),
        ("small", "limit", 1515, 395, ("OFF", "", "MAX"), "LIMIT"),
        ("small", "output", 1632, 395, ("-12", "0", "+12"), "OUTPUT"),
    ]),
    dict (key = "tube", tint = ((242, 214, 92), (204, 162, 38)), type = "classicTube", title = "CLASS A TUBE STRIP 737", sub = "PREAMP  ·  OPTICAL COMPRESSOR  ·  EQUALIZER", w = 2150, mx = 1060, items = [
        ("sel", "lowFreq", 195, 230, ("15", "30", "60", "150"), "LOW Hz"),
        ("sel", "midFreq", 335, 230, ("220", "500", "1.5k", "3k", "6k"), "MID Hz"),
        ("sel", "highFreq", 475, 230, ("5k", "10k", "20k", "32k"), "HIGH Hz"),
        ("small", "low", 195, 395, ("-24", "0", "+24"), "LOW"),
        ("small", "mid", 335, 395, ("-16", "0", "+16"), "MID"),
        ("small", "high", 475, 395, ("-20", "0", "+20"), "HIGH"),
        ("big", "gain", 740, 250, TEN, "TUBE GAIN"),
        ("big", "threshold", 1380, 250, ("-30", "-25", "-20", "-15", "-10", "-5", "0", "+5", "+10"), "THRESHOLD"),
        ("sel", "attack", 1600, 230, ("FAST", "MED", "SLOW"), "ATTACK"),
        ("small", "ratio", 1745, 230, ("2", "", "20"), "RATIO"),
        ("sel", "release", 1600, 395, ("FAST", "MED", "SLOW", "AUTO"), "RELEASE"),
        ("small", "output", 1745, 395, ("-30", "0", "+10"), "OUTPUT"),
    ]),
    dict (key = "dual", tint = ((196, 44, 44), (108, 16, 22)), type = "classicDual", title = "DUAL COMP 6176", sub = "TUBE PREAMP  ·  FET  &  OPTO COMPRESSORS", w = 2250, mx = 1080, items = [
        ("small", "low", 195, 230, ("-12", "0", "+12"), "LOW 100"),
        ("small", "high", 195, 395, ("-12", "0", "+12"), "HIGH 10k"),
        ("big", "preamp", 445, 250, TEN, "TUBE GAIN"),
        ("big", "fetInput", 790, 250, TEN, "FET INPUT"),
        ("big", "optoPeak", 1370, 250, HUNDRED, "OPTO PEAK"),
        ("sel", "mode", 1590, 230, ("FET", "BOTH", "OPTO"), "MODE"),
        ("sel", "ratio", 1735, 230, ("4", "8", "12", "20", "ALL"), "FET RATIO"),
        ("small", "attack", 1590, 395, ("1", "4", "7"), "ATTACK"),
        ("small", "release", 1735, 395, ("1", "4", "7"), "RELEASE"),
        ("small", "output", 1880, 230, ("-20", "0", "+20"), "OUTPUT"),
    ]),
]

def common (face):
    """Every face's: the meter selector, power and its lamp (right), the Colour section under the meter."""
    w, mx = face["w"], face["mx"]
    return [
        ("sel", "meterMode", w - 290, 150, ("+10", "GR", "+4"), "METER"),
        ("small", "mix", w - 155, 150, ("DRY", "", "WET"), "MIX"),
        ("switch", "power", w - 290, 318, ("ON", "POWER"), ""),
        ("sel", "voice", mx - 115, 375, ("OFF", "CLEAN", "OPTO", "VARI-MU"), "TUBE VOICE"),
        ("small", "lowBump", mx + 5, 375, ("0", "+3", "+6"), "LOW BUMP"),
        ("small", "texture", mx + 120, 375, ("WARM", "OFF", "BRIGHT"), "TEXTURE", [-60.0, 0.0, 60.0]),
    ]

def items_of (face):
    return face["items"] + common (face)

# ---- drawing ----------------------------------------------------------------------------------------------------
class Skin:
    def __init__ (self, key, tint = None):
        self.key = key
        self.tint = tint                       # "gc": the face's own coloured glass (Half & Half's left side)
        self.glass = key in ("gd", "gs", "gc")
        lum = (0.3 * tint[0][0] + 0.59 * tint[0][1] + 0.11 * tint[0][2]) if tint else 0
        self.dark = key in ("gd", "mb") or (key == "gc" and lum < 150)
        self.ink = (230, 231, 235, 255) if self.dark and key != "mb" else (28, 30, 36, 255) if key in ("gs", "gc") else (214, 216, 220, 255) if key == "mb" else (44, 46, 50, 255)
        self.dim = ((222, 222, 230, 255) if key == "gc" else (150, 152, 158, 255)) if self.dark else ((50, 46, 44, 255) if key == "gc" else (88, 92, 100, 255))
        self.accent = (47, 124, 246, 255) if key == "gs" else (242, 164, 58, 255) if key == "gd" else self.ink if key == "gc" else (226, 140, 38, 255)

def screw (img, x, y):
    d = ImageDraw.Draw (img)
    d.ellipse ((x - 11, y - 11, x + 11, y + 11), fill = (150, 152, 152, 255))
    d.ellipse ((x - 9, y - 9, x + 9, y + 9), fill = (196, 198, 198, 255))
    d.line ((x - 6, y - 6, x + 6, y + 6), fill = (90, 92, 94, 255), width = 3)
    d.line ((x - 6, y + 6, x + 6, y - 6), fill = (90, 92, 94, 255), width = 3)

def slots (img, W, sk):
    d = ImageDraw.Draw (img)
    for x0, x1 in ((14, 62), (W - 62, W - 14)):
        for y in (96, 360):
            d.rounded_rectangle ((x0, y, x1, y + 24), 12, fill = BACKDROP + (255,))
            rim = Image.new ("RGBA", img.size, (0, 0, 0, 0))
            rd = ImageDraw.Draw (rim)
            rd.rounded_rectangle ((x0 - 2, y - 2, x1 + 2, y + 26), 14, outline = (255, 255, 255, 60) if sk.dark else (255, 255, 255, 230), width = 2)
            rd.arc ((x0 - 2, y - 2, x0 + 26, y + 26), 90, 270, fill = (0, 0, 0, 160) if sk.dark else (90, 100, 116, 120), width = 2)
            rd.line ((x0 + 12, y - 1, x1 - 12, y - 1), fill = (0, 0, 0, 160) if sk.dark else (90, 100, 116, 120), width = 2)
            img.alpha_composite (rim)

def metal_panel (W, sk):
    rng = np.random.default_rng (7)
    base = np.zeros ((H, W, 3), np.float32)
    base[...] = (34, 35, 38) if sk.dark else (201, 203, 200)
    grain = rng.normal (0, 1.0, (H, W)).astype (np.float32) * 1.5
    streak = rng.normal (0, 1.0, (H, 1)).astype (np.float32) * 1.4
    shade = np.linspace (8, -10, H, dtype = np.float32)[:, None] * (0.6 if sk.dark else 1.0)
    base += (grain + streak + shade)[..., None]
    img = Image.fromarray (np.clip (base, 0, 255).astype (np.uint8)).convert ("RGBA")
    d = ImageDraw.Draw (img)
    for x0, x1 in ((0, 70), (W - 70, W)):
        d.rectangle ((x0, 0, x1, H), fill = (24, 25, 27, 255) if sk.dark else (186, 188, 186, 255))
    edge = (12, 12, 14, 255) if sk.dark else (150, 152, 150, 255)
    d.line ((70, 0, 70, H), fill = edge, width = 2)
    d.line ((W - 70, 0, W - 70, H), fill = edge, width = 2)
    d.line ((70, 1, W - 70, 1), fill = (70, 72, 76, 255) if sk.dark else (232, 233, 231, 255), width = 2)
    d.line ((70, H - 2, W - 70, H - 2), fill = edge, width = 2)
    slots (img, W, sk)
    for (x, y) in ((110, 28), (W - 110, 28), (110, 452), (W - 110, 452)):
        screw (img, x, y)
    return img

def glass_panel (W, sk):
    dark = sk.dark
    img = Image.new ("RGBA", (W, H), BACKDROP + (255,))
    yy, xx = np.mgrid[0:H, 0:W].astype (np.float32)
    t = (xx / W * 0.35 + yy / H * 0.65)
    if sk.tint: top, bottom = np.array (sk.tint[0], np.float32), np.array (sk.tint[1], np.float32)
    elif sk.key == "gd": top, bottom = np.array ([44, 44, 48], np.float32), np.array ([14, 14, 16], np.float32)
    else:    top, bottom = np.array ([222, 224, 226], np.float32), np.array ([184, 187, 191], np.float32)
    body = top[None, None, :] * (1 - t[..., None]) + bottom[None, None, :] * t[..., None]
    band = np.clip (1.0 - np.abs ((xx / W - yy / H * 0.55) - 0.12) / 0.18, 0, 1) ** 2
    body += band[..., None] * (14 if dark else 10)
    acc = np.array (sk.accent[:3], np.float32)
    glow = np.exp (-(((xx - W * 0.5) / (W * 0.33)) ** 2 + ((yy - 470) / 260) ** 2))
    if not sk.tint:
        body = body * (1 - glow[..., None] * 0.10) + acc[None, None, :] * glow[..., None] * 0.10
    slab = Image.fromarray (np.dstack ([np.clip (body, 0, 255).astype (np.uint8), np.full ((H, W), 255, np.uint8)]), "RGBA")
    mask = Image.new ("L", (W, H), 0)
    ImageDraw.Draw (mask).rounded_rectangle ((2, 2, W - 3, H - 3), 34, fill = 255)
    img.paste (slab, (0, 0), mask)
    # rack ears: a slightly deeper pane each end, a seam of light between
    ear = Image.new ("RGBA", (W, H), (0, 0, 0, 0))
    ed = ImageDraw.Draw (ear)
    for x0, x1 in ((0, 70), (W - 70, W)):
        ed.rectangle ((x0, 0, x1, H), fill = (0, 0, 0, 60) if dark else (120, 132, 150, 26))
    ear.putalpha (Image.fromarray (np.minimum (np.array (ear.getchannel ("A")), np.array (mask))))
    img.alpha_composite (ear)
    d = ImageDraw.Draw (img)
    d.rounded_rectangle ((2, 2, W - 3, H - 3), 34, outline = (255, 255, 255, 46 if dark else 230), width = 2)
    d.line ((36, 3, W - 36, 3), fill = (255, 255, 255, 70 if dark else 255), width = 2)
    d.line ((36, H - 4, W - 36, H - 4), fill = (0, 0, 0, 120 if dark else 40), width = 2)
    for x in (70, W - 70):
        d.line ((x, 4, x, H - 5), fill = (0, 0, 0, 150) if dark else (150, 160, 176, 120), width = 2)
        d.line ((x + 2, 4, x + 2, H - 5), fill = (255, 255, 255, 40) if dark else (255, 255, 255, 255), width = 1)
    slots (img, W, sk)
    for (x, y) in ((110, 28), (W - 110, 28), (110, 452), (W - 110, 452)):
        screw (img, x, y)
    return img

def ctext (d, x, y, s, f, fill, spacing = 0):
    if spacing:
        w = sum (d.textlength (ch, font = f) for ch in s) + spacing * (len (s) - 1)
        cx = x - w / 2
        for ch in s:
            d.text ((cx, y), ch, font = f, fill = fill)
            cx += d.textlength (ch, font = f) + spacing
        return
    w = d.textlength (s, font = f)
    d.text ((x - w / 2, y), s, font = f, fill = fill)

def polar (c, r, a): return c[0] + math.sin (math.radians (a)) * r, c[1] - math.cos (math.radians (a)) * r

def scale_ring (d, c, r, labels, ink, a0 = -135.0, a1 = 135.0, size = 15):
    f = font (FONT, size)
    n = len (labels)
    for i, lab in enumerate (labels):
        a = a0 + (a1 - a0) * i / (n - 1)
        x, y = polar (c, r, a)
        ctext (d, x, y - size * 0.62, lab, f, ink)
        d.line (polar (c, r - 22, a) + polar (c, r - 14, a), fill = ink, width = 2)
    for i in range ((n - 1) * 5 + 1):
        a = a0 + (a1 - a0) * i / ((n - 1) * 5)
        d.line (polar (c, r - 20, a) + polar (c, r - 16, a), fill = ink, width = 1)

def angles_of (item):
    kind, labels = item[0], item[4]
    if len (item) > 6: return item[6]
    if kind == "sel": return steps (len (labels))
    if kind in ("big", "small"):
        return [-135.0, 135.0]
    return []

def draw_items (img, face, sk):
    d = ImageDraw.Draw (img)
    fcap = font (FONT, 14); fsm = font (FONT, 12); ft = font (FONT_MED, 11)
    for it in items_of (face):
        kind, pid, x, y, labels, caption = it[:6]
        if kind == "big":
            scale_ring (d, (x, y), 104, list (labels), sk.ink)
            ctext (d, x, y + 122, caption, fcap, sk.ink, spacing = 2)
        elif kind == "sel":
            r = 58 if it[1] != "meterMode" else 72
            angs = angles_of (it)
            for a, s in zip (angs, labels):
                px, py = polar ((x, y), r, a)
                ctext (d, px, py - 7, s, ft, sk.ink)
                d.line (polar ((x, y), 44, a) + polar ((x, y), 49, a), fill = sk.ink, width = 2)
            ctext (d, x, y + (66 if pid != "meterMode" else 76), caption, fsm if pid in ("voice",) else fcap if pid == "meterMode" else fsm, sk.ink, spacing = 1)
        elif kind == "small":
            angs = angles_of (it)
            r = 54
            if len (angs) == 2:
                spots = [(-135, labels[0]), (0, labels[1]), (135, labels[2])]
            else:
                spots = list (zip (angs, labels))
            for a, s in spots:
                if not s: continue
                px, py = polar ((x, y), r, a)
                ctext (d, px, py - 7, s, ft, sk.ink)
            ctext (d, x, y + 50, caption, fsm, sk.ink, spacing = 1)
        elif kind == "switch":
            ctext (d, x, y - 64, labels[0], fsm, sk.ink, spacing = 1)
            ctext (d, x, y + 52, labels[1], fsm, sk.ink, spacing = 1)

def spots_of (face):
    """Where each printed setting sits, and the knob's position (0..1) it stands for: a click on it turns the
    knob there."""
    out = []
    for it in items_of (face):
        kind, pid, x, y, labels = it[:5]
        if kind == "big":
            n = len (labels)
            for i in range (n):
                px, py = polar ((x, y), 104, -135.0 + 270.0 * i / (n - 1))
                out.append ((pid, px, py, i / (n - 1)))
        elif kind == "sel":
            r = 58 if pid != "meterMode" else 72
            n = len (labels)
            for i, a in enumerate (angles_of (it)):
                px, py = polar ((x, y), r, a)
                out.append ((pid, px, py, i / (n - 1)))
        elif kind == "small":
            angs = angles_of (it)
            places = [(-135, 0.0), (0, 0.5), (135, 1.0)] if len (angs) == 2 else [(a, i / (len (angs) - 1)) for i, a in enumerate (angs)]
            for (a, prop), lab in zip (places, labels):
                if lab:
                    px, py = polar ((x, y), 54, a)
                    out.append ((pid, px, py, prop))
        elif kind == "switch":
            out.append ((pid, x, y - 58, 1.0))
            out.append ((pid, x, y + 58, 0.0))
    return out

def well (img, face, sk):
    mx = face["mx"]
    x0, y0, x1, y1 = mx - 190, 292, mx + 190, 456
    if sk.glass:
        w_ = Image.new ("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw (w_).rounded_rectangle ((x0, y0, x1, y1), 18, fill = (0, 0, 0, 80) if sk.dark else (255, 255, 255, 55),
                                               outline = (255, 255, 255, 36) if sk.dark else (0, 0, 0, 26), width = 2)
        img.alpha_composite (w_)
        d = ImageDraw.Draw (img)
        ctext (d, mx, 299, "COLOUR", font (FONT, 11), sk.accent, spacing = 3)
    else:
        d = ImageDraw.Draw (img)
        d.rounded_rectangle ((x0, y0, x1, y1), 12, fill = (24, 25, 27, 255) if sk.dark else (190, 192, 189, 255), outline = (60, 62, 66, 255) if sk.dark else (160, 162, 160, 255), width = 2)
        ctext (d, mx, 299, "COLOUR", font (FONT, 11), (140, 142, 148, 255) if sk.dark else (96, 98, 102, 255), spacing = 3)

def meter_block():
    src = Image.open (os.path.join (OUT, "vvca_panel.jpg")).convert ("RGBA").crop (METER_SRC)
    blk = src.resize ((METER_W, METER_H), Image.LANCZOS)
    m = Image.new ("L", blk.size, 0)
    ImageDraw.Draw (m).rounded_rectangle ((0, 0, METER_W - 1, METER_H - 1), 14, fill = 255)
    blk.putalpha (m)
    return blk

def meter (img, face, sk, blk):
    mx = face["mx"]
    x0 = mx - METER_W // 2
    sh = Image.new ("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw (sh).rounded_rectangle ((x0 + 4, 70, x0 + METER_W + 4, 62 + METER_H + 10), 14, fill = (0, 0, 0, 110 if sk.dark else 70))
    img.alpha_composite (sh.filter (ImageFilter.GaussianBlur (12)))
    img.alpha_composite (blk, (x0, 62))
    ImageDraw.Draw (img).rounded_rectangle ((x0, 62, x0 + METER_W - 1, 62 + METER_H - 1), 14,
                                             outline = (255, 255, 255, 50) if sk.dark else (255, 255, 255, 200), width = 2)

def title (img, face, sk):
    d = ImageDraw.Draw (img)
    d.text ((116, 54), face["title"], font = font (FONT_BOLD, 34), fill = sk.ink)
    d.rectangle ((118, 100, 122, 120), fill = sk.accent)
    d.text ((132, 102), face["sub"], font = font (FONT_MED, 13), fill = sk.dim)

def render (face, skin, blk):
    sk = Skin (skin, face.get ("tint") if skin == "gc" else None)
    W = face["w"]
    img = glass_panel (W, sk) if sk.glass else metal_panel (W, sk)
    title (img, face, sk)
    well (img, face, sk)
    meter (img, face, sk, blk)
    draw_items (img, face, sk)
    return img

# ---- the sprites, and placing them for a preview ----------------------------------------------------------------
def frame (path, index):
    s = Image.open (path).convert ("RGBA")
    w = s.size[0]
    return s.crop ((0, index * w, w, (index + 1) * w))

def sprites():
    k = KNOBS
    out = {
        "big": frame (os.path.join (k, "knob_66_black_x3.png"), 4),
        "small": frame (os.path.join (k, "knob_vintage_black_x3.png"), 2),
    }
    sw = Image.open (os.path.join (k, "switch_metal_x3.png")).convert ("RGBA")
    out["switch_up"] = sw.crop ((0, 0, 192, 192)); out["switch_down"] = sw.crop ((0, 192, 192, 384))
    led = Image.open (os.path.join (k, "LED_Red_x3.png")).convert ("RGBA")
    out["led_off"] = led.crop ((0, 0, 192, 192)); out["led_on"] = led.crop ((0, 192, 192, 384))
    return out

# The needle: its pivot and reach in the meter's own pixels (as photographed), and 0..20 dB across
NEEDLE_PIVOT = (101.44, 123.35); NEEDLE_R = 86.0; NEEDLE_WINDOW = (12.0, 10.0, 199.0, 97.0)
def needle_angle (gr): return -37.45 + 3.8445 * gr

def place (img, face, sp, gr = 4.0):
    def put (im, c, size, ang = 0.0):
        im = im.rotate (-ang, resample = Image.BICUBIC).resize ((size, size), Image.LANCZOS)
        img.alpha_composite (im, (int (c[0] - size / 2), int (c[1] - size / 2)))
    for it in items_of (face):
        kind, pid, x, y = it[:4]
        angs = angles_of (it)
        a = angs[len (angs) // 2] if len (angs) > 2 else -40.0 if kind != "switch" else 0
        if kind == "big": put (sp["big"], (x, y), BIG, a - BIG_ANGLE)
        elif kind == "sel": put (sp["big"], (x, y), SEL, a - BIG_ANGLE)
        elif kind == "small": put (sp["small"], (x, y), SMALL, a - SMALL_ANGLE)
        elif kind == "switch": put (sp["switch_up"], (x, y), SWITCH)
    put (sp["led_on"], (face["w"] - 215, 318), 56)
    # the needle
    s = METER_W / (METER_SRC[2] - METER_SRC[0])
    x0 = face["mx"] - METER_W // 2
    px, py = x0 + NEEDLE_PIVOT[0] * s, 62 + NEEDLE_PIVOT[1] * s
    a = math.radians (needle_angle (gr))
    lay = Image.new ("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw (lay).line ((px, py, px + math.sin (a) * NEEDLE_R * s, py - math.cos (a) * NEEDLE_R * s), fill = (245, 245, 240, 255), width = 2)
    m = Image.new ("L", img.size, 0)
    wx0, wy0, wx1, wy1 = NEEDLE_WINDOW
    ImageDraw.Draw (m).rectangle ((x0 + wx0 * s, 62 + wy0 * s, x0 + wx1 * s, 62 + wy1 * s), fill = 255)
    lay.putalpha (Image.fromarray (np.minimum (np.array (lay.getchannel ("A")), np.array (m))))
    img.alpha_composite (lay)

def seam (face):
    """Half & Half: where the diagonal crosses the top and the bottom (behind the meter)."""
    return face["mx"] + 80, face["mx"] - 160

def half_and_half (face, dark, light):
    t, b = seam (face)
    W = face["w"]
    out = light.copy()
    m = Image.new ("L", out.size, 0)
    ImageDraw.Draw (m).polygon (((t, 0), (W, 0), (W, H), (b, H)), fill = 255)
    out.paste (dark, (0, 0), m)
    ImageDraw.Draw (out).line ((t, 0, b, H), fill = (255, 255, 255, 120), width = 2)
    # the Colour section and the meter stay whole (dark), the seam passing behind them
    mx = face["mx"]
    m = Image.new ("L", out.size, 0)
    ImageDraw.Draw (m).rounded_rectangle ((mx - 192, 290, mx + 192, 458), 20, fill = 255)
    x0 = mx - METER_W // 2
    ImageDraw.Draw (m).rounded_rectangle ((x0 - 6, 56, x0 + METER_W + 10, 62 + METER_H + 14), 18, fill = 255)
    out.paste (dark, (0, 0), m.filter (ImageFilter.GaussianBlur (1.5)))
    return out

def header():
    """ClassicLayouts.h: where every part sits, for EffectDesigns.cpp."""
    kinds = { "big": "big", "sel": "selector", "small": "small", "switch": "toggle" }
    lines = ["#pragma once", "// Generated by Tools/compressor_skins/classic_faces.py - edit the faces there.", "#include <vector>",
             "namespace riff::classicfaces", "{",
             "    enum class Part { big, selector, small, toggle };",
             "    struct Item { const char* id; Part part; float x, y; std::vector<float> angles; };",
             "    struct Spot { const char* id; float x, y, position; };   // (a printed setting: a click there turns the knob to it)",
             "    struct Face { const char* type; const char* key; int width; float meterX; float seamTop, seamBottom; bool tinted; std::vector<Item> items; std::vector<Spot> spots; };",
             "    inline constexpr int height = %d;" % H,
             "    inline constexpr float bigSize = %d, selectorSize = %d, smallSize = %d, toggleSize = %d;" % (BIG, SEL, SMALL, SWITCH),
             "    inline constexpr float bigAngle = %.1ff, smallAngle = %.1ff;" % (BIG_ANGLE, SMALL_ANGLE),
             "    inline constexpr float meterWidth = %d, meterHeight = %d, meterTop = 62, meterPhotoWidth = %d;" % (METER_W, METER_H, METER_SRC[2] - METER_SRC[0]),
             "    inline constexpr float needlePivotX = %.2ff, needlePivotY = %.2ff, needleReach = %.1ff;" % (NEEDLE_PIVOT + (NEEDLE_R,)),
             "    inline constexpr float needleWindow[4] { %.1ff, %.1ff, %.1ff, %.1ff };" % NEEDLE_WINDOW,
             "    inline constexpr float needleAt0 = -37.45f, needlePerDb = 3.8445f;   // (0..20 dB of compression across)",
             "    inline const std::vector<Face>& faces()", "    {", "        static const std::vector<Face> all {"]
    for f in FACES:
        t, b = seam (f)
        lines.append ('            { "%s", "%s", %d, %.1ff, %.1ff, %.1ff, %s, {' % (f["type"], f["key"], f["w"], f["mx"], t, b, "true" if "tint" in f else "false"))
        for it in items_of (f):
            angs = ", ".join ("%.1ff" % a for a in angles_of (it))
            lines.append ('                { "%s", Part::%s, %.1ff, %.1ff, { %s } },' % (it[1], kinds[it[0]], it[2], it[3], angs))
        lines.append ("            }, {")
        for (pid, x, y, prop) in spots_of (f):
            lines.append ('                { "%s", %.1ff, %.1ff, %.4ff },' % (pid, x, y, prop))
        lines.append ("            } },")
    lines += ["        };", "        return all;", "    }", "}", ""]
    open (os.path.join (ROOT, "Source", "ui", "ClassicLayouts.h"), "w").write ("\n".join (lines))

def main():
    blk = meter_block()
    sp = sprites()
    for name, im in sp.items():
        im.save (os.path.join (OUT, "classic_%s.png" % name))
    header()
    for f in FACES:
        rendered = {}
        for skin in ("gd", "gs", "mg", "mb") + (("gc",) if "tint" in f else ()):
            img = render (f, skin, blk)
            rendered[skin] = img
            if not PREVIEW:
                img.convert ("RGB").save (os.path.join (OUT, "classic_%s_%s.jpg" % (f["key"], skin)), quality = 88, optimize = True)
        if PREVIEW:
            os.makedirs (PREVIEW, exist_ok = True)
            hh = half_and_half (f, rendered["gd"], rendered.get ("gc", rendered["gs"]))
            for name, im in (("hh", hh), ("gd", rendered["gd"]), ("gs", rendered["gs"]), ("mg", rendered["mg"]), ("mb", rendered["mb"])):
                im = im.copy(); place (im, f, sp)
                im.convert ("RGB").save (os.path.join (PREVIEW, "%s_%s.png" % (f["key"], name)))
    print ("done")

if __name__ == "__main__":
    main()
