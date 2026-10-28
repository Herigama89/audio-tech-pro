"""Numbered call-outs on screenshots ("sketches" for the manual): a numbered dot on each part, an outline round it,
and the key (number: name) returned for the text.

annotate(png, parts, out, crop=None, zoom=1.0, pad=(0,0,0,0), max_width=1400) -> [(n, label)]
  parts: dicts with label, x, y, w, h (in the screenshot's pixels) and, optionally, dot: [x, y] - where the number
  goes (screenshot pixels too; it may be out in the padding), joined to its part by a line."""
from PIL import Image, ImageDraw, ImageFont
import os

COLOURS = [(255, 138, 31), (25, 211, 208), (162, 91, 255), (255, 59, 84), (255, 216, 74), (92, 255, 143), (63, 168, 255), (255, 79, 180)]


def font(size):
    for f in ['/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', '/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf']:
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def annotate(png, parts, out, crop=None, zoom=1.0, pad=(0, 0, 0, 0), max_width=1400, background=(24, 26, 30)):
    src = Image.open(png).convert('RGB')
    if crop:
        src = src.crop(crop)
        ox, oy = crop[0], crop[1]
    else:
        ox = oy = 0
    if zoom != 1.0:
        src = src.resize((int(src.width * zoom), int(src.height * zoom)), Image.LANCZOS)
    # dim the picture a little so the call-outs stand out
    src = Image.blend(src, Image.new('RGB', src.size, (0, 0, 0)), 0.18)
    pl, pt, pr, pb = pad
    im = Image.new('RGB', (src.width + pl + pr, src.height + pt + pb), background)
    im.paste(src, (pl, pt))

    def to(x, y):
        return ((x - ox) * zoom + pl, (y - oy) * zoom + pt)

    d = ImageDraw.Draw(im)
    f = font(15)
    key, placed, dots = [], [], []
    for i, p in enumerate(parts):
        n = len(key) + 1
        c = COLOURS[(n - 1) % len(COLOURS)]
        x, y = to(p['x'], p['y'])
        w, h = p['w'] * zoom, p['h'] * zoom
        if x + w < 0 or y + h < 0 or x > im.width or y > im.height:
            continue
        d.rectangle([x, y, x + w, y + h], outline=c, width=2)
        if 'dot' in p:
            cx, cy = to(*p['dot'])
            # a line from the number to the nearest point of its part
            nx, ny = min(max(cx, x), x + w), min(max(cy, y), y + h)
            d.line([cx, cy, nx, ny], fill=c, width=2)
        else:
            big = w * h > 200 * 120
            cx, cy = (x + 24, y + 24) if big else (x + w / 2, y + h / 2)
            for (px, py) in placed:
                if abs(px - cx) < 22 and abs(py - cy) < 22:
                    cy += 24
        placed.append((cx, cy))
        dots.append((cx, cy, c, str(n)))
        key.append((n, p['label']))
    for cx, cy, c, t in dots:   # (numbers over the lines)
        r = 12
        d.ellipse([cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2], fill=(0, 0, 0))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=c)
        tw = d.textlength(t, font=f)
        d.text((cx - tw / 2, cy - 9), t, fill=(0, 0, 0), font=f)
    if im.width > max_width:
        im = im.resize((max_width, int(im.height * max_width / im.width)), Image.LANCZOS)
    im.save(out, quality=86)
    return key
