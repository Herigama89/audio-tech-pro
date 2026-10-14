#!/usr/bin/env python3
"""
Builds the Xenon skin's pieces from your picture (pixels cut out, nothing drawn):

    python3 Tools/make_xenon_skin.py <xenon-picture.jpg>   (1365 x 768)

Writes to Resources/skin/xenon/ (all start with xenon_):
  xenon_knob.png     the glowing cyan knob (round cut)
  xenon_cap.png      the fader cap
  xenon_slot.png     the lit (used) insert slot
  xenon_meter.png    a stretch of the lit teal meter (tiled up the meter)
  xenon_panel.png    the dark cell texture behind the mixer
  xenon_bar.png      the engraved swirl behind the control bar
"""
import os
import sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'Resources', 'skin', 'xenon')
os.makedirs(OUT, exist_ok=True)
src = Image.open(sys.argv[1]).convert('RGBA')
S = src.width / 1365.0


def crop(x0, y0, x1, y1):
    return src.crop(tuple(int(round(v * S)) for v in (x0, y0, x1, y1)))


def save(im, name, scale=1):
    if scale != 1:
        im = im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)
    im.save(os.path.join(OUT, 'xenon_' + name + '.png'), optimize=True)
    print('wrote', name, im.size)


# The knob: a round cut (its glow included), at 4x so it scales down cleanly.
k = crop(4, 406, 25, 427)
k = k.resize((k.width * 4, k.height * 4), Image.LANCZOS)
a = np.asarray(k).copy()
h, w = a.shape[:2]
yy, xx = np.mgrid[0:h, 0:w]
d = np.sqrt((xx + 0.5 - w / 2) ** 2 + (yy + 0.5 - h / 2) ** 2)
a[:, :, 3] = (np.clip((w / 2 - 1 - d) / 2.0, 0, 1) * 255).astype(np.uint8)
save(Image.fromarray(a, 'RGBA'), 'knob')

save(crop(4, 590, 16, 613), 'cap', 4)
save(crop(3, 466, 36, 477), 'slot', 4)
save(crop(29, 645, 35, 688), 'meter', 4)
save(crop(1075, 400, 1315, 715), 'panel')
save(crop(225, 12, 525, 88), 'bar')
