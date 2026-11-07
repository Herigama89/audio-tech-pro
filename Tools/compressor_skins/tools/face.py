# face.py: cutting a hardware photo into a face for Audio Tech Pro: the panel with its moving parts taken
# out (and the makers' names), the parts as pictures of their own, and a layout (JSON) of where they go.
import numpy as np, cv2, json, math, os
from PIL import Image

class Face:
    def __init__(self, name, path, crop=None, scale=1.0, out='out'):
        im = Image.open(path).convert('RGB')
        self.crop = crop or (0, 0, im.width, im.height)   # (the parts are cut from the whole photo; the panel is cropped at the end)
        if scale != 1.0:
            im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
        self.name, self.scale, self.out = name, scale, out
        self.src = np.array(im).astype(np.float32)
        self.bg = self.src.copy()
        self.H, self.W = self.src.shape[:2]
        self.yy, self.xx = np.mgrid[0:self.H, 0:self.W].astype(np.float32)
        self.layout = {'size': [self.W, self.H], 'parts': {}}
        self.rng = np.random.default_rng(7)
        os.makedirs(out, exist_ok=True)

    # (all coordinates are given in the original photo's pixels, before the crop's offset is taken off by the caller)
    def S(self, v): return v * self.scale

    def save_part(self, key, rgb, alpha, info):
        fn = f'{self.name}_{key}'
        a = (np.clip(alpha, 0, 1) * 255).astype(np.uint8)
        Image.fromarray(np.dstack([np.clip(rgb, 0, 255).astype(np.uint8), a]), 'RGBA').save(f'{self.out}/{fn}.png')
        info['image'] = fn + '_png'
        self.layout['parts'][key] = info

    # --- removing things from the panel ---------------------------------------------------------------
    def smooth_fill(self, mask, sigma=(40, 14), feather=0.0):
        """Fills a plain painted panel under mask: its own colour from round it (smoothly), with a little grain."""
        keep = (cv2.dilate(mask, np.ones((5, 5), np.uint8)) == 0).astype(np.float32)
        est = None
        for sg in sigma:
            sx, sy = sg if isinstance(sg, tuple) else (sg, sg)
            num = cv2.GaussianBlur(self.bg * keep[..., None], (0, 0), sx, sigmaY=sy); den = cv2.GaussianBlur(keep, (0, 0), sx, sigmaY=sy)[..., None]
            e = num / np.maximum(den, 1e-6); w = np.clip(den / 0.2, 0, 1)
            est = e if est is None else est * (1 - w) + e * w
        est = est + self.rng.normal(0, 1.1, est.shape)
        if feather > 0:
            inner = cv2.erode(mask, np.ones((int(feather * 2) | 1, int(feather * 2) | 1), np.uint8)).astype(np.float32)
            m = np.clip(cv2.GaussianBlur(inner, (0, 0), feather) * 1.2, 0, 1)[..., None]
        else:
            m = np.maximum(mask.astype(np.float32), cv2.GaussianBlur(mask.astype(np.float32), (0, 0), 1.2))[..., None]
        self.bg = self.bg * (1 - m) + est * m

    def erase(self, rects, brushed=False, threshold=None, dilate=3, radius=5, smooth=False, sigma=(40, 14), feather=0.0):
        """Takes out what's in the rectangles (text, logos): with threshold, only the pixels that differ from the
        panel's own colour round them by more than that; filled from round them (brushed: with horizontal grain)."""
        mask = np.zeros((self.H, self.W), np.uint8)
        for (x0, y0, x1, y1) in rects:
            x0, y0, x1, y1 = [int(round(self.S(v))) for v in (x0, y0, x1, y1)]
            x0, y0 = max(0, x0), max(0, y0)
            x1, y1 = min(self.W, x1), min(self.H, y1)
            if threshold is None:
                mask[y0:y1, x0:x1] = 1
            else:
                ring = np.concatenate([self.bg[max(0, y0 - 4):y0, x0:x1].reshape(-1, 3), self.bg[y1:y1 + 4, x0:x1].reshape(-1, 3),
                                       self.bg[y0:y1, max(0, x0 - 4):x0].reshape(-1, 3), self.bg[y0:y1, x1:x1 + 4].reshape(-1, 3)])
                ref = np.median(ring, axis=0)
                d = np.abs(self.bg[y0:y1, x0:x1] - ref).sum(2)
                mask[y0:y1, x0:x1] = (d > threshold).astype(np.uint8)
        if dilate: mask = cv2.dilate(mask, np.ones((dilate, dilate), np.uint8))
        if smooth: self.smooth_fill(mask, sigma, feather)
        else: self.fill(mask, brushed, radius)
        return mask

    def fill(self, mask, brushed=False, radius=5):
        if not mask.any(): return
        filled = cv2.inpaint(np.clip(self.bg, 0, 255).astype(np.uint8), mask, radius, cv2.INPAINT_TELEA).astype(np.float32)
        if brushed:
            # brushed metal: grain along the rows
            g = self.rng.normal(0, 1, (self.H, 1)) * 3.0 + self.rng.normal(0, 1, (self.H, self.W)) * 1.2
            g = cv2.GaussianBlur(g.astype(np.float32), (31, 1), 0)
            filled = filled + g[..., None] * 1.6
        else:
            filled = filled + self.rng.normal(0, 1.0, filled.shape)
        m = cv2.GaussianBlur(mask.astype(np.float32), (3, 3), 0.8)[..., None]
        m = np.maximum(m, mask[..., None].astype(np.float32))
        self.bg = self.bg * (1 - m) + filled * m

    # --- knobs ----------------------------------------------------------------------------------------
    def knob_whole(self, key, cx, cy, r, info=None, panel_fill=True, mask='disc', feather=1.2):
        """A knob that turns whole: a disc of the photo round its centre."""
        cx, cy, r = self.S(cx), self.S(cy), self.S(r)
        R = int(math.ceil(r)) + 3
        icx, icy = int(round(cx)), int(round(cy))
        d = np.hypot(*np.mgrid[-R:R, -R:R])
        a = np.clip((r - d) / feather + 0.5, 0, 1)
        crop = self.src[icy - R:icy + R, icx - R:icx + R]
        self.save_part(key, crop, a, dict(info or {}, cx=icx, cy=icy, R=R, kind='turn'))
        if panel_fill:
            m = np.zeros((self.H, self.W), np.uint8)
            m[icy - R:icy + R, icx - R:icx + R] = (d < r + 1).astype(np.uint8)
            self.fill(m)

    def knob_pointer(self, key, cx, cy, r0, r1, kind, hint, spread=22, info=None, threshold=70, fillturn=70, fillmode='turn', grow=3):
        """A knob whose pointer line alone turns (the cap stays as photographed): the line between radii r0 and r1
        round the angle hint (degrees clockwise from 12), lighter (kind 'light') or darker ('dark') than the cap."""
        cx, cy, r0, r1 = self.S(cx), self.S(cy), self.S(r0), self.S(r1)
        d = np.hypot(self.xx - cx, self.yy - cy)
        ang = np.degrees(np.arctan2(self.xx - cx, -(self.yy - cy)))
        near = (np.abs((ang - hint + 180) % 360 - 180) < spread) & (d > r0) & (d < r1)
        ring = (d > r0) & (d < r1) & ~near
        ref = np.median(self.src[ring], axis=0)
        lum = self.src.mean(2); rl = ref.mean()
        cand = near & ((lum - rl > threshold * 0.5) if kind == 'light' else (rl - lum > threshold * 0.5)) & (np.abs(self.src - ref).sum(2) > threshold)
        m = cand.astype(np.uint8)
        n, lab, st, cen = cv2.connectedComponentsWithStats(m)
        if n <= 1: raise RuntimeError(f'{key}: no pointer found')
        i = 1 + np.argmax(st[1:, 4]); m = (lab == i).astype(np.uint8)
        soft = near & (np.abs(self.src - ref).sum(2) > threshold * 0.45)
        m = m | (cv2.dilate(m, np.ones((3, 3), np.uint8)) & soft.astype(np.uint8))
        # (a ring-shaped mark: its inside too)
        ff = m.copy(); h, w = ff.shape; fm = np.zeros((h + 2, w + 2), np.uint8); cv2.floodFill(ff, fm, (0, 0), 1); m = m | (1 - ff)
        ys, xs = np.nonzero(m)
        pa = math.degrees(math.atan2(xs.mean() - cx, -(ys.mean() - cy)))
        R = int(math.ceil(r1)) + 3
        icx, icy = int(round(cx)), int(round(cy))
        alpha = cv2.GaussianBlur(m.astype(np.float32), (3, 3), 0.6)
        self.save_part(key, self.src[icy - R:icy + R, icx - R:icx + R], alpha[icy - R:icy + R, icx - R:icx + R],
                       dict(info or {}, cx=icx, cy=icy, R=R, kind='pointer', angle=round(pa, 1)))
        # the cap without the line: its own surface turned a little round fills in
        em = cv2.dilate(m, np.ones((grow, grow), np.uint8))
        if fillmode == 'inpaint':
            self.fill(em, radius=4)
            return pa
        # (both ways round, averaged: a skirt's reflections differ from one side to the other)
        t1 = cv2.warpAffine(self.bg, cv2.getRotationMatrix2D((float(cx), float(cy)), fillturn, 1.0), (self.W, self.H), flags=cv2.INTER_CUBIC)
        t2 = cv2.warpAffine(self.bg, cv2.getRotationMatrix2D((float(cx), float(cy)), -fillturn, 1.0), (self.W, self.H), flags=cv2.INTER_CUBIC)
        turned = 0.5 * (t1 + t2)
        s = np.maximum(em.astype(np.float32), cv2.GaussianBlur(em.astype(np.float32), (3, 3), 0.8))[..., None]
        self.bg = self.bg * (1 - s) + turned * s
        return pa

    def marker(self, key, cx, cy, rect, info=None, fill=True):
        """A lit marker (a number on a ring) that moves round the knob's centre: the patch in rect."""
        cx, cy = self.S(cx), self.S(cy)
        x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
        R = int(math.ceil(max(math.hypot(x - cx, y - cy) for x in (x0, x1) for y in (y0, y1)))) + 2
        icx, icy = int(round(cx)), int(round(cy))
        a = np.zeros((self.H, self.W), np.float32)
        patch = self.src[y0:y1, x0:x1]
        lum = patch.mean(2)
        a[y0:y1, x0:x1] = np.clip((lum - np.percentile(lum, 40)) / 60, 0, 1)
        pa = math.degrees(math.atan2((x0 + x1) / 2 - cx, -((y0 + y1) / 2 - cy)))
        self.save_part(key, self.src[icy - R:icy + R, icx - R:icx + R], a[icy - R:icy + R, icx - R:icx + R],
                       dict(info or {}, cx=icx, cy=icy, R=R, kind='pointer', angle=round(pa, 1)))
        if fill:
            m = np.zeros((self.H, self.W), np.uint8); m[y0:y1, x0:x1] = (a[y0:y1, x0:x1] > 0.1)
            self.fill(cv2.dilate(m, np.ones((3, 3), np.uint8)))
        return pa

    # --- meters ---------------------------------------------------------------------------------------
    def needle(self, key, tip, base, pivot, rtip, width=4, threshold=60, window=None, info=None, light=False):
        """A meter's needle: cut from the photo between tip and base, straightened and placed so it points straight
        up from the pivot (its tip rtip away): turned round the pivot to show the reading. The photo's needle is
        taken out of the face."""
        tx, ty, bx, by = self.S(tip[0]), self.S(tip[1]), self.S(base[0]), self.S(base[1])
        px, py, rtip, width = self.S(pivot[0]), self.S(pivot[1]), self.S(rtip), self.S(width)
        L = math.hypot(tx - bx, ty - by)
        ux, uy = (tx - bx) / L, (ty - by) / L
        # the needle's pixels: near the line, darker than the face round it
        t = (self.xx - bx) * ux + (self.yy - by) * uy
        dist = np.abs((self.xx - bx) * uy - (self.yy - by) * ux)
        band = (t > -2) & (t < L + 2) & (dist < width)
        around = (t > 0) & (t < L) & (dist > width * 1.5) & (dist < width * 4)
        ref = np.median(self.src[around], axis=0)
        diff = np.abs(self.src - ref).sum(2)
        # a soft matte: how much darker than the face each pixel near the line is
        lum = self.src.mean(2)
        matte = np.clip(((lum - ref.mean()) if light else (ref.mean() - lum)) - threshold * 0.15, 0, None) / (threshold * 0.6)
        matte = np.clip(matte, 0, 1) * band
        m = (matte > 0.25).astype(np.uint8)
        needleColour = np.median(self.src[matte > 0.8], axis=0) if (matte > 0.8).any() else self.src[m > 0].mean(0)
        # (taken off: just the needle's own pixels, each filled across the needle from the face either side of it,
        #  so the scale's print it crossed - the 0, the arc - carries on through)
        nm = cv2.dilate(((matte > 0.12) & (t > -3) & (t < L + 3)).astype(np.uint8), np.ones((3, 3), np.uint8))
        self.across_fill(nm, (uy, -ux))
        # straighten: turn the photo round the needle's base so the needle stands upright, take its strip
        ang = math.degrees(math.atan2(ux, -uy))   # (its angle from straight up)
        M = cv2.getRotationMatrix2D((float(bx), float(by)), ang, 1.0)
        up = np.broadcast_to(needleColour, self.src.shape).astype(np.float32)
        upm = cv2.warpAffine(matte.astype(np.float32), M, (self.W, self.H), flags=cv2.INTER_LINEAR)
        Wn = int(math.ceil(width * 2.5)) | 1
        Ln = int(math.ceil(L)) + 2
        R = int(math.ceil(rtip)) + 4
        canvas = np.zeros((2 * R, 2 * R, 3), np.float32); ca = np.zeros((2 * R, 2 * R), np.float32)
        ix0, iy0 = int(round(bx)) - Wn // 2, int(round(by)) - Ln
        strip = up[iy0:iy0 + Ln + 1, ix0:ix0 + Wn]; sm = upm[iy0:iy0 + Ln + 1, ix0:ix0 + Wn]
        Y0 = R - int(round(rtip)) - 1
        canvas[Y0:Y0 + strip.shape[0], R - Wn // 2:R - Wn // 2 + Wn] = strip
        ca[Y0:Y0 + strip.shape[0], R - Wn // 2:R - Wn // 2 + Wn] = sm
        ca = cv2.GaussianBlur(ca, (3, 3), 0.6)
        self.save_part(key, canvas, ca, dict(info or {}, cx=int(round(px)), cy=int(round(py)), R=R, kind='needle',
                                              window=[round(self.S(v)) for v in window] if window else None))

    def lamp(self, key, cx, cy, r, lit_in_photo=True, glow=1.6, info=None, dark=0.28):
        """A lamp (an LED, a lit button's jewel): its lit look as a picture; the panel shows it dark."""
        cx, cy, r = self.S(cx), self.S(cy), self.S(r)
        R = int(math.ceil(r * glow)) + 2
        icx, icy = int(round(cx)), int(round(cy))
        d = np.hypot(*np.mgrid[-R:R, -R:R])
        crop = self.src[icy - R:icy + R, icx - R:icx + R].copy()
        ringv = crop[(d > r * glow - 1.5) & (d < r * glow + 0.5)]
        panel = np.median(ringv, axis=0) if len(ringv) else crop.mean((0, 1))
        core = np.clip((r - d) / 1.5 + 0.5, 0, 1)
        halo = np.clip((r * glow - d) / (r * (glow - 1) + 1e-3), 0, 1)
        if lit_in_photo:
            lit = crop
            off = panel + (crop - panel) * dark
        else:
            # (unlit in the photo: lit is the lens brightened, with a glow of its own colour)
            hue = crop[d < r].mean(0)
            hue = hue / max(1.0, hue.max()) * 255
            lit = np.clip(crop * 1.8 + hue * 0.5, 0, 255)
            off = crop
        alpha = np.maximum(core, halo * 0.85) if lit_in_photo else np.maximum(core, halo * 0.5)
        if not lit_in_photo:
            lit = lit * core[..., None] + hue * (1 - core[..., None])
        self.save_part(key, lit, alpha, dict(info or {}, cx=icx, cy=icy, R=R, kind='lamp'))
        patch = self.bg[icy - R:icy + R, icx - R:icx + R]
        b = halo[..., None]
        self.bg[icy - R:icy + R, icx - R:icx + R] = patch * (1 - b) + off * b

    def region(self, key, rect, info=None, darken_bg=None, alpha_from=None, from_bg=False):
        """A rectangle of the photo as a picture (a lit bar, a lit key): with darken_bg, the panel shows it that much darker."""
        x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
        crop = (self.bg if from_bg else self.src)[y0:y1, x0:x1].copy()
        a = np.ones(crop.shape[:2], np.float32)
        if alpha_from == 'bright':
            lum = crop.mean(2); a = np.clip((lum - np.percentile(lum, 30)) / 50, 0, 1)
        self.save_part(key, crop, a, dict(info or {}, x=x0, y=y0, w=x1 - x0, h=y1 - y0, kind='region'))
        if darken_bg is not None:
            self.bg[y0:y1, x0:x1] = self.bg[y0:y1, x0:x1] * (1 - a[..., None]) + (self.bg[y0:y1, x0:x1] * darken_bg) * a[..., None]

    def write(self, quality=93):
        x0, y0, x1, y1 = [int(round(self.S(v))) for v in self.crop]
        # (the photo's edges: lines of another colour - the backdrop, a bright rim - trimmed off)
        def edge(lines, inward):
            ref = np.median(np.stack([lines(inward * k) for k in range(10, 20)]), axis=0)
            cut = 0
            for k in range(0, 16):
                if np.abs(lines(inward * k) - ref).mean() > 22: cut = k + 1
            return cut
        lum = self.bg.mean(2)
        y0 += edge(lambda k: lum[y0 + k, x0:x1], 1); y1 -= edge(lambda k: lum[y1 - 1 + k, x0:x1], -1)
        x0 += edge(lambda k: lum[y0:y1, x0 + k], 1); x1 -= edge(lambda k: lum[y0:y1, x1 - 1 + k], -1)
        self.crop = (x0 / self.scale, y0 / self.scale, x1 / self.scale, y1 / self.scale)
        self.bg = self.bg[y0:y1, x0:x1]
        # the white backdrop showing through (round the panel, in the rack ears' holes): the rack's dark
        b = self.bg; l = b.mean(2); sat = b.max(2) - b.min(2)
        white = ((l > 236) & (sat < 16)).astype(np.uint8)
        n, lab, st, _ = cv2.connectedComponentsWithStats(white)
        h, w = l.shape
        drop = np.zeros_like(white)
        for i in range(1, n):
            x, y, ww, hh, area = st[i]
            touches = x == 0 or y == 0 or x + ww >= w or y + hh >= h
            inEar = (x < 90 or x + ww > w - 90) and area < 4000 and area > 30
            if touches or inEar:
                drop[lab == i] = 1
        if drop.any():
            a = cv2.GaussianBlur(cv2.dilate(drop, np.ones((3, 3), np.uint8)).astype(np.float32), (3, 3), 0.8)[..., None]
            self.bg = b * (1 - a) + np.array([20, 21, 24], np.float32) * a
        for p in self.layout['parts'].values():
            for k in ('cx', 'x'):
                if k in p: p[k] -= x0
            for k in ('cy', 'y'):
                if k in p: p[k] -= y0
            if p.get('window'):
                p['window'] = [p['window'][0] - x0, p['window'][1] - y0, p['window'][2] - x0, p['window'][3] - y0]
        self.layout['size'] = [x1 - x0, y1 - y0]
        self.layout['origin'] = [x0, y0]   # (the panel's top left in the photo: for places given in the photo's pixels)
        Image.fromarray(np.clip(self.bg, 0, 255).astype(np.uint8)).save(f'{self.out}/{self.name}_panel.jpg', quality=quality, subsampling=0)
        self.layout['panel'] = f'{self.name}_panel_jpg'
        json.dump(self.layout, open(f'{self.out}/{self.name}.json', 'w'), indent=1)

    def preview(self, path, turns=None):
        """The panel with every part on it (turned by turns[key] degrees), for checking by eye."""
        im = Image.fromarray(np.clip(self.bg, 0, 255).astype(np.uint8)).convert('RGBA')
        for key, p in self.layout['parts'].items():
            sp = Image.open(f"{self.out}/{p['image'][:-4]}.png")
            if p['kind'] in ('turn', 'pointer', 'needle'):
                ang = (turns or {}).get(key, 0.0)
                if p['kind'] == 'needle':
                    ang = (turns or {}).get(key, 0.0)
                    win = p.get('window')
                    rot = sp.rotate(-ang, resample=Image.BICUBIC)
                    layer = Image.new('RGBA', im.size, (0, 0, 0, 0))
                    layer.alpha_composite(rot, (p['cx'] - p['R'], p['cy'] - p['R'])) if p['cx'] - p['R'] >= 0 and p['cy'] - p['R'] >= 0 else layer.paste(rot, (p['cx'] - p['R'], p['cy'] - p['R']), rot)
                    if win:
                        mask = Image.new('L', im.size, 0); mask.paste(255, tuple(win))
                        layer.putalpha(Image.fromarray(np.minimum(np.array(layer.split()[3]), np.array(mask))))
                    im.alpha_composite(layer)
                    continue
                rot = sp.rotate(-ang if p['kind'] != 'pointer' else -(ang - p['angle']) if key in (turns or {}) else 0, resample=Image.BICUBIC)
                im.alpha_composite(rot, (p['cx'] - p['R'], p['cy'] - p['R']))
            elif p['kind'] == 'lamp':
                if (turns or {}).get(key, 1):
                    im.alpha_composite(sp, (p['cx'] - p['R'], p['cy'] - p['R']))
            elif p['kind'] == 'region':
                if (turns or {}).get(key, 1):
                    im.alpha_composite(sp, (p['x'], p['y']))
        im.convert('RGB').save(path)

def _brushed(self, rect, keep=(), feather=3, ref_rect=None):
    """Refinishes a block of brushed metal (where a maker's name or number was): each row its own colour, from the
    clean panel in ref_rect (default: the block itself, its middle values), with the streaks of brushing; the
    keep rectangles (screws, buttons) stay as they are."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
    rx0, ry0, rx1, ry1 = [int(round(self.S(v))) for v in (ref_rect or rect)]
    block = self.bg[y0:y1, x0:x1]
    ref = self.src[y0:y1, rx0:rx1]
    lum = ref.mean(2)
    out = np.empty_like(block)
    for j in range(y1 - y0):
        row = ref[j]; l = lum[j]
        lo, hi = np.percentile(l, 30), np.percentile(l, 70)
        sel = row[(l >= lo) & (l <= hi)]
        out[j] = np.median(sel, axis=0) if len(sel) else row.mean(0)
    # streaks along the rows, and the light falling off across the block as on the photo
    streak = (cv2.GaussianBlur(self.rng.normal(0, 1, (y1 - y0, x1 - x0)).astype(np.float32), (61, 1), 0) * 5.0
              + cv2.GaussianBlur(self.rng.normal(0, 1, (y1 - y0, x1 - x0)).astype(np.float32), (9, 1), 0) * 6.0)
    # (the photo's own grain: how much its clean rows vary along themselves)
    grain = float(np.median(np.std(lum - cv2.GaussianBlur(lum, (0, 0), 6), axis=1)))
    streak *= max(0.5, grain / 3.0)
    colsh = np.zeros(x1 - x0, np.float32)
    out = out + (streak + colsh[None, :])[..., None] * (out / np.maximum(out.mean(2, keepdims=True), 1))
    m = np.ones((y1 - y0, x1 - x0), np.float32)
    for (kx0, ky0, kx1, ky1) in keep:
        kx0, ky0, kx1, ky1 = [int(round(self.S(v))) for v in (kx0, ky0, kx1, ky1)]
        m[max(0, ky0 - y0):max(0, ky1 - y0), max(0, kx0 - x0):max(0, kx1 - x0)] = 0
    m = cv2.GaussianBlur(m, (0, 0), feather)
    m = np.minimum(m, cv2.erode(np.ones_like(m), np.ones((3, 3))))
    self.bg[y0:y1, x0:x1] = block * (1 - m[..., None]) + out * m[..., None]

Face.brushed = _brushed

from PIL import ImageDraw, ImageFont
FONTS = {'sans': '/usr/share/fonts/truetype/freefont/FreeSans.ttf', 'sansbold': '/usr/share/fonts/truetype/freefont/FreeSansBold.ttf',
         'cond': '/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf', 'condbold': '/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf'}

def _relabel(self, rect, text, size, colour=None, font='sans', threshold=60, centre=None, erase=True, dilate=3):
    """Replaces the printing in rect with text: the old print taken off (filled from the panel round it), the new
    one in the old print's colour (or colour), size in pixels, centred on centre (default: the rect's middle)."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
    patch = self.bg[y0:y1, x0:x1]
    # (the print is the minority in the rect: what's lighter than its middle colour)
    ref = np.median(patch.reshape(-1, 3), axis=0)
    ink = (patch.mean(2) - ref.mean()) > threshold * 0.5
    if colour is None:
        colour = np.percentile(patch[ink], 75, axis=0) if ink.any() else np.array([230, 230, 230])
    if erase:
        m = np.zeros((self.H, self.W), np.uint8); m[y0:y1, x0:x1] = ink
        self.fill(cv2.dilate(m, np.ones((dilate, dilate), np.uint8)), radius=4)
    if not text:
        return
    S = 4
    fnt = ImageFont.truetype(FONTS[font], int(round(self.S(size) * S)))
    bb = fnt.getbbox(text)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    layer = Image.new('L', (tw + 8 * S, th + 8 * S), 0)
    ImageDraw.Draw(layer).text((4 * S - bb[0], 4 * S - bb[1]), text, font=fnt, fill=255)
    layer = layer.resize((max(1, layer.width // S), max(1, layer.height // S)), Image.LANCZOS)
    a = np.array(layer).astype(np.float32) / 255.0
    cx, cy = (self.S(centre[0]), self.S(centre[1])) if centre else ((x0 + x1) / 2, (y0 + y1) / 2)
    ox, oy = int(round(cx - a.shape[1] / 2)), int(round(cy - a.shape[0] / 2))
    h, w = a.shape
    sub = self.bg[oy:oy + h, ox:ox + w]
    a = a[:sub.shape[0], :sub.shape[1]]
    self.bg[oy:oy + h, ox:ox + w] = sub * (1 - a[..., None]) + np.array(colour, np.float32) * a[..., None]

def _lamp_mask(self, key, rect, sat=70, bright=110, glow=2.0, dark=0.22, info=None):
    """A lamp of any shape (a lit strip under a key, lit text): the bright coloured pixels in rect, as they're lit
    in the photo; the panel shows them dark."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
    patch = self.src[y0:y1, x0:x1]
    s = patch.max(2) - patch.min(2)
    m = ((s > sat) & (patch.max(2) > bright)).astype(np.float32)
    if not m.any():
        print('no lamp in', key); return
    m = cv2.dilate(m, np.ones((3, 3), np.uint8))
    halo = cv2.GaussianBlur(m, (0, 0), glow) * 0.8
    a = np.maximum(cv2.GaussianBlur(m, (3, 3), 0.6), halo)
    panel = np.median(patch[m < 0.5], axis=0) if (m < 0.5).any() else patch.mean((0, 1))
    self.save_part(key, patch, a, dict(info or {}, x=x0, y=y0, w=x1 - x0, h=y1 - y0, kind='region'))
    off = panel + (patch - panel) * dark
    self.bg[y0:y1, x0:x1] = self.bg[y0:y1, x0:x1] * (1 - a[..., None]) + off * a[..., None]

def _bar(self, key, rect, lit_rect=None, track=None, info=None):
    """A bar meter's segment column: the lit bar (from lit_rect, the photo's fullest one, stretched to fit) as its
    picture; the panel shows the track unlit (track: its colour, or sampled from the column's dark part)."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
    lx0, ly0, lx1, ly1 = [int(round(self.S(v))) for v in (lit_rect or rect)]
    lit = cv2.resize(self.src[ly0:ly1, lx0:lx1], (x1 - x0, y1 - y0), interpolation=cv2.INTER_CUBIC)
    self.save_part(key, lit, np.ones(lit.shape[:2], np.float32), dict(info or {}, x=x0, y=y0, w=x1 - x0, h=y1 - y0, kind='bar'))
    if track is None:
        col = self.src[y0:y1, x0:x1]; l = col.mean(2)
        track = np.median(col[l < np.percentile(l, 20) + 5], axis=0)
    self.bg[y0:y1, x0:x1] = np.array(track, np.float32)

def _ring(self, key, cx, cy, r0, r1, tint, boost=1.8, info=None, dim_lit=True):
    """A ring of printed numbers round a knob, the one at the knob's setting lit (as on the stepped gain): the
    ring with its numbers lit (tint) as the picture, drawn by the app only round the setting; the photo's lit
    number dimmed like the rest."""
    cx, cy, r0, r1 = self.S(cx), self.S(cy), self.S(r0), self.S(r1)
    R = int(math.ceil(r1)) + 2
    icx, icy = int(round(cx)), int(round(cy))
    d = np.hypot(*np.mgrid[-R:R, -R:R])
    ann = ((d > r0) & (d < r1)).astype(np.float32)
    patch = self.bg[icy - R:icy + R, icx - R:icx + R].copy()
    lum = patch.mean(2); s = patch.max(2) - patch.min(2)
    base = np.median(patch[ann > 0], axis=0)
    if dim_lit:
        litm = (s > 60) & (ann > 0)
        grey = np.repeat((lum * 0.55)[..., None], 3, 2)
        patch[litm] = grey[litm]
        self.bg[icy - R:icy + R, icx - R:icx + R] = patch
    ink = np.clip((patch.mean(2) - base.mean() - 12) / 40, 0, 1) * ann
    lit = np.array(tint, np.float32)[None, None, :] * np.clip(ink * boost, 0, 1)[..., None]
    a = np.clip(ink * 1.4, 0, 1)
    a = np.maximum(a, cv2.GaussianBlur(a, (0, 0), 2.0) * 0.6)
    self.save_part(key, lit, a, dict(info or {}, cx=icx, cy=icy, R=R, kind='ring'))

def _arc(self, key, cx, cy, r0, r1, lit_angle, unlit_angle, info=None, unlit_from=None, lit_from=None):
    """A lit arc round a knob (its value from the lowest setting): the lit look swept round the whole circle as the
    picture (the app draws it from the start to the setting); the panel's ring unlit all round."""
    cx, cy, r0, r1 = self.S(cx), self.S(cy), self.S(r0), self.S(r1)
    R = int(math.ceil(r1)) + 3
    icx, icy = int(round(cx)), int(round(cy))
    gy, gx = np.mgrid[-R:R, -R:R].astype(np.float32)
    d = np.hypot(gx, gy)
    ann = np.clip((d - r0) / 1.0 + 0.5, 0, 1) * np.clip((r1 - d) / 1.0 + 0.5, 0, 1)
    d = d.astype(np.float32)
    def sweep(angle, src=None):
        # the ring's look along one ray, by its place across the ring (src: another knob's ring, (cx, cy, r0, r1))
        scx, scy, sr0, sr1 = (self.S(src[0]), self.S(src[1]), self.S(src[2]), self.S(src[3])) if src else (cx, cy, r0, r1)
        t = (d - r0) / (r1 - r0)
        rs = sr0 + t * (sr1 - sr0)
        th = math.radians(angle)
        xs = (scx + rs * math.sin(th)).astype(np.float32); ys = (scy - rs * math.cos(th)).astype(np.float32)
        return cv2.remap(self.src, xs, ys, cv2.INTER_LINEAR)
    lit = sweep(lit_angle, lit_from); unlit = sweep(unlit_angle, unlit_from)
    self.save_part(key, lit, ann, dict(info or {}, cx=icx, cy=icy, R=R, kind='arc'))
    patch = self.bg[icy - R:icy + R, icx - R:icx + R]
    self.bg[icy - R:icy + R, icx - R:icx + R] = patch * (1 - ann[..., None]) + unlit * ann[..., None]

Face.relabel = _relabel
Face.lamp_mask = _lamp_mask
Face.bar = _bar
Face.ring = _ring
Face.arc = _arc

def _erase_ink(self, rect, t=25, dilate=3, radius=3, darker=True, size=21):
    """Takes off printing (darker or lighter than the surface round it, e.g. a logo on a meter's face): each pixel
    against the surface's local middle colour; filled from round it."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
    lum = np.clip(self.bg.mean(2), 0, 255).astype(np.uint8)
    local = cv2.medianBlur(lum, size | 1).astype(np.float32)
    diff = (local - lum) if darker else (lum - local)
    m = np.zeros((self.H, self.W), np.uint8)
    m[y0:y1, x0:x1] = (diff[y0:y1, x0:x1] > t)
    if dilate: m = cv2.dilate(m, np.ones((dilate, dilate), np.uint8))
    self.fill(m, radius=radius)
    return m

Face.erase_ink = _erase_ink

def _pressed(self, key, rect, k=0.55, info=None):
    """A push key as it looks pushed in: the photo's key, darker and a pixel lower (the app draws it on the chosen one)."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
    crop = self.bg[y0:y1, x0:x1].copy()
    pushed = np.roll(crop, 1, axis=0) * k
    pushed[0] = crop[0] * 0.3
    self.save_part(key, pushed, np.ones(crop.shape[:2], np.float32), dict(info or {}, x=x0, y=y0, w=x1 - x0, h=y1 - y0, kind='region'))

Face.pressed = _pressed

def _toggle(self, key, cx, cy, R, rn, threshold=40, info=None):
    """A bat toggle switch: as photographed (bat up) and thrown (bat down: the bat mirrored about the nut, the panel
    where it was) - two pictures, nothing turned (its light stays on the right side)."""
    cx, cy, R, rn = self.S(cx), self.S(cy), self.S(R), self.S(rn)
    Ri = int(math.ceil(R)) + 2
    icx, icy = int(round(cx)), int(round(cy))
    crop = self.src[icy - Ri:icy + Ri, icx - Ri:icx + Ri].copy()
    gy, gx = np.mgrid[-Ri:Ri, -Ri:Ri].astype(np.float32)
    d = np.hypot(gx, gy)
    # thrown: the whole switch mirrored top to bottom (its light stays on the same side); the bat a little darker
    down = crop[::-1].copy()
    top = (gy > rn * 0.3).astype(np.float32)[..., None]   # (where the bat is now)
    down = down * (1 - top * 0.12)
    alpha = np.clip((R - d) / 1.2 + 0.5, 0, 1)
    self.save_part(key + '_up', crop, alpha, dict(info or {}, cx=icx, cy=icy, R=Ri, kind='turn'))
    self.save_part(key + '_down', down, alpha, dict(info or {}, cx=icx, cy=icy, R=Ri, kind='turn'))

Face.toggle = _toggle

def _boost(self, key, gain=1.35, glow=(255, 60, 40), amount=0.5):
    """A lamp's lit picture made brighter, with a glow of its colour (a lamp that's lit only faintly in the photo)."""
    p = self.layout['parts'][key]
    fn = f"{self.out}/{p['image'][:-4]}.png"
    im = np.array(Image.open(fn)).astype(np.float32)
    rgb, a = im[..., :3], im[..., 3] / 255.0
    R = rgb.shape[0] / 2
    d = np.hypot(*np.mgrid[-R:R, -R:R])
    inner = (d < R * 0.62).astype(np.float32)
    rgb = rgb * (1 + (gain - 1) * inner[..., None]) + np.array(glow, np.float32) * inner[..., None] * 0.15
    halo = np.clip(1 - d / R, 0, 1) ** 1.5 * amount
    rgb = rgb * a[..., None] + np.array(glow, np.float32) * (1 - a[..., None])
    a = np.maximum(a, halo)
    Image.fromarray(np.dstack([np.clip(rgb, 0, 255), a * 255]).astype(np.uint8), 'RGBA').save(fn)

Face.boost = _boost

def _arc_over(self, key, cx, cy, r0, r1, lit_from, lit_angle, info=None):
    """A lit arc over a printed ring (the panel's ring left as it is): the lit look from another knob's ring."""
    cx, cy, r0, r1 = self.S(cx), self.S(cy), self.S(r0), self.S(r1)
    R = int(math.ceil(r1)) + 3
    icx, icy = int(round(cx)), int(round(cy))
    gy, gx = np.mgrid[-R:R, -R:R].astype(np.float32)
    d = np.hypot(gx, gy)
    ann = np.clip((d - r0) / 1.0 + 0.5, 0, 1) * np.clip((r1 - d) / 1.0 + 0.5, 0, 1)
    scx, scy, sr0, sr1 = [self.S(v) for v in lit_from]
    t = (d - r0) / (r1 - r0)
    rs = sr0 + t * (sr1 - sr0)
    th = math.radians(lit_angle)
    lit = cv2.remap(self.src, (scx + rs * math.sin(th)).astype(np.float32), (scy - rs * math.cos(th)).astype(np.float32), cv2.INTER_LINEAR)
    # (only the lit colour: how much brighter than dark it is)
    a = ann * np.clip((lit.max(2) - 40) / 120, 0, 1)
    self.save_part(key, lit, a, dict(info or {}, cx=icx, cy=icy, R=R, kind='arc'))

Face.arc_over = _arc_over

def _cap_mask(self, cx, cy, r, test):
    """The knob's own pixels round (cx, cy) within r (test: which pixels are the knob), holes filled, its largest piece."""
    cx, cy, r = self.S(cx), self.S(cy), self.S(r)
    d = np.hypot(self.xx - cx, self.yy - cy)
    A = self.src
    l = A.mean(2); sat = A.max(2) - A.min(2)
    m = (test(A, l, sat) & (d < r)).astype(np.uint8)
    m = cv2.morphologyEx(m, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    n, lab, st, cen = cv2.connectedComponentsWithStats(m)
    if n <= 1: raise RuntimeError('no cap')
    i = 1 + np.argmax(st[1:, 4]); m = (lab == i).astype(np.uint8)
    ff = m.copy(); fm = np.zeros((self.H + 2, self.W + 2), np.uint8); cv2.floodFill(ff, fm, (0, 0), 1); m = m | (1 - ff)
    return m

def _knob_cap(self, key, cx, cy, r, test, info=None, centre=True, grow=1):
    """A knob that turns whole, cut to its own outline (the panel round it stays): its centre found from that outline."""
    m = self._cap_mask(cx, cy, r, test)
    if grow: m = cv2.dilate(m, np.ones((grow * 2 + 1, grow * 2 + 1), np.uint8))
    ys, xs = np.nonzero(m)
    if centre:
        # (the outline's middle: from its widest extents, steadier than the centroid when a pointer pokes out)
        ccx, ccy = (xs.min() + xs.max()) / 2.0, (ys.min() + ys.max()) / 2.0
    else:
        ccx, ccy = self.S(cx), self.S(cy)
    R = int(math.ceil(max(np.hypot(xs - ccx, ys - ccy)))) + 2
    icx, icy = int(round(ccx)), int(round(ccy))
    a = cv2.GaussianBlur(m.astype(np.float32), (3, 3), 0.8)
    crop = self.src[icy - R:icy + R, icx - R:icx + R]
    self.save_part(key, crop, a[icy - R:icy + R, icx - R:icx + R], dict(info or {}, cx=icx, cy=icy, R=R, kind='turn'))
    # under it: the knob filled in from round it (covered, except where a turned pointer leaves its place)
    self.fill(cv2.dilate(m, np.ones((3, 3), np.uint8)), radius=5)
    return ccx / self.scale, ccy / self.scale

Face._cap_mask = _cap_mask
Face.knob_cap = _knob_cap

def _toggle2(self, key, cx, cy, R, rn, threshold=40, info=None, search=6):
    """A bat toggle, as photographed (bat up) and thrown (down): the switch mirrored top to bottom about its nut's
    middle, found from the nut's outline (the hex's widest rows)."""
    cx, cy, R = self.S(cx), self.S(cy), self.S(R)
    Ri = int(math.ceil(R)) + search + 2
    icx, icy = int(round(cx)), int(round(cy))
    crop = self.src[icy - Ri:icy + Ri, icx - Ri:icx + Ri]
    ring = np.concatenate([crop[0], crop[-1], crop[:, 0], crop[:, -1]])
    panel = np.median(ring, axis=0)
    diff = (np.abs(crop - panel).sum(2) > threshold).astype(np.uint8)
    diff = cv2.morphologyEx(diff, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    widths = diff.sum(1)
    wide = np.nonzero(widths > widths.max() * 0.8)[0]
    my = (wide.min() + wide.max()) / 2.0
    cols = np.nonzero(diff[wide].sum(0) > 0)[0]
    mx = (cols.min() + cols.max()) / 2.0
    ncx, ncy = icx - Ri + mx, icy - Ri + my
    Rt = int(math.ceil(R)) + 2
    jx, jy = int(round(ncx)), int(round(ncy))
    up = self.src[jy - Rt:jy + Rt, jx - Rt:jx + Rt].copy()
    gy, gx = np.mgrid[-Rt:Rt, -Rt:Rt].astype(np.float32)
    d = np.hypot(gx, gy)
    down = up[::-1].copy()
    # (the bat now hangs below: a little darker, it faces away from the light)
    down = down * (1 - (gy > rn).astype(np.float32)[..., None] * 0.12)
    alpha = np.clip((R - d) / 1.2 + 0.5, 0, 1)
    # the panel under it, clean (the switch drawn up or down over it): its own colour from round it
    m = np.zeros((self.H, self.W), np.uint8)
    cv2.circle(m, (jx, jy), int(math.ceil(R)) + 2, 1, -1)
    m[max(0, jy - int(R * 1.4)):jy, jx - int(R * 0.6):jx + int(R * 0.6)] = 1   # (the bat's tip, above)
    self.smooth_fill(m, sigma=(40, 14, 6), feather=2.0)
    clean = self.bg[jy - Rt:jy + Rt, jx - Rt:jx + Rt]
    # (each picture: the switch's own pixels over the clean panel, so nothing of the other way shows)
    def own(img):
        sw = (np.abs(img - clean).sum(2) > threshold).astype(np.uint8)
        sw = cv2.morphologyEx(sw, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))
        return np.minimum(alpha, cv2.GaussianBlur(cv2.dilate(sw, np.ones((3, 3), np.uint8)).astype(np.float32), (3, 3), 0.8))
    au = own(up)
    self.save_part(key + '_up', up, au, dict(info or {}, cx=jx, cy=jy, R=Rt, kind='turn'))
    self.save_part(key + '_down', down, au[::-1], dict(info or {}, cx=jx, cy=jy, R=Rt, kind='turn'))

Face.toggle = _toggle2

def _needle_fill(self, mask, pivot, delta, light=False, line=None):
    """The face where a needle was: the face turned a little round the needle's pivot. Each side of where the needle was
    takes the copy brought from its own side, so a scale changing colour under it (black up to 0, red after) carries
    on as printed, nothing mixed."""
    px, py = self.S(pivot[0]), self.S(pivot[1])
    M1 = cv2.getRotationMatrix2D((float(px), float(py)), delta, 1.0)
    M2 = cv2.getRotationMatrix2D((float(px), float(py)), -delta, 1.0)
    t1 = cv2.warpAffine(self.bg, M1, (self.W, self.H), flags=cv2.INTER_CUBIC)
    t2 = cv2.warpAffine(self.bg, M2, (self.W, self.H), flags=cv2.INTER_CUBIC)
    if line is None:
        fill = 0.5 * (t1 + t2)
    else:
        bx, by, ux, uy = line
        side = (self.xx - bx) * uy - (self.yy - by) * ux          # (which side of the needle's line each pixel is)
        # which copy brings pixels from the positive side: where a point on the line is fetched from
        inv = cv2.invertAffineTransform(M1)
        qx, qy = bx + ux * 20, by + uy * 20
        sx = inv[0, 0] * qx + inv[0, 1] * qy + inv[0, 2]; sy = inv[1, 0] * qx + inv[1, 1] * qy + inv[1, 2]
        positiveFromT1 = ((sx - bx) * uy - (sy - by) * ux) > 0
        pos = (side > 0)[..., None]
        fill = np.where(pos, t1 if positiveFromT1 else t2, t2 if positiveFromT1 else t1)
        # (a soft seam along the middle)
        w = np.clip(side / 1.5 * 0.5 + 0.5, 0, 1)[..., None]
        a, b2 = (t1, t2) if positiveFromT1 else (t2, t1)
        fill = a * w + b2 * (1 - w)
    m = cv2.GaussianBlur(mask.astype(np.float32), (0, 0), 1.0)
    m = np.maximum(m, mask.astype(np.float32))[..., None]
    self.bg = self.bg * (1 - m) + fill * m

Face.needle_fill = _needle_fill

def pointer_angle(path, cx, cy, r0, r1, kind, hint, spread=30, threshold=70):
    """Where a knob points in the photo (degrees clockwise from 12), without cutting anything."""
    f = Face('scratch', path, out='/tmp/claude-0/scratchface')
    return f.knob_pointer('p', cx, cy, r0, r1, kind, hint, spread=spread, threshold=threshold, fillmode='inpaint')


def _across_fill(self, mask, perp, reach=10):
    """Fills mask by interpolating, for each pixel, between the nearest unmasked pixels either side along perp."""
    px, py = perp
    ys, xs = np.nonzero(mask)
    src = self.bg.copy()
    H, W = mask.shape
    for y, x in zip(ys, xs):
        found = []
        for sgn in (1, -1):
            for k in range(1, reach + 1):
                qx, qy = int(round(x + sgn * px * k)), int(round(y + sgn * py * k))
                if not (0 <= qx < W and 0 <= qy < H): break
                if not mask[qy, qx]:
                    found.append((k, src[qy, qx])); break
        if len(found) == 2:
            (k1, c1), (k2, c2) = found
            if np.abs(c1 - c2).sum() > 90:
                # (an edge under the needle - the red arc starting: kept sharp, the nearer side's)
                self.bg[y, x] = c1 if k1 <= k2 else c2
            else:
                self.bg[y, x] = (c1 * k2 + c2 * k1) / (k1 + k2)
        elif found:
            self.bg[y, x] = found[0][1]

Face.across_fill = _across_fill

def _column_fill(self, rect, margin=3):
    """Fills a rectangle of a plain lit surface from the rows just above and below it, column by column (its
    light falling off top to bottom carries on), with a little grain."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
    top = np.median(self.bg[y0 - margin:y0, x0:x1], axis=0)
    bot = np.median(self.bg[y1:y1 + margin, x0:x1], axis=0)
    top = cv2.GaussianBlur(top[None], (0, 0), 6)[0]; bot = cv2.GaussianBlur(bot[None], (0, 0), 6)[0]
    t = np.linspace(0, 1, y1 - y0)[:, None, None]
    fill = top[None] * (1 - t) + bot[None] * t + self.rng.normal(0, 0.8, (y1 - y0, x1 - x0, 3))
    m = np.ones((y1 - y0, x1 - x0), np.float32)
    m = cv2.GaussianBlur(np.pad(m, 2)[2:-2, 2:-2] * 0 + m, (0, 0), 0.01)
    self.bg[y0:y1, x0:x1] = fill

Face.column_fill = _column_fill

def _shaded_fill(self, rect, ref_cols, feather=2.0):
    """Fills a rectangle of a lit face: each column's colour from the row just above it, darkening down the rect as the
    clean columns ref_cols (x0, x1) of the photo darken (the lamp's light falling off)."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
    rx0, rx1 = [int(round(self.S(v))) for v in ref_cols]
    prof = np.median(self.bg[y0 - 1:y1, rx0:rx1], axis=1)          # (rows y0-1 .. y1-1)
    gain = prof[1:] / np.maximum(prof[0], 1)
    top = cv2.GaussianBlur(np.median(self.bg[y0 - 3:y0, x0:x1], axis=0)[None], (0, 0), 3)[0]
    fill = top[None] * gain[:, None, :] + self.rng.normal(0, 0.8, (y1 - y0, x1 - x0, 3))
    m = np.zeros((self.H, self.W), np.float32); m[y0:y1, x0:x1] = 1
    m = cv2.GaussianBlur(m, (0, 0), feather); m = np.clip(m * 1.6, 0, 1)
    full = self.bg.copy(); full[y0:y1, x0:x1] = fill
    self.bg = self.bg * (1 - m[..., None]) + full * m[..., None]

Face.shaded_fill = _shaded_fill

def _lr_fill(self, rect, k=3, feather=1.2):
    """Fills a rectangle from the columns just left and right of it, row by row (a plain face: its shading goes on
    as it is either side), with a little grain."""
    x0, y0, x1, y1 = [int(round(self.S(v))) for v in rect]
    L = np.median(self.bg[y0:y1, x0 - k:x0], axis=1)
    R = np.median(self.bg[y0:y1, x1:x1 + k], axis=1)
    # (where one side has print on it - darker - the other side's colour)
    dl = L.mean(1) - R.mean(1)
    L = np.where((dl < -18)[:, None], R, L); R = np.where((dl > 18)[:, None], L, R)
    L = cv2.GaussianBlur(L[:, None], (0, 0), 1.5)[:, 0]; R = cv2.GaussianBlur(R[:, None], (0, 0), 1.5)[:, 0]
    t = np.linspace(0, 1, x1 - x0)[None, :, None]
    fill = L[:, None] * (1 - t) + R[:, None] * t + self.rng.normal(0, 0.8, (y1 - y0, x1 - x0, 3))
    m = np.zeros((self.H, self.W), np.float32); m[y0:y1, x0:x1] = 1
    m = np.clip(cv2.GaussianBlur(m, (0, 0), feather) * 1.5, 0, 1)
    full = self.bg.copy(); full[y0:y1, x0:x1] = fill
    self.bg = self.bg * (1 - m[..., None]) + full * m[..., None]

Face.lr_fill = _lr_fill

def _push_button(self, key, cx, cy, R, lamp=(255, 70, 40), clear=34, info=None):
    """Replaces a toggle with a push button (drawn): the switch taken off the panel, a chrome bezel with a dark cap,
    its lamp lit (on) or dark (off) - two pictures."""
    cx, cy, R = self.S(cx), self.S(cy), self.S(R)
    icx, icy = int(round(cx)), int(round(cy))
    m = np.zeros((self.H, self.W), np.uint8)
    cv2.circle(m, (icx, icy), int(self.S(clear)) + 2, 1, -1)
    m[max(0, icy - int(self.S(clear) * 1.45)):icy, icx - int(self.S(clear) * 0.7):icx + int(self.S(clear) * 1.35)] = 1
    self.smooth_fill(m, sigma=(40, 14, 6), feather=2.0)
    S = 4
    Ri = int(math.ceil(R * 1.35)) + 2
    size = Ri * 2 * S
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    c = size / 2.0
    d = np.hypot(xx - c, yy - c) / S
    ang = np.arctan2(yy - c, xx - c)
    def pic(on):
        rgb = np.zeros((size, size, 3), np.float32); a = np.zeros((size, size), np.float32)
        # soft shadow
        sh = np.clip(1 - (np.hypot(xx - c - 1.5 * S, yy - c - 2.5 * S) / S - R) / (R * 0.3), 0, 1) * 0.45
        # chrome bezel: light from the top left
        bez = (d <= R)
        shade = 0.5 + 0.5 * np.cos(ang + 2.3)
        chrome = 150 + 90 * shade
        rgb[bez] = np.stack([chrome, chrome, chrome * 1.02], -1)[bez]
        # cap: dark, slightly domed
        capR = R * 0.74
        cap = d <= capR
        dome = np.clip(1 - (np.hypot(xx - c + capR * 0.3 * S, yy - c + capR * 0.35 * S) / S) / capR, 0, 1)
        capc = 22 + 40 * dome ** 2
        rgb[cap] = np.stack([capc, capc, capc * 1.05], -1)[cap]
        # lamp jewel in the cap
        lr = R * 0.42
        lamp_m = d <= lr
        lamp_c = np.array(lamp, np.float32)
        if on:
            core = np.clip(1 - d / lr, 0, 1)
            col = lamp_c[None, None, :] * (0.75 + 0.25 * core[..., None]) + 255 * (core[..., None] ** 3) * 0.6
        else:
            col = lamp_c[None, None, :] * 0.22 + 10
            col = np.broadcast_to(col, rgb.shape)
        rgb[lamp_m] = np.clip(col, 0, 255)[lamp_m]
        # the jewel's glint
        gl = np.hypot(xx - c + lr * 0.35 * S, yy - c + lr * 0.4 * S) / S < lr * 0.22
        rgb[gl] = rgb[gl] * 0.4 + 255 * 0.6
        a = np.clip((R - d) * S / 2 + 0.5, 0, 1)
        a = np.maximum(a, sh)
        rgb = np.where((d > R)[..., None], 0, rgb)
        if on:
            glow = np.clip(1 - d / (R * 1.3), 0, 1) ** 2 * 0.0
        img = np.dstack([rgb, a * 255]).clip(0, 255).astype(np.uint8)
        im = Image.fromarray(img, 'RGBA').resize((Ri * 2, Ri * 2), Image.LANCZOS)
        return np.array(im).astype(np.float32)
    for name, on in (('on', True), ('off', False)):
        p = pic(on)
        self.save_part(f'{key}_{name}', p[..., :3], p[..., 3] / 255.0, dict(info or {}, cx=icx, cy=icy, R=Ri, kind='turn'))

Face.push_button = _push_button
