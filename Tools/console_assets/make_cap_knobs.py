"""The Console caps knobs (Themes > Knobs): the blue cap knob and the pan knob cut from knob_sheet.jpg (the user's picture),
each in a circle just inside its black shadow, centred on the knob's own axis (found by its rotational symmetry) so it turns
true; the blue cap repainted for each section. Writes Resources/skin/looks/look_cap_ssl_<colour>.png and look_knob_pan.png."""
import cv2, numpy as np, os
HERE = os.path.dirname (os.path.abspath (__file__))
OUT = os.path.join (HERE, "..", "..", "Resources", "skin", "looks")
src = cv2.imread (os.path.join (HERE, "knob_sheet.jpg"))
grey = cv2.cvtColor (src, cv2.COLOR_BGR2GRAY).astype (np.float32)
ang = np.linspace (0, 2 * np.pi, 240, endpoint = False)
away_from_pointer = np.abs (((ang + np.pi / 2 + np.pi) % (2 * np.pi)) - np.pi) > 0.6

def axis (cx, cy, r0, r1):
    """The centre about which the knob looks the same all round (least spread along each circle)."""
    def spread (x, y):
        rs = np.arange (r0, r1, 1.0)
        X = (x + np.outer (np.cos (ang), rs)).astype (np.float32); Y = (y + np.outer (np.sin (ang), rs)).astype (np.float32)
        return cv2.remap (grey, X, Y, cv2.INTER_LINEAR)[away_from_pointer].std (0).sum()
    for step in (1.0, 0.25):
        best = min (((spread (cx + dx, cy + dy), cx + dx, cy + dy) for dx in np.arange (-4, 4.01, step) for dy in np.arange (-4, 4.01, step)))
        cx, cy = best[1], best[2]
    return cx, cy

def cut (cx, cy, R, size):
    scale = size / (2.0 * R)
    M = np.float32 ([[scale, 0, size / 2 - cx * scale], [0, scale, size / 2 - cy * scale]])
    im = cv2.warpAffine (src, M, (size, size), flags = cv2.INTER_LANCZOS4, borderMode = cv2.BORDER_REFLECT)
    yy, xx = np.mgrid[0:size, 0:size]; rr = np.hypot (xx - size / 2 + 0.5, yy - size / 2 + 0.5) / scale
    out = cv2.cvtColor (im, cv2.COLOR_BGR2BGRA)
    out[:, :, 3] = (np.clip ((R - rr) * scale / 1.5, 0, 1) * 255).astype (np.uint8)
    return out

SIZE = 192
cx, cy = axis (237.5, 203.5, 70, 108)
cap = cut (cx, cy, 108, SIZE)
px, py = axis (236.5, 564.5, 75, 116)
cv2.imwrite (os.path.join (OUT, "look_knob_pan.png"), cut (px, py, 118, SIZE))

hsv = cv2.cvtColor (cap[:, :, :3], cv2.COLOR_BGR2HSV).astype (np.float32)
capMask = cv2.GaussianBlur (((hsv[:, :, 0] > 92) & (hsv[:, :, 0] < 132) & (hsv[:, :, 1] > 70)).astype (np.float32), (5, 5), 0)[:, :, None]
yy, xx = np.mgrid[0:SIZE, 0:SIZE]
pointer = cv2.GaussianBlur (((hsv[:, :, 1] < 80) & (hsv[:, :, 2] > 150) & (np.hypot (xx - SIZE / 2, yy - SIZE / 2) < SIZE * 0.38)).astype (np.float32), (3, 3), 0)[:, :, None]

def paint (name, hue = None, sat = 1.0, val = 1.0, fixed_sat = None, dark_pointer = False):
    h = hsv.copy()
    if hue is not None: h[:, :, 0] = hue
    h[:, :, 1] = np.clip (h[:, :, 1] * sat if fixed_sat is None else fixed_sat, 0, 255)
    h[:, :, 2] = np.clip (h[:, :, 2] * val, 0, 255)
    rgb = cv2.cvtColor (h.astype (np.uint8), cv2.COLOR_HSV2BGR).astype (np.float32)
    o = cap.astype (np.float32)
    o[:, :, :3] = o[:, :, :3] * (1 - capMask) + rgb * capMask
    if dark_pointer:
        o[:, :, :3] = o[:, :, :3] * (1 - pointer) + 30 * pointer
    cv2.imwrite (os.path.join (OUT, "look_cap_ssl_%s.png" % name), o.clip (0, 255).astype (np.uint8))

paint ("blue")
paint ("red", hue = 0, sat = 1.15, val = 1.15)
paint ("green", hue = 62, sat = 0.95, val = 0.95)
paint ("dkgreen", hue = 66, sat = 0.9, val = 0.62)
paint ("brown", hue = 12, sat = 0.85, val = 0.6)
paint ("gold", hue = 22, sat = 1.05, val = 1.35)
paint ("purple", hue = 137, sat = 0.8, val = 1.05)
paint ("white", fixed_sat = 8, val = 1.85, dark_pointer = True)
paint ("grey", fixed_sat = 6, val = 1.05)
paint ("black", fixed_sat = 6, val = 0.42)
print ("axes", (cx, cy), (px, py))
