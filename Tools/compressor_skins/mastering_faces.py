#!/usr/bin/env python3
"""
Mastering Compressors: three stereo mastering compressors with faces after the hardware's own (its layout, the kind
of knobs and how far they turn, its meters) - drawn from scratch, no maker's artwork or names:

    Mastering Hill Comp    an optical stage into a discrete VCA stage, a choice of output transformer
    Mastering Child Comp   a variable-mu valve limiter: input, threshold, time constant, DC bias, lateral/vertical
    Iron Master Comp       a valve mastering compressor: rectifier, tube bias, airbag, side-chain bass cut

Skins: Hardware (each face's own paint), and the Classic Compressors' glass and metal ones.

    python3 Tools/compressor_skins/mastering_faces.py --preview OUT_DIR
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
import classic_faces as cf
from classic_faces import ctext, polar, font, FONT, FONT_BOLD, FONT_MED, H

NEW = "/tmp/claude-0/knobs/new"
OUTK = "/tmp/claude-0/knobs/out"
PREVIEW = sys.argv[sys.argv.index ("--preview") + 1] if "--preview" in sys.argv else None
TEN = [str (v) for v in range (0, 11)]

def steps (n, step = 45.0): return [-(step * (n - 1) / 2) + step * i for i in range (n)]

# Items: (part, id, x, y, labels, caption[, angles]); parts: big / sel (big sprite, smaller) / small / switch
FACES = [
    dict (key = "hill", title = "MASTERING HILL COMP", sub = "OPTICAL  ·  DISCRETE  ·  TRANSFORMERS", w = 2300, mx = 935,
          paint = ((52, 54, 58), (30, 31, 34)), ink = (226, 228, 230), accent = (96, 220, 120),
          knobs = ("Moogish_1", "MiniMoog_Selector", "Vintage_Knob"), meter = "green",
          sections = [("OPTICAL", 150, 640), ("DISCRETE", 1240, 1900)],
          items = [
        ("big", "optThreshold", 300, 255, TEN, "THRESHOLD"),
        ("small", "optGain", 500, 230, ("0", "", "+10"), "GAIN"),
        ("switch", "optIn", 500, 395, ("IN", "OUT"), ""),
        ("sel", "transformer", 815, 385, ("NICKEL", "IRON", "STEEL"), "TRANSFORMER"),
        ("switch", "scFilter", 960, 385, ("SC 90 Hz", "SC FLAT"), ""),
        ("switch", "link", 1070, 385, ("LINK", "DUAL"), ""),
        ("big", "discThreshold", 1350, 255, TEN, "THRESHOLD"),
        ("sel", "ratio", 1560, 230, ("1.2", "2", "3", "4", "6"), "RATIO"),
        ("sel", "attack", 1560, 395, (".1", ".2", ".4", ".8", "1.6", "3.2"), "ATTACK ms"),
        ("sel", "recovery", 1720, 230, (".1", ".25", ".4", ".8", "1.2", "DUAL"), "RECOVERY s"),
        ("small", "discGain", 1720, 395, ("0", "", "+10"), "GAIN"),
        ("switch", "discIn", 1860, 230, ("IN", "OUT"), ""),
    ]),
    dict (key = "child", title = "MASTERING CHILD COMP", sub = "VARIABLE-MU VALVE LIMITER", w = 2000, mx = 1000,
          paint = ((132, 146, 160), (104, 117, 131)), ink = (248, 248, 246), accent = (248, 248, 246),
          knobs = ("BB_radio_128", "knob_66_black", "knob_vintage_black"), meter = "white",
          sections = [],
          items = [
        ("big", "input", 300, 255, ("-∞", "-20", "-15", "-10", "-6", "-3", "0"), "INPUT GAIN"),
        ("big", "threshold", 570, 255, TEN, "THRESHOLD"),
        ("switch", "agc", 1000, 395, ("LAT/VERT", "LEFT/RIGHT"), ""),
        ("sel", "timeConstant", 1430, 255, ("1", "2", "3", "4", "5", "6"), "TIME CONSTANT", steps (6, 40.0)),
        ("small", "bias", 1640, 230, ("LO", "", "HI"), "DC BIAS"),
        ("small", "output", 1640, 395, ("-10", "0", "+10"), "OUTPUT"),
    ]),
    dict (key = "iron", title = "IRON MASTER COMP", sub = "VALVE MASTERING COMPRESSOR", w = 2100, mx = 1050,
          paint = ((38, 36, 34), (22, 21, 20)), ink = (236, 226, 208), accent = (236, 168, 72),
          knobs = ("ChromedKnob_1_1", "ENIGMA-knob-S", "ENIGMA-knob-S"), meter = "cream",
          sections = [],
          items = [
        ("big", "threshold", 310, 255, ("-30", "-25", "-20", "-15", "-10", "-5", "0", "+5", "+10"), "THRESHOLD"),
        ("sel", "attack", 540, 230, ("1", "2", "3", "4", "5", "6"), "ATTACK"),
        ("sel", "release", 540, 395, ("1", "2", "3", "4", "5", "6"), "RELEASE"),
        ("switch", "tubeBias", 920, 395, ("HI", "LO"), "TUBE BIAS"),
        ("sel", "airbag", 1050, 390, ("OFF", "1", "2", "3"), "AIRBAG"),
        ("sel", "scBassCut", 1180, 390, ("OFF", "75", "150"), "S/C BASS CUT"),
        ("sel", "rectifier", 1380, 230, ("1", "2", "3", "4", "5", "6"), "RECTIFIER"),
        ("switch", "link", 1380, 395, ("LINK", "DUAL"), ""),
        ("big", "makeup", 1610, 255, ("0", "2", "4", "6", "8", "10", "12", "14", "16", "18", "20"), "MAKE-UP"),
    ]),
]

def common (face):
    w = face["w"]
    return [
        ("small", "mix", w - 200, 150, ("DRY", "", "WET"), "MIX"),
        ("switch", "power", w - 240, 330, ("ON", "POWER"), ""),
    ]

def items_of (face): return face["items"] + common (face)

def angles_of (it):
    if len (it) > 6: return it[6]
    if it[0] == "sel": return steps (len (it[4]))
    if it[0] in ("big", "small"): return [-135.0, 135.0]
    return []

class HwSkin:
    def __init__ (self, face):
        self.key = "hw"; self.glass = False; self.tint = None
        self.ink = face["ink"] + (255,)
        self.accent = face["accent"] + (255,)
        self.dark = sum (face["paint"][0]) < 380
        self.dim = tuple (int (c * 0.75) for c in face["ink"]) + (255,)

def hw_panel (face, sk):
    """The hardware's own paint: a flat enamel panel, lightly textured, and its rack ears."""
    W = face["w"]
    rng = np.random.default_rng (11)
    top, bottom = np.array (face["paint"][0], np.float32), np.array (face["paint"][1], np.float32)
    t = np.linspace (0, 1, H, dtype = np.float32)[:, None, None]
    body = top * (1 - t) + bottom * t + np.zeros ((H, W, 3), np.float32)
    body += rng.normal (0, 1.0, (H, W, 1)).astype (np.float32) * 2.2   # (enamel's fine grain)
    img = Image.fromarray (np.clip (body, 0, 255).astype (np.uint8)).convert ("RGBA")
    d = ImageDraw.Draw (img)
    ear = tuple (int (c * 0.82) for c in face["paint"][1]) + (255,)
    for x0, x1 in ((0, 70), (W - 70, W)):
        d.rectangle ((x0, 0, x1, H), fill = ear)
    edge = tuple (int (c * 0.55) for c in face["paint"][1]) + (255,)
    d.line ((70, 0, 70, H), fill = edge, width = 2); d.line ((W - 70, 0, W - 70, H), fill = edge, width = 2)
    d.line ((70, 1, W - 70, 1), fill = tuple (min (255, int (c * 1.25)) for c in face["paint"][0]) + (255,), width = 2)
    cf.slots (img, W, sk)
    for (x, y) in ((110, 28), (W - 110, 28), (110, 452), (W - 110, 452)):
        cf.screw (img, x, y)
    return img

# ---- meters --------------------------------------------------------------------------------------------------------
def strip_frame (name, index, frames, fw, fh):
    s = Image.open (os.path.join (NEW, name + ".png")).convert ("RGBA")
    return s.crop ((0, index * fh, fw, (index + 1) * fh))

def white_vu (pos):
    """The white VU (a filmstrip of its needle): its maker's mark painted out."""
    k = int (round (pos * 99))
    im = strip_frame ("Realistic-VU2-2", k, 100, 256, 256)
    d = ImageDraw.Draw (im)
    d.rectangle ((96, 150, 160, 176), fill = (236, 238, 236, 255))   # (the script logo)
    return im

def cream_vu (pos):
    k = int (round (pos * 47))
    return strip_frame ("vu3", k, 48, 240, 160)

def green_meter (gr):
    """A gain-reduction meter lit green from behind (drawn): 0..20 dB."""
    w, h = 230, 170
    yy, xx = np.mgrid[0:h, 0:w].astype (np.float32)
    glow = np.exp (-(((xx - w / 2) / (w * 0.6)) ** 2 + ((yy - h * 0.9) / (h * 0.9)) ** 2))
    face = np.zeros ((h, w, 4), np.float32)
    face[..., 0] = 20 + 60 * glow; face[..., 1] = 60 + 170 * glow; face[..., 2] = 30 + 80 * glow; face[..., 3] = 255
    im = Image.fromarray (face.astype (np.uint8), "RGBA")
    d = ImageDraw.Draw (im)
    pivot = (w / 2, h + 40); R = 160
    ink = (10, 26, 12, 255)
    for i, lab in enumerate (("0", "2", "4", "6", "8", "10", "15", "20")):
        a = -40 + 80 * i / 7
        d.line (polar (pivot, R - 12, a) + polar (pivot, R, a), fill = ink, width = 2)
        x, y = polar (pivot, R + 14, a)
        ctext (d, x, y - 8, lab, font (FONT_MED, 13), ink)
    ctext (d, w / 2, h - 52, "GAIN REDUCTION", font (FONT_MED, 11), ink, spacing = 1)
    a = math.radians (-40 + 80 * min (1.0, gr / 20.0))
    d.line ((pivot[0], pivot[1], pivot[0] + math.sin (a) * (R + 4), pivot[1] - math.cos (a) * (R + 4)), fill = (12, 12, 12, 255), width = 3)
    m = Image.new ("L", (w, h), 0); ImageDraw.Draw (m).rounded_rectangle ((0, 0, w - 1, h - 1), 10, fill = 255)
    im.putalpha (m)
    return im

def meters (img, face, sk, positions = (0.55, 0.62)):
    mx = face["mx"]
    for k, pos in zip ((-1, 1), positions):
        if face["meter"] == "white":
            m = white_vu (pos).resize ((244, 244), Image.LANCZOS); cx, top = mx + k * 132, 40
        elif face["meter"] == "cream":
            m = cream_vu (pos).resize ((264, 176), Image.LANCZOS); cx, top = mx + k * 142, 66
        else:
            m = green_meter (pos * 20.0); cx, top = mx + k * 128, 62
        x0 = int (cx - m.width / 2)
        sh = Image.new ("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw (sh).rounded_rectangle ((x0 + 4, top + 8, x0 + m.width + 4, top + m.height + 10), 14, fill = (0, 0, 0, 120))
        img.alpha_composite (sh.filter (ImageFilter.GaussianBlur (10)))
        img.alpha_composite (m, (x0, top))
        ImageDraw.Draw (img).rounded_rectangle ((x0, top, x0 + m.width - 1, top + m.height - 1), 12,
                                                 outline = (255, 255, 255, 60) if sk.dark else (255, 255, 255, 170), width = 2)
        ctext (ImageDraw.Draw (img), cx, top + m.height + 6, "LEFT" if k < 0 else "RIGHT", font (FONT, 11), sk.ink, spacing = 2)

# ---- drawing -------------------------------------------------------------------------------------------------------
def draw_items (img, face, sk):
    d = ImageDraw.Draw (img)
    fcap = font (FONT, 14); fsm = font (FONT, 12); ft = font (FONT_MED, 11)
    for it in items_of (face):
        kind, pid, x, y, labels, caption = it[:6]
        angs = angles_of (it)
        if kind == "big":
            n = len (labels)
            cf.scale_ring (d, (x, y), 104, list (labels), sk.ink) if n > 1 else None
            ctext (d, x, y + 122, caption, fcap, sk.ink, spacing = 2)
        elif kind == "sel":
            r = 62 if pid != "timeConstant" else 82
            for a, s in zip (angs, labels):
                px, py = polar ((x, y), r, a)
                ctext (d, px, py - 7, s, ft if pid != "timeConstant" else fcap, sk.ink)
                d.line (polar ((x, y), r - 16, a) + polar ((x, y), r - 10, a), fill = sk.ink, width = 2)
            ctext (d, x, y + (70 if pid != "timeConstant" else 104), caption, fsm if pid != "timeConstant" else fcap, sk.ink, spacing = 1)
        elif kind == "small":
            spots = [(-135, labels[0]), (0, labels[1]), (135, labels[2])]
            for a, s in spots:
                if s:
                    px, py = polar ((x, y), 54, a)
                    ctext (d, px, py - 7, s, ft, sk.ink)
            ctext (d, x, y + 50, caption, fsm, sk.ink, spacing = 1)
        elif kind == "switch":
            ctext (d, x, y - 64, labels[0], fsm, sk.ink, spacing = 1)
            ctext (d, x, y + 52, labels[1], fsm, sk.ink, spacing = 1)
            if caption:
                ctext (d, x, y + 70, caption, fsm, sk.ink, spacing = 1)

def sections (img, face, sk):
    d = ImageDraw.Draw (img)
    for name, x0, x1 in face["sections"]:
        d.line ((x0, 156, x0 + 40, 156), fill = sk.accent, width = 2)
        d.line ((x1 - 40, 156, x1, 156), fill = sk.accent, width = 2)
        ctext (d, (x0 + x1) / 2, 148, name, font (FONT_BOLD, 13), sk.accent, spacing = 3)

def title (img, face, sk):
    d = ImageDraw.Draw (img)
    d.text ((116, 54), face["title"], font = font (FONT_BOLD, 32), fill = sk.ink)
    d.rectangle ((118, 98, 122, 118), fill = sk.accent)
    d.text ((132, 100), face["sub"], font = font (FONT_MED, 13), fill = sk.dim)

def render (face, skin):
    W = face["w"]
    if skin == "hw":
        sk = HwSkin (face); img = hw_panel (face, sk)
    else:
        sk = cf.Skin (skin); img = cf.glass_panel (W, sk) if sk.glass else cf.metal_panel (W, sk)
    title (img, face, sk)
    sections (img, face, sk)
    meters (img, face, sk)
    draw_items (img, face, sk)
    return img

def strip (name, frames = None):
    s = Image.open (os.path.join (NEW, name + ".png")).convert ("RGBA")
    w = s.size[0]
    n = s.size[1] // w
    return [s.crop ((0, i * w, w, (i + 1) * w)) for i in range (n)]

def place (img, face):
    big, sel, small = (strip (n) for n in face["knobs"])
    sw = cf.sprites()
    def put (frames, c, size, value):
        im = frames[int (round (value * (len (frames) - 1)))].resize ((size, size), Image.LANCZOS)
        img.alpha_composite (im, (int (c[0] - size / 2), int (c[1] - size / 2)))
    for it in items_of (face):
        kind, pid, x, y = it[:4]
        v = 0.4 if kind in ("big", "small") else 0.5
        if kind == "big": put (big, (x, y), 170, v)
        elif kind == "sel": put (sel, (x, y), 108 if pid != "timeConstant" else 150, v)
        elif kind == "small": put (small, (x, y), 84, v)
        elif kind == "switch":
            im = sw["switch_up"].resize ((96, 96), Image.LANCZOS); img.alpha_composite (im, (x - 48, y - 48))
    led = sw["led_on"].resize ((56, 56), Image.LANCZOS); img.alpha_composite (led, (face["w"] - 160 - 28, 330 - 28))

def main():
    os.makedirs (PREVIEW, exist_ok = True)
    for f in FACES:
        for skin in ("hw", "gd", "gs"):
            im = render (f, skin); place (im, f)
            im.convert ("RGB").save (os.path.join (PREVIEW, "%s_%s.png" % (f["key"], skin)))
    print ("done")

if __name__ == "__main__":
    main()
