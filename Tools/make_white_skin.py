#!/usr/bin/env python3
"""
Builds the Studio White skin's pieces from your pictures (no drawing: the
pixels are cut out, and recoloured where noted):

    python3 Tools/make_white_skin.py <faders.png> <interface-panel.png>

  faders.png          the three long-throw faders (transparent PNG, 1280 x 1091)
  interface-panel.png the audio interface's front panel (857 x 196): its big
                      knob with the LED ring, and the blue LED level meter

Writes to Resources/skin/white/:
  white_fader_cap_black.png (all files start with white_)
  fader_cap_black.png  the middle fader's cap, as it is
  fader_cap.png        the same cap in a white shade (its light and dark bands
                       swapped, the white line turned blue), for the white skin
  fader_rail.png       a slice of the fader's rail (tiled down the slot)
  knob_body.png        the knob: knurled edge and lit inner ring
  knob_ring.png        the LED ring around it (lit as far as the value)
  led_on.png, led_clip.png   one lit meter segment, and the red clip light
"""
import os
import sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'Resources', 'skin', 'white')
os.makedirs(OUT, exist_ok=True)

faders = Image.open(sys.argv[1]).convert('RGBA')
panel = Image.open(sys.argv[2]).convert('RGBA')

# --- Fader cap (the middle one) and its rail ---------------------------------
cap = faders.crop((531, 365, 681, 671))
cap.save(os.path.join(OUT, 'white_fader_cap_black.png'), optimize=True)

a = np.asarray(cap).astype(float)
lum = a[:, :, :3].mean(axis=2)
white = np.zeros_like(a)
light = np.clip(250.0 - lum * 0.42, 0, 255)          # dark bands -> light, light bands -> a little darker
line = lum > 205                                        # the white line across the cap
for c, blue in zip(range(3), (62, 140, 226)):
    ch = light.copy()
    ch[line] = blue
    white[:, :, c] = ch
white[:, :, 0] *= 0.985                                 # a touch warm, like the console's white caps
white[:, :, 2] *= 1.0
white[:, :, 3] = a[:, :, 3]
# Keep the cap's own rim (its outermost pixels, a mid grey) so it stands out on a white strip.
opaque = a[:, :, 3] > 200
rim = opaque & ~(np.roll(opaque, 2, 0) & np.roll(opaque, -2, 0) & np.roll(opaque, 2, 1) & np.roll(opaque, -2, 1))
for c in range(3):
    white[:, :, c][rim] = np.clip(lum[rim] * 0.45 + 95, 0, 255)
Image.fromarray(white.astype(np.uint8), 'RGBA').save(os.path.join(OUT, 'white_fader_cap.png'), optimize=True)

rail = faders.crop((586, 80, 624, 200))
rail.save(os.path.join(OUT, 'white_fader_rail.png'), optimize=True)

# --- The knob: centre (398, 97); body inside r 66, LED ring 66..88 ------------
cx, cy, R = 398.0, 97.0, 89
box = (int(cx - R), int(cy - R), int(cx + R), int(cy + R))
k = np.asarray(panel.crop(box)).astype(float)
h, w = k.shape[:2]
yy, xx = np.mgrid[0:h, 0:w]
d = np.sqrt((xx + 0.5 - R) ** 2 + (yy + 0.5 - R) ** 2)

body = k.copy()
body[:, :, 3] = np.clip((66.0 - d) * 1.5 + 0.5, 0, 1) * 255
Image.fromarray(body.astype(np.uint8), 'RGBA').save(os.path.join(OUT, 'white_knob_body.png'), optimize=True)

ring = k.copy()
inside = np.clip((d - 66.0) * 1.5, 0, 1)
outside = np.clip((89.0 - d) * 1.5, 0, 1)
glow = np.clip((k[:, :, 2] - k[:, :, 0] - 20) / 120.0, 0, 1)  # only the lit (blue) pixels
ring[:, :, 3] = inside * outside * glow * 255
Image.fromarray(ring.astype(np.uint8), 'RGBA').save(os.path.join(OUT, 'white_knob_ring.png'), optimize=True)

# --- Meter segments -------------------------------------------------------------
panel.crop((678, 36, 691, 77)).save(os.path.join(OUT, 'white_led_on.png'), optimize=True)
panel.crop((808, 41, 819, 70)).save(os.path.join(OUT, 'white_led_clip.png'), optimize=True)
print('wrote', sorted(os.listdir(OUT)))
