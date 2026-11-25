#!/usr/bin/env python3
"""
Lindell photo-real renderer, vendored unchanged from lindell-tracker-2 (feature/analog-dsp).
Used as a library by render_assets.py; its own layout constants are overridden there.

Everything is defined in *photo coordinates* of the reference front-panel shot
(2000 x 196 px, a full 19" 1U panel). Assets are rendered at SCALE x that, so
the background is 4000 x 392 px - enough for a retina (2x) display at the
default 1500 px wide editor and above.

Outputs (in ../assets):
  panel.png          brushed-aluminium faceplate with all legends, scales,
                     meter window (unlit), ear slots, screws, drop shadows
  knob_strip.png     teal knob, 180 frames x 2 deg (vertical filmstrip)
  toggle_strip.png   black toggle cap, 2 frames (left / right)
  lamp_strip.png     power lamp, 2 frames (off / on)
  seg_green.png      lit meter segment (green) incl. glow
  seg_red.png        lit meter segment (red) incl. glow
  led_red.png        small round red LED (lit) incl. glow
and ../Source/Layout.h with every control position, so C++ and art stay in sync.

Requires: numpy, scipy, pillow.
"""
import math
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "assets")
os.makedirs(OUT, exist_ok=True)

SCALE = 2                       # asset px per photo px
PW, PH = 2000, 196              # photo coordinate space
W, H = PW * SCALE, PH * SCALE

FONT_REG = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
FONT_LOGO = os.path.join(ROOT, "third_party", "fonts", "UncialAntiqua-Regular.ttf")

INK = (38, 38, 40)
rng = np.random.default_rng(1234)

# --------------------------------------------------------------------------
# Layout (photo coordinates) - measured from the reference photograph
# --------------------------------------------------------------------------
PANEL = (3, 2, 1993, 193)               # x0, y0, x1, y1
KNOB_R = 39.0
KNOBS = {                               # centre x, centre y
    "threshold": (379.5, 97.0),
    "attack":    (521.5, 97.0),
    "release":   (663.5, 98.0),
    "knee":      (805.0, 98.0),
    "character": (956.0, 95.5),
    "gain":      (1776.5, 95.5),
}
LAMP = (211.0, 54.0, 32.0)              # cx, cy, bezel radius
TOGGLE_R = 19.5
TOGGLES = {
    "power":  (209.5, 137.5),
}
MIX = (1818.0, 170.0, 7.0)              # trim screw right of the GAIN legend: cx, cy, head radius
HPF = (318.0, 170.0, 7.0)               # sidechain HPF trim screw, left of the THRESHOLD legend
WINDOW_TOGGLE = (1640.0, 171.0, 0.62)   # small WINDOW switch on the main panel: cx, cy, scale of the toggle sprite

# ---- fold-down WINDOW panel (its own photo-px space: 2000 x WIN_H) --------
WIN_H = 196                                     # same height as the faceplate: the window overlays it
WIN_DISPLAY = (40.0, 12.0, 1262.0, 184.0)       # glass display
SPEC = (86.0, 24.0, 1246.0, 120.0)              # spectrum plot area (inside the glass)
GRH = (86.0, 140.0, 1246.0, 172.0)              # gain-reduction history plot area
WIN_SWITCH = (1925.0, 171.0)                    # WINDOW switch position while the window is open
SPEC_DB = 72.0                                  # spectrum: 0 .. -72 dBFS
GRH_DB = 24.0                                   # GR history: 0 .. 24 dB
SMALL_R = 19.0                                  # small knob radius
WIN_KNOBS = {                                   # id: (cx, cy)
    "scHpf": (1365.0, 66.0), "scSlope": (1365.0, 140.0),
    "scEq1Freq": (1535.0, 66.0), "scEq1Gain": (1535.0, 140.0),
    "scEq2Freq": (1705.0, 66.0), "scEq2Gain": (1705.0, 140.0),
}
SLOPE_ANGLES = [-90.0, -30.0, 30.0, 90.0]
SCREWS = [(132.5, 48.5), (132.5, 145.0), (1863.0, 46.5), (1863.0, 145.5)]
SLOTS = [  # x0,y0,x1,y1 ear slots
    (13, 14, 62, 39), (13, 153, 62, 178),
    (1934, 14, 1983, 39), (1934, 153, 1983, 178),
]
METER_WIN = (1056, 43, 1694, 91)
SEG_X0, SEG_PITCH, SEG_W = 1077.0, 24.76, 19.0
SEG_Y0, SEG_Y1 = 56.0, 68.5           # OUTPUT meter (own window)
SEG_LABEL_Y = 80.0
GR_WIN = (1056, 100, 1694, 148)        # same width as the OUTPUT window        # GAIN REDUCTION meter: its own window below
GR_Y0, GR_Y1 = 113.0, 125.5
GR_LABEL_Y = 137.0
NUM_GR = 22
SEG_LABELS = ["-21", "-18", "-16", "-14", "-12", "-10", "-9", "-8", "-7", "-6",
              "-5", "-4", "-3", "-2", "-1", "-.5", "0", ".5", "1", "2", "3", "4"]
CLIP_X0 = 1647.0
# knob sweep: position 0 at -150 deg, 10 at +150 deg (0 deg = 12 o'clock)
SWEEP = 150.0
# character switch: 16 detents, 22.5 deg apart, offset 11.25 from the cross
CHAR_R_DOT = 45.0
CHAR_NAMES = [  # clockwise from 12 o'clock (index = detent)
    "OPTICAL CLEAN", "OPTICAL V", "OPTICAL CV", "OPTICAL VV",
    "AIR CLEAN", "AIR VINTAGE", "AIR C-VCA", "AIR V-VCA",
    "SOFT C", "SOFT V", "SOFT CV", "SOFT VV",
    "HARD C", "HARD V", "HARD C-VCA", "HARD V-VCA",
]


def P(v):
    """photo px -> asset px"""
    return v * SCALE


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------
def box_blur_x(a, n):
    k = np.ones(n) / n
    return ndimage.convolve1d(a, k, axis=1, mode="wrap")


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def to_img(rgb, alpha=None):
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
    if alpha is None:
        return Image.fromarray(rgb, "RGB")
    a = np.clip(alpha * 255, 0, 255).astype(np.uint8)
    return Image.fromarray(np.dstack([rgb, a]), "RGBA")


def rounded_rect_mask(w, h, x0, y0, x1, y1, r, ss=4):
    """anti-aliased rounded rect coverage (float 0..1), coords in px"""
    img = Image.new("L", (w * ss, h * ss), 0)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([x0 * ss, y0 * ss, x1 * ss, y1 * ss], radius=r * ss, fill=255)
    img = img.resize((w, h), Image.LANCZOS)
    return np.asarray(img).astype(np.float32) / 255.0


def disc_mask(w, h, cx, cy, r, ss=4):
    img = Image.new("L", (w * ss, h * ss), 0)
    d = ImageDraw.Draw(img)
    d.ellipse([(cx - r) * ss, (cy - r) * ss, (cx + r) * ss, (cy + r) * ss], fill=255)
    img = img.resize((w, h), Image.LANCZOS)
    return np.asarray(img).astype(np.float32) / 255.0


def blend(dst, color, cov):
    """alpha-over a flat colour with coverage map onto float RGB image"""
    c = np.asarray(color, np.float32)
    dst[...] = dst * (1 - cov[..., None]) + c * cov[..., None]


# --------------------------------------------------------------------------
# text layer (printed legends). Drawn as a coverage mask at 4x then reduced.
# --------------------------------------------------------------------------
class Legend:
    SS = 2  # extra supersampling over the asset scale

    def __init__(self):
        self.k = SCALE * self.SS
        self.img = Image.new("L", (PW * self.k, PH * self.k), 0)
        self.d = ImageDraw.Draw(self.img)
        self._fonts = {}

    def font(self, path, cap):
        """font sized so that its cap height (in photo px) == cap"""
        key = (path, cap)
        if key not in self._fonts:
            probe = ImageFont.truetype(path, 1000)
            bb = probe.getbbox("H", anchor="ls")
            cap_ratio = -bb[1] / 1000.0
            self._fonts[key] = ImageFont.truetype(path, max(1, int(round(cap * self.k / cap_ratio))))
        return self._fonts[key]

    def text(self, s, x, yc, cap, align="c", bold=False, track=0.0, path=None, fill=255, weight=0.22):
        """x = anchor (left/centre/right per align), yc = vertical centre of caps"""
        f = self.font(path or (FONT_BOLD if bold else FONT_REG), cap)
        k = self.k
        base = (yc + cap / 2.0) * k
        # measure with tracking
        adv = [f.getlength(ch) for ch in s]
        width = sum(adv) + track * k * (len(s) - 1)
        if align == "c":
            x0 = x * k - width / 2
        elif align == "r":
            x0 = x * k - width
        else:
            x0 = x * k
        if track == 0:
            self.d.text((x0, base), s, font=f, fill=fill, anchor="ls", stroke_width=int(round(weight * k)), stroke_fill=fill)
        else:
            cx = x0
            for ch, a in zip(s, adv):
                self.d.text((cx, base), ch, font=f, fill=fill, anchor="ls", stroke_width=int(round(weight * k)), stroke_fill=fill)
                cx += a + track * k
        return width / k

    def dot(self, x, y, r):
        k = self.k
        self.d.ellipse([(x - r) * k, (y - r) * k, (x + r) * k, (y + r) * k], fill=255)

    def line(self, x0, y0, x1, y1, w):
        k = self.k
        self.d.line([(x0 * k, y0 * k), (x1 * k, y1 * k)], fill=255, width=max(1, int(w * k)))

    def polyline(self, pts, w):
        k = self.k
        self.d.line([(x * k, y * k) for x, y in pts], fill=255, width=max(1, int(round(w * k))), joint="curve")

    def mask(self):
        m = self.img.resize((W, H), Image.LANCZOS)
        return np.asarray(m).astype(np.float32) / 255.0


def polar(cx, cy, ang_deg, r):
    a = math.radians(ang_deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)


# --------------------------------------------------------------------------
# PANEL
# --------------------------------------------------------------------------
LOGOS = [  # (file, centre x, centre y, height) photo px - side by side on the right ear, between its slots
    ("logo_lsr.png", 1908.5, 96.0, 20.5),
    ("logo_l.png",   1960.5, 96.5, None),   # None: same pixel scale as the LSR mark (as in DB-SAT)
]


def place_logos(rgb, shadow_acc):
    """photographed 3-D letters, recoloured to black, set onto the panel with a contact shadow"""
    scale = None
    for fname, cx, cy, hgt in LOGOS:
        im = Image.open(os.path.join(ROOT, "third_party", "logos", fname)).convert("RGBA")
        if scale is None:
            scale = hgt / im.height
        w, h = im.width * scale, im.height * scale
        wpx, hpx = max(1, int(round(P(w)))), max(1, int(round(P(h))))
        im = im.resize((wpx, hpx), Image.LANCZOS)
        a = np.asarray(im).astype(np.float32)
        # black moulded finish: keep the photo's own shading (its relief) as a dark-grey range
        lum = a[..., :3].mean(axis=2)
        shade = np.clip((lum - 195.0) / 60.0, 0, 1)
        dark = 12.0 + 62.0 * shade ** 1.6
        a[..., :3] = np.dstack([dark, dark, dark * 1.03])
        ox, oy = int(round(P(cx - w / 2))), int(round(P(cy - h / 2)))
        m = np.zeros((H, W), np.float32)
        m[oy:oy + hpx, ox:ox + wpx] = a[..., 3] / 255.0
        shadow_acc += 0.55 * ndimage.shift(ndimage.gaussian_filter(m, P(1.2)), (P(1.3), P(0.2)), order=1)
        shadow_acc += 0.25 * ndimage.gaussian_filter(m, P(0.5))
        yield (ox, oy, a)


def render_panel():
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    u = xx / W
    v = yy / H

    # ---- brushed aluminium (albedo) -------------------------------------
    def streaks(length, amp):
        n = box_blur_x(rng.normal(size=(H, W)).astype(np.float32), length)
        return amp * n / n.std()
    brushed = streaks(160, 3.4) + streaks(40, 2.6) + streaks(9, 1.6) + streaks(900, 1.8)
    # occasional deeper hairline scratches from the brushing belt
    sc = (rng.random((H, W)) < 0.0012).astype(np.float32) * rng.normal(1.0, 0.4, (H, W)).astype(np.float32)
    sc = box_blur_x(sc, 420) * 420 / 30.0
    brushed += 7.0 * np.clip(sc, -1.5, 1.5)
    band = ndimage.gaussian_filter1d(rng.normal(size=H).astype(np.float32), 4)
    brushed += 2.0 * band[:, None] / band.std()
    # anisotropic sheen: brushed metal stretches the studio lights into soft
    # VERTICAL bands (across the grain). Inside a band the grain shows strongly.
    sheen = (0.95 * np.exp(-((u - 0.22) / 0.075) ** 2) + 0.70 * np.exp(-((u - 0.47) / 0.11) ** 2)
             + 1.00 * np.exp(-((u - 0.76) / 0.06) ** 2) + 0.45 * np.exp(-((u - 0.93) / 0.05) ** 2))
    sheen *= 0.55 + 0.45 * (1 - v)                      # the light is above: bands fade downward
    sheen += 0.12 * ndimage.gaussian_filter1d(rng.normal(size=W).astype(np.float32), 90)[None, :] * 8
    metal = 190.0 + 48.0 * sheen + brushed * (0.75 + 1.3 * sheen)      # brighter, stronger sheen bands
    rgb = np.dstack([metal * 1.000, metal * 0.990, metal * 0.968]).astype(np.float32)

    # ---- printed legends: silk-screen ink ---------------------------------
    L = Legend()
    knob_scales(L)
    char_scale(L)
    misc_legends(L)
    ink = L.mask()
    # screen-print edge: ink spreads and the mesh leaves a slightly ragged edge
    rough = ndimage.gaussian_filter(rng.normal(size=ink.shape).astype(np.float32), 0.7)
    ink = smoothstep(0.30, 0.70, ndimage.gaussian_filter(ink, 0.9) + 0.09 * rough)
    ink = ndimage.gaussian_filter(ink, 0.75)
    mottle = ndimage.gaussian_filter(rng.normal(size=ink.shape).astype(np.float32), 2.5)
    ink = np.clip(ink * (0.97 + 0.05 * mottle / (mottle.std() + 1e-6)), 0, 1)
    ink = np.maximum(ink, 0.10 * ndimage.gaussian_filter(ink, 2.0))      # faint bleed into the grain
    # the ink film is thin: the brushing still shows through it
    ink_col = np.array(INK, np.float32)[None, None, :] * (1.0 + 0.012 * brushed[..., None])
    ink_col = ink_col + (26.0 * sheen)[..., None]            # semi-gloss ink reflects the light bands too
    # printed film stands proud of the metal: a faint soft shadow just below it
    under = ndimage.shift(ndimage.gaussian_filter(ink, 1.6), (2.2, 0.4), order=1)
    rgb *= (1 - 0.20 * under * (1 - ink))[..., None]
    rgb = rgb * (1 - 0.94 * ink[..., None]) + ink_col * (0.94 * ink[..., None])
    # and its upper edges catch the key light
    gy = np.gradient(ndimage.gaussian_filter(ink, 0.8), axis=0)
    rgb += (55.0 * np.clip(gy, 0, None) * ink)[..., None]

    # ---- meter window, screws ------------------------------------------
    meter_window(rgb)
    for (cx, cy) in SCREWS:
        screw(rgb, cx, cy)
    trim_hole(rgb)

    # ---- shadows cast by everything standing proud of the panel ---------
    shadow = np.zeros((H, W), np.float32)

    def disc_shadow(items, r, off_y, blur, strength):
        img = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(img)
        for (cx, cy) in items:
            X, Y, RR = P(cx), P(cy + off_y), P(r)
            d.ellipse([X - RR, Y - RR, X + RR, Y + RR], fill=255)
        return strength * np.asarray(img.filter(ImageFilter.GaussianBlur(P(blur)))).astype(np.float32) / 255.0

    knobs = list(KNOBS.values())
    toggles = list(TOGGLES.values())
    # key light from above: long soft shadow straight down + tighter penumbra + contact
    shadow += disc_shadow(knobs, KNOB_R * 1.25, 16.0, 18.0, 0.42)     # broad pool, like the reference
    shadow += disc_shadow(knobs, KNOB_R * 0.98, 9.0, 8.0, 0.45)
    shadow += disc_shadow(knobs, KNOB_R * 1.0, 3.0, 3.0, 0.40)
    shadow += disc_shadow(knobs, KNOB_R * 1.03, 0.6, 1.2, 0.55)
    shadow += disc_shadow(toggles, TOGGLE_R * 0.80, 3.0, 2.5, 0.40)
    shadow += disc_shadow(toggles, TOGGLE_R * 0.84, 0.5, 1.0, 0.40)
    shadow += disc_shadow([(LAMP[0], LAMP[1])], LAMP[2] * 0.97, 3.0, 3.0, 0.40)
    shadow += disc_shadow([(MIX[0], MIX[1])], MIX[2] * 1.0, 2.0, 1.6, 0.35)
    shadow += disc_shadow([(HPF[0], HPF[1])], HPF[2] * 1.0, 2.0, 1.6, 0.35)
    shadow += disc_shadow([(WINDOW_TOGGLE[0], WINDOW_TOGGLE[1])], TOGGLE_R * 0.95 * WINDOW_TOGGLE[2], 4.0, 3.5, 0.45)
    logos = list(place_logos(rgb, shadow))
    rgb *= (1 - 0.66 * np.clip(shadow, 0, 1))[..., None]
    for (ox, oy, a) in logos:
        al = a[..., 3:4] / 255.0
        reg = rgb[oy:oy + a.shape[0], ox:ox + a.shape[1]]
        reg[...] = reg * (1 - al) + a[..., :3] * al

    # ---- studio lighting over the whole faceplate -------------------------
    # key from above: bright top, falling off toward the bottom edge, gentle
    # vignette at the ends, broad sheen band where the brushing catches the light
    light = 1.20 - 0.30 * v ** 1.25                           # key from above: bright top, darker bottom
    light *= 1 - 0.16 * np.abs(2 * u - 1) ** 2.4               # falls off toward both ends
    light *= 1 + 0.06 * np.exp(-((u - 0.5) / 0.30) ** 2 - ((v - 0.15) / 0.35) ** 2)   # hot spot, top centre
    rgb *= light[..., None]

    # ---- photographic shine: specular reflections (white, on top of everything)
    # 1) the studio softbox reflected in the faceplate: broad soft diagonal glare
    g = u * 0.95 + v * 0.30
    glare = 0.75 * np.exp(-((g - 0.60) / 0.09) ** 2) + 0.45 * np.exp(-((g - 0.18) / 0.06) ** 2)
    glare *= 0.65 + 0.35 * (1 - v)
    rgb += (34.0 * glare)[..., None]
    # the centre of the faceplate was a little hot: 15 % down there, edges unchanged
    rgb *= (1 - 0.15 * np.exp(-((u - 0.5) / 0.24) ** 2))[..., None]
    # 2) glass on the meter windows: lit top bevel + soft diagonal reflection streak
    yy2, xx2 = np.mgrid[0:H, 0:W].astype(np.float32)
    for (wx0, wy0, wx1, wy1) in (METER_WIN, GR_WIN):
        cov = rounded_rect_mask(W, H, P(wx0), P(wy0), P(wx1), P(wy1), P(6.5))
        lu = (xx2 - P(wx0)) / P(wx1 - wx0); lv = (yy2 - P(wy0)) / P(wy1 - wy0)
        streak = np.exp(-((lu * 0.30 + lv - 0.22) / 0.13) ** 2) * (lv < 0.6)
        inner = ndimage.distance_transform_edt(cov > 0.5).astype(np.float32)
        bevel = np.exp(-inner / P(1.0)) * smoothstep(0.6, 0.0, lv)
        rgb += (cov * (16.0 * streak + 40.0 * bevel))[..., None]

    # ---- panel outline, ear slots, rack behind ----------------------------
    x0, y0, x1, y1 = PANEL
    panel_cov = rounded_rect_mask(W, H, P(x0), P(y0), P(x1), P(y1), P(7))
    for (a_, b_, c_, d_) in SLOTS:
        panel_cov *= 1 - rounded_rect_mask(W, H, P(a_), P(b_), P(c_), P(d_), P((d_ - b_) / 2))
    dist = ndimage.distance_transform_edt(panel_cov > 0.5).astype(np.float32)
    edge = np.exp(-dist / P(1.4))
    gy = ndimage.sobel(ndimage.gaussian_filter(panel_cov, P(0.8)), axis=0)
    gx = ndimage.sobel(ndimage.gaussian_filter(panel_cov, P(0.8)), axis=1)
    facing = np.clip(gy * 2.0 + gx * 0.6, -1, 1)
    rgb *= (1 + 0.28 * facing * edge)[..., None]
    rgb *= (1 - 0.12 * edge)[..., None]

    back = np.zeros_like(rgb)
    rail = 22 + 10 * (1 - v) + 1.2 * streaks(9, 1.0)
    back[...] = np.dstack([rail, rail, rail * 1.04])
    back *= (1 - 0.80 * ndimage.gaussian_filter(panel_cov, P(2.2)))[..., None]

    out = back * (1 - panel_cov[..., None]) + rgb * panel_cov[..., None]
    out = photo_grade(out, contrast=1.14, bloom=0.45)        # brighter, more shine
    img = photo_finish(to_img(out), blur=0.85, grain=2.4, seed=3, sharpen=0.55)
    img.save(os.path.join(OUT, "panel.png"), optimize=True)
    return img


def knob_scales(L):
    numcap = 9.2
    for name in ["threshold", "attack", "release", "knee", "gain"]:
        cx, cy = KNOBS[name]
        # dots between numbers
        for k in range(10):
            ang = -SWEEP + 15 + 30 * k
            x, y = polar(cx, cy, ang, 56.5)
            L.dot(x, y, 1.75)
        # numbers
        for n in range(11):
            ang = -SWEEP + 30 * n
            label = str(n)
            if n == 0:
                label = {"threshold": "OFF", "attack": "FAST", "release": "FAST",
                         "knee": "GENTLE", "gain": "0"}[name]
            if n == 10:
                label = {"threshold": "10", "attack": "SLOW", "release": "SLOW",
                         "knee": "STEEP", "gain": "10"}[name]
            if label.isdigit():
                x, y = polar(cx, cy, ang, 60.5)
                L.text(label, x, y, numcap)
            else:
                # word labels sit under the end-stops, slightly inward
                x, y = polar(cx, cy, ang, 57.0)
                y = cy + 52.5
                x = cx + (-1 if n == 0 else 1) * 33.0
                L.text(label, x, y, 7.4)
        L.text(name.upper(), cx, cy + 70.5, 10.6)


def char_scale(L):
    cx, cy = KNOBS["character"]
    # cross hair lines
    L.line(cx, 26, cx, 56.5, 1.25)
    L.line(cx, 136.5, cx, 158, 1.25)
    L.line(896, cy + 0.5, 918.5, cy + 0.5, 1.25)
    L.line(993.5, cy + 0.5, 1018, cy + 0.5, 1.25)
    for k in range(16):
        ang = 11.25 + 22.5 * k
        x, y = polar(cx, cy, ang, CHAR_R_DOT)
        L.dot(x, y, 1.5)
    cap = 7.4
    # (text, anchor x, centre y, align)
    labels = [
        ("CLEAN", 965.0, 40.0, "l"), ("V", 990.5, 57.5, "l"), ("CV", 1003.5, 71.0, "l"),
        ("VV", 1010.0, 87.5, "l"),
        ("CLEAN", 1010.0, 106.0, "l"), ("VINTAGE", 1004.0, 124.0, "l"),
        ("C-VCA", 990.0, 137.5, "l"), ("V-VCA", 972.5, 151.0, "l"),
        ("C", 942.5, 151.0, "r"), ("V", 925.5, 137.5, "r"), ("CV", 911.5, 124.0, "r"),
        ("VV", 906.5, 106.0, "r"),
        ("C", 905.5, 87.5, "r"), ("V", 912.5, 71.0, "r"), ("C-VCA", 924.5, 57.5, "r"),
        ("V-VCA", 947.0, 40.0, "r"),
    ]
    for s, x, y, a in labels:
        L.text(s, x, y, cap, align=a)
    L.text("HARD", 892.5, 18.8, 8.4)
    L.text("OPTICAL", 1020.0, 18.8, 8.4)
    L.text("SOFT", 891.0, 145.5, 8.4)
    L.text("AIR", 1030.0, 155.0, 8.4, align="l")
    L.text("OPTICAL", 1030.0, 166.0, 8.4, align="l")
    L.text("CHARACTER", cx, 170.5, 10.6)


def misc_legends(L):
    # logo
    L.text("TRACKER", 22.5, 96.0, 22.0, align="l", path=FONT_LOGO, track=1.2, weight=0.55)
    L.text("POWER", 213.5, 103.0, 9.0)
    L.text("ON", 180.0, 136.5, 8.4)
    L.text("OFF", 249.5, 136.5, 8.4)
    # brand
    L.text("LINDELL PLUGINS", 1374.0, 25.0, 13.6, track=0.9, weight=0.12)
    # toggles
    mix_scale(L)
    L.text("WINDOW", WINDOW_TOGGLE[0] - 50.0, WINDOW_TOGGLE[1] + 0.5, 7.8)
    # (no signature)


def mix_scale(L):
    screw_scale(L, MIX, "MIX", "DRY", "WET", name_dx=27.0)
    screw_scale(L, HPF, "SC HPF", "0", "300", name_dx=-31.0)


def screw_scale(L, pos, name, lo, hi, name_dx):
    """printed tick scale around a trim screw"""
    cx, cy, rh = pos
    for k in range(11):
        ang = -SWEEP + 30 * k
        major = k in (0, 5, 10)
        x0, y0 = polar(cx, cy, ang, rh + 2.8)
        x1, y1 = polar(cx, cy, ang, rh + (6.4 if major else 4.8))
        L.line(x0, y0, x1, y1, 1.1 if major else 0.8)
    L.text(name, cx + name_dx, cy + 1.0, 7.8)
    xd, yd = polar(cx, cy, -SWEEP, rh + 11.0)
    xw, yw = polar(cx, cy, SWEEP, rh + 11.0)
    L.text(lo, xd - 4.0, yd + 1.0, 5.8)
    L.text(hi, xw + 4.0, yw + 1.0, 5.8)


def signature(L):
    """A generic hand-drawn pen flourish (not a reproduction of anyone's signature)."""
    pts = []
    x0, y0 = 1856.0, 96.0
    for i in range(700):
        t = i / 699.0
        x = x0 + 112 * t + 4.5 * math.sin(t * 2 * math.pi * 7.0)
        y = y0 + 10.0 * math.sin(t * 2 * math.pi * 7.0 + 1.2) * (0.55 + 0.45 * math.sin(t * 9)) \
            - 9.0 * t + 4 * math.sin(t * 3.1)
        pts.append((x, y))
    L.polyline(pts, 1.05)
    L.polyline([(1900, 104), (1916, 99), (1946, 93)], 0.9)


def meter_window(rgb):
    glass_window(rgb, METER_WIN)
    glass_window(rgb, GR_WIN)
    meter_contents(rgb)


def glass_window(rgb, rect):
    x0, y0, x1, y1 = rect
    cov = rounded_rect_mask(W, H, P(x0), P(y0), P(x1), P(y1), P(6.5))
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    v = (yy - P(y0)) / P(y1 - y0)
    # dark smoked window, faint reflection gradient
    base = 13 + 10 * np.exp(-((v - 0.08) ** 2) / 0.01) + 4 * (1 - v)
    win = np.dstack([base, base, base * 1.05])
    # raised bezel highlight along bottom-ish edge, inner shadow at top
    inner = ndimage.distance_transform_edt(cov > 0.5).astype(np.float32)
    win *= (1 - 0.5 * np.exp(-inner / P(2.0)))[..., None]
    blend(rgb, (0, 0, 0), np.zeros_like(cov))
    rgb[...] = rgb * (1 - cov[..., None]) + win * cov[..., None]
    # thin light rim just outside the window (panel cut edge)
    ring = ndimage.gaussian_filter(cov, P(0.6)) - cov
    rgb *= (1 - 0.35 * np.clip(ring * 3, 0, 1))[..., None]



def meter_contents(rgb):
    # unlit segments: OUTPUT row (green / red) and GAIN REDUCTION row (amber)
    for i in range(22):
        sx = SEG_X0 + i * SEG_PITCH
        seg_off(rgb, sx, SEG_Y0, sx + SEG_W, SEG_Y1, "red" if i >= 16 else "green")
    seg_off(rgb, CLIP_X0, SEG_Y0, CLIP_X0 + SEG_W, SEG_Y1, "red")
    for i in range(NUM_GR):
        sx = SEG_X0 + i * SEG_PITCH
        seg_off(rgb, sx, GR_Y0, sx + SEG_W, GR_Y1, "green")

    # labels (white printed on the glass)
    Lw = Legend()
    for i, s in enumerate(SEG_LABELS):
        cx = SEG_X0 + i * SEG_PITCH + SEG_W / 2
        Lw.text(s, cx + (0.6 if s.startswith("-") else 0), SEG_LABEL_Y, 6.9, bold=True)
    Lw.text("CLIP", CLIP_X0 + SEG_W / 2 + 0.5, SEG_LABEL_Y, 6.9, bold=True)
    for i in range(NUM_GR):                   # GR in dB, 1 at the far right, counting up to the left
        cx = SEG_X0 + i * SEG_PITCH + SEG_W / 2
        Lw.text(str(NUM_GR - i), cx, GR_LABEL_Y, 6.9, bold=True)
    Lw.text("OUT", 1066.0, 0.5 * (SEG_Y0 + SEG_Y1), 5.6, bold=True)
    Lw.text("GR", 1066.0, 0.5 * (GR_Y0 + GR_Y1), 5.6, bold=True)
    Lw.text("dB", 1066.0, GR_LABEL_Y, 5.6, bold=True)
    m = Lw.mask()
    rough = ndimage.gaussian_filter(rng.normal(size=m.shape).astype(np.float32), 0.7)
    m = ndimage.gaussian_filter(smoothstep(0.30, 0.70, ndimage.gaussian_filter(m, 0.9) + 0.09 * rough), 0.55)
    blend(rgb, (206, 206, 200), m * 0.90)


def seg_off(rgb, x0, y0, x1, y1, kind):
    cov = rounded_rect_mask(W, H, P(x0), P(y0), P(x1), P(y1), P(0.8))
    col = np.array({"red": (74, 30, 36), "green": (40, 56, 48), "amber": (72, 54, 26)}[kind], np.float32)
    yy = np.mgrid[0:H, 0:1][0].astype(np.float32)
    v = np.clip((yy - P(y0)) / P(y1 - y0), 0, 1)
    shade = (1.15 - 0.35 * v)        # diffuser catches light at top
    seg = col[None, None, :] * shade[..., None]
    rgb[...] = rgb * (1 - cov[..., None]) + seg * cov[..., None]


def trim_hole(rgb):
    for pos in (MIX, HPF):
        _trim_hole(rgb, pos)


def _trim_hole(rgb, pos):
    """the drilled hole a trim screw sits in: dark recess with a bright chamfer"""
    cx, cy, rh = pos
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    dx, dy = (xx - P(cx)) / P(rh), (yy - P(cy)) / P(rh)
    ang = np.arctan2(dy, dx)
    ring = disc_mask(W, H, P(cx), P(cy), P(rh * 1.28)) - disc_mask(W, H, P(cx), P(cy), P(rh * 1.08))
    ringc = 150 + 70 * np.sin(ang)        # chamfer: lit at the bottom edge (faces the key light)
    rgb[...] = rgb * (1 - ring[..., None]) + np.dstack([ringc, ringc * 0.99, ringc * 0.96]) * ring[..., None]
    hole = disc_mask(W, H, P(cx), P(cy), P(rh * 1.08))
    blend(rgb, (18, 17, 17), hole)


def screw(rgb, cx, cy):
    """dark hole plug / standoff as in the photo"""
    R = 16.5
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    dx, dy = (xx - P(cx)) / P(R), (yy - P(cy)) / P(R)
    r = np.sqrt(dx * dx + dy * dy)
    # chamfer ring (bright on top, dark at bottom)
    ring = disc_mask(W, H, P(cx), P(cy), P(R)) - disc_mask(W, H, P(cx), P(cy), P(R * 0.86))
    ang = np.arctan2(dy, dx)
    ringc = 175 - 70 * np.sin(ang)
    blend(rgb, (0, 0, 0), np.zeros_like(ring))
    rgb[...] = rgb * (1 - ring[..., None]) + np.dstack([ringc, ringc * 0.98, ringc * 0.95]) * ring[..., None]
    # plug body: very dark brown, dome shading
    body = disc_mask(W, H, P(cx), P(cy), P(R * 0.86))
    nz = np.sqrt(np.clip(1 - (r / 0.86) ** 2, 0, 1))
    shade = 22 + 40 * np.clip(-dy * 0.6 - dx * 0.3 + 0.3, 0, 1) * nz + 10 * nz
    col = np.dstack([shade * 1.25, shade * 0.95, shade * 0.95])
    rgb[...] = rgb * (1 - body[..., None]) + col * body[..., None]
    # specular glint
    spec = np.exp(-(((dx + 0.32) ** 2 + (dy + 0.38) ** 2) / 0.02))
    rgb += (spec * body * 55)[..., None]
    # centre recess
    hole = disc_mask(W, H, P(cx), P(cy), P(3.2))
    blend(rgb, (8, 6, 6), hole)


# --------------------------------------------------------------------------
# PHOTOGRAPHIC SHADING
# A product-photography studio: one large overhead softbox (key light, its
# rectangular reflection is what makes plastic and glass read as real), a
# narrow strip light at the left, a dim card at the right, a dark room and
# the grey panel below bouncing a little light back up.
# --------------------------------------------------------------------------
LIGHT = np.array([-0.22, -0.78, 0.58])           # key light: above, slightly left
LIGHT /= np.linalg.norm(LIGHT)
VIEW = np.array([0.0, 0.0, 1.0])


def _norm(v):
    v = np.asarray(v, np.float64)
    return v / np.linalg.norm(v)


def _rect_light(rx, ry, rz, centre, right, half_w, half_h, soft):
    c = _norm(centre)
    rt = _norm(right)
    up = np.cross(c, rt)
    d = rx * c[0] + ry * c[1] + rz * c[2]
    x = (rx * rt[0] + ry * rt[1] + rz * rt[2]) / np.maximum(d, 1e-3)
    y = (rx * up[0] + ry * up[1] + rz * up[2]) / np.maximum(d, 1e-3)
    m = (1 - smoothstep(half_w - soft, half_w + soft, np.abs(x))) * \
        (1 - smoothstep(half_h - soft, half_h + soft, np.abs(y)))
    # softbox diffusion: brighter in the middle
    m *= 0.82 + 0.18 * np.exp(-(x / half_w) ** 2 - (y / half_h) ** 2)
    return m * (d > 0.05)


def studio_env(rx, ry, rz, soft=0.0, strip_gain=1.0, key_size=1.0):
    """radiance seen along reflection direction (rx,ry,rz); soft widens the edges (roughness)"""
    key = _rect_light(rx, ry, rz, (-0.18, -0.80, 0.57), (1, 0, 0), 0.85 * key_size, 0.30 * key_size, 0.05 + soft)
    strip = _rect_light(rx, ry, rz, (-0.88, -0.22, 0.42), (0, 1, 0), 0.75, 0.12, 0.04 + soft)
    card = _rect_light(rx, ry, rz, (0.80, -0.30, 0.52), (0, 1, 0), 0.40, 0.30, 0.10 + soft)
    room = 0.05 + 0.07 * np.clip(-ry, 0, 1)
    floor = 0.10 * smoothstep(0.2, 0.8, ry)            # the panel below bounces light up
    return room + floor + 9.0 * key + 3.0 * strip_gain * strip + 0.9 * strip_gain * card      # HDR


def reflect_view(n):
    nx, ny, nz = n[..., 0], n[..., 1], n[..., 2]
    return 2 * nz * nx, 2 * nz * ny, 2 * nz * nz - 1


def shade_photo(n, albedo, f0=0.045, gloss=1.0, ao=None, shadow=None, soft=0.0, wrap=0.25,
                light=None, strip_gain=1.0, key_size=1.0, gloss_map=None):
    """dielectric (plastic / lacquer): diffuse under the key + fill, Fresnel reflection of the studio"""
    rx, ry, rz = reflect_view(n)
    env = studio_env(rx, ry, rz, soft, strip_gain, key_size)
    nz = np.clip(n[..., 2], 0, 1)
    fres = f0 + (1 - f0) * (1 - nz) ** 5
    Lv = LIGHT if light is None else light
    ndl = (n * Lv).sum(2)
    key = np.clip((ndl + wrap) / (1 + wrap), 0, 1)
    if shadow is not None:
        key = key * shadow
    fill_dir = _norm((-0.88, -0.22, 0.42))
    fill = np.clip((n * fill_dir).sum(2), 0, 1)
    amb = 0.30 + 0.12 * np.clip(-n[..., 1], 0, 1)
    diff = amb + 0.85 * key + 0.22 * fill
    occ = ao if ao is not None else 1.0
    col = albedo[None, None, :] * (diff * occ)[..., None]
    spec = gloss * fres * env * (occ if ao is not None else 1.0)
    if gloss_map is not None:
        spec = spec * gloss_map
    if shadow is not None:
        spec = spec * (0.35 + 0.65 * shadow)
    col += 255 * spec[..., None]
    return col


def shade_metal(n, tint, rough=0.03, ao=None, front=1.0):
    """polished metal: almost pure, tinted reflection of the studio
    (+ the photographer's white card around the lens, so face-on chrome isn't black)"""
    rx, ry, rz = reflect_view(n)
    env = studio_env(rx, ry, rz, rough)
    card = _rect_light(rx, ry, rz, (0.0, -0.15, 1.0), (1, 0, 0), 0.55, 0.40, 0.25 + rough)
    ring = 0.35 * smoothstep(0.55, 0.95, -ry) * (rz > 0)
    panel = 0.75 * smoothstep(0.05, 0.65, ry)            # the brushed panel below, seen in the chrome
    env = env + front * (0.85 * card + ring) + panel
    # handled, polished chrome: faint scratches/smudges modulate the reflection
    smudge = ndimage.gaussian_filter(rng.normal(size=env.shape).astype(np.float32), 5.0)
    env = env * (1 + 0.035 * smudge / (smudge.std() + 1e-6))
    t = np.asarray(tint, np.float32) / 255.0
    col = 255 * np.clip(env, 0, 3)[..., None] * t[None, None, :] * 0.75
    if ao is not None:
        col *= ao[..., None]
    return col


def normals_from_height(h, px):
    gy, gx = np.gradient(h, px)
    n = np.dstack([-gx, -gy, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return n


def cast_shadow(hs, R, yy, xx, slope=0.62, steps=24, reach=0.55, light=None):
    """soft self-shadow of a heightfield from the key light"""
    Lv = LIGHT if light is None else light
    lx, ly = Lv[0], Lv[1]
    ll = math.hypot(lx, ly)
    out = np.zeros_like(hs)
    for dj in (-0.15, 0.0, 0.15):
        ca, sa = math.cos(dj), math.sin(dj)
        dx, dy = (lx * ca - ly * sa) / ll, (lx * sa + ly * ca) / ll
        lit = np.ones_like(hs)
        for k in range(1, steps + 1):
            tt = k * reach / steps
            hq = ndimage.map_coordinates(hs, [yy + dy * tt * R, xx + dx * tt * R], order=1, mode="nearest")
            lit = np.minimum(lit, np.clip(1 - (hq - (hs + tt * slope)) / 0.02, 0, 1))
        out += lit / 3.0
    return ndimage.gaussian_filter(out, R * 0.03)


def photo_grade(rgb, contrast=1.10, bloom=0.30):
    """the 'printed photograph' look: S-curve contrast, warm highlights / cool
    shadows, and a soft bloom around the brightest areas"""
    x = np.clip(rgb / 255.0, 0, 2.0)
    lum = x.mean(axis=2, keepdims=True)
    s = 0.5 + (lum - 0.5) * contrast
    s = s + 0.06 * np.sin(np.clip(lum, 0, 1) * np.pi * 2) * 0.25 * contrast       # gentle S
    x = x * (np.clip(s, 1e-4, None) / np.clip(lum, 1e-4, None))
    x = x * np.array([1.012, 1.0, 0.985])[None, None, :] * np.clip(lum, 0, 1) \
        + x * np.array([0.985, 1.0, 1.02])[None, None, :] * (1 - np.clip(lum, 0, 1))
    if bloom > 0:
        hi = np.clip(x.mean(axis=2) - 0.80, 0, None)
        glow = ndimage.gaussian_filter(hi, 6.0) + 0.5 * ndimage.gaussian_filter(hi, 18.0)
        x = x + bloom * glow[..., None]
    return x * 255.0


def photo_finish(img, blur=0.55, grain=1.6, seed=0, sharpen=0.45):
    """camera look: lens micro-blur, in-camera sharpening halo, luminance +
    chroma grain, filmic highlight roll-off"""
    rgba = img.mode == "RGBA"
    a = np.asarray(img).astype(np.float32)
    rgb = a[..., :3]
    if blur > 0:
        rgb = ndimage.gaussian_filter(rgb, (blur, blur, 0))
    if sharpen > 0:
        rgb = rgb + sharpen * (rgb - ndimage.gaussian_filter(rgb, (2.6, 2.6, 0)))
    cg = np.random.default_rng(seed + 7).normal(size=rgb.shape).astype(np.float32)
    rgb = rgb + 0.9 * ndimage.gaussian_filter(cg, (1.2, 1.2, 0))
    g = np.random.default_rng(seed).normal(size=rgb.shape[:2]).astype(np.float32)
    g = ndimage.gaussian_filter(g, 0.6) * 1.8
    rgb = rgb + grain * g[..., None]
    x = np.clip(rgb / 255.0, 0, 3.0)
    x = np.where(x > 0.80, 0.80 + (1 - np.exp(-(x - 0.80) / 0.20)) * 0.20, x)
    rgb = x * 255
    if rgba:
        al = a[..., 3:]
        if blur > 0:
            al = ndimage.gaussian_filter(al, (blur * 0.6, blur * 0.6, 0))
        return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), al]).astype(np.uint8), "RGBA")
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")


# --------------------------------------------------------------------------
# KNOB - glossy injection-moulded teal plastic.
# Tapered, knurled skirt (its side wall shows as a ring even head-on), domed
# cap with a waisted pointer bar between two lens-shaped dishes.
# --------------------------------------------------------------------------
KNOB_FRAME = 176           # px (asset), knob radius = 78 px
KNOB_FRAMES = 180
TEAL = np.array([14, 160, 126], np.float32)


def knob_frame(angle_deg, ss=3):
    N = KNOB_FRAME * ss
    R = KNOB_R * SCALE * ss
    c = (N - 1) / 2.0
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    u, v = (xx - c) / R, (yy - c) / R
    r = np.sqrt(u * u + v * v)
    th = np.arctan2(v, u)
    a = math.radians(angle_deg)
    px_, py_ = math.sin(a), -math.cos(a)             # pointer direction
    along = u * px_ + v * py_
    across = -u * py_ + v * px_

    CAP = 0.88
    KL = np.array([-0.56, -0.62, 0.55]); KL /= np.linalg.norm(KL)   # photo's key: upper left
    # ---- tapered skirt with a moulded knurl, rounded into the cap lip
    t = np.clip((r - CAP) / (1.0 - CAP), 0, 1)
    skirt = 0.44 * (1 - t ** 1.6)
    rib = 0.5 + 0.5 * np.cos(80 * (th - a))
    knurl = 0.020 * rib ** 1.5 * smoothstep(CAP - 0.005, CAP + 0.02, r) * (1 - smoothstep(0.95, 0.995, r))

    # ---- cap: shallow dome, wide flat-topped bar, broad soft dishes
    rc = np.clip(r / CAP, 0, 1)
    dome = 0.44 + 0.09 * (1 - rc ** 2) + 0.018 * np.exp(-((r - (CAP - 0.03)) / 0.02) ** 2)
    # two large dishes that meet in a THIN crest along the pointer axis
    bb = np.abs(across)
    dc, ea, eb = 0.46, 0.80, 0.47          # dish centre offset, semi-axes (along, across)
    dd = np.sqrt((along / ea) ** 2 + ((bb - dc) / eb) ** 2)
    depth = 0.20
    dish = depth * np.clip(1 - dd ** 2, 0, 1) ** 0.9
    # the crest itself: a narrow rounded ridge (~0.05 R wide)
    crest = 0.012 * np.exp(-(across / 0.035) ** 2) * (1 - smoothstep(0.55, 0.85, np.abs(along)))
    cap = dome - dish + crest
    lip = smoothstep(CAP - 0.05, CAP, r)
    cap = cap * (1 - lip) + 0.47 * lip
    capmask = 1 - smoothstep(CAP - 0.004, CAP + 0.008, r)
    h = skirt * (1 - capmask) + cap * capmask + knurl

    dr = np.sqrt((along - 0.75) ** 2 + across ** 2)
    dotm = 1 - smoothstep(0.088, 0.100, dr)
    dotring = np.exp(-((dr - 0.103) / 0.012) ** 2)
    h += 0.012 * np.sqrt(np.clip(1 - (dr / 0.095) ** 2, 0, 1))

    px = 1.0 / R
    hs = ndimage.gaussian_filter(h, R * 0.022)
    n = normals_from_height(hs, px)
    sh = 0.22 + 0.78 * cast_shadow(hs, R, yy, xx, slope=0.50, reach=0.65, light=KL)
    hb = ndimage.gaussian_filter(hs, R * 0.12)
    ao = np.clip(1 - 2.4 * np.clip(hb - hs, 0, None), 0.45, 1)
    ao *= 0.70 + 0.30 * smoothstep(0.0, 0.42, hs)

    # satin-gloss moulding: reflections strongest on convex crests and the lip,
    # concave dishes (rougher, and they see mostly the dark room) stay calm
    curv = ndimage.laplace(ndimage.gaussian_filter(hs, R * 0.04)) * (R * R * 0.02)
    gmap = np.clip(0.45 - 2.2 * curv, 0.18, 1.0)
    col = shade_photo(n, TEAL, f0=0.045, gloss=1.05, ao=ao, shadow=sh, soft=0.03,
                      light=KL, strip_gain=0.25, key_size=1.25, gloss_map=gmap)
    dotm = ndimage.gaussian_filter(dotm, R * 0.006)
    dotcol = shade_photo(n, np.array([175, 200, 214], np.float32), f0=0.05, gloss=0.4, ao=ao,
                         light=KL, strip_gain=0.25)
    col = col * (1 - dotm[..., None]) + dotcol * dotm[..., None]
    col *= (1 - 0.45 * dotring)[..., None]
    # photographic catch-light: crisp white glint of the key light on rims and crests
    hv = KL + np.array([0.0, 0.0, 1.0]); hv /= np.linalg.norm(hv)
    glint = np.clip((n * hv).sum(2), 0, 1) ** 160
    col += (255.0 * 0.75 * glint * sh)[..., None]

    alpha = 1 - smoothstep(1.0 - 1.2 / R * ss, 1.0, r)
    img = to_img(col, alpha).resize((KNOB_FRAME, KNOB_FRAME), Image.LANCZOS)
    return photo_finish(img, blur=0.45, grain=1.5, seed=int(angle_deg * 10))


def render_knob_strip():
    strip = Image.new("RGBA", (KNOB_FRAME, KNOB_FRAME * KNOB_FRAMES))
    for i in range(KNOB_FRAMES):
        strip.paste(knob_frame(i * 360.0 / KNOB_FRAMES), (0, i * KNOB_FRAME))
    strip.save(os.path.join(OUT, "knob_strip.png"), optimize=True)


# --------------------------------------------------------------------------
# TOGGLE - glossy black bat-handle cap seen head-on, lever left or right
# --------------------------------------------------------------------------
TOGGLE_FRAME = 96


def toggle_frame(side, ss=4):
    """bat toggle seen head-on: chrome hex nut + threaded bushing, chrome lever
    (the pin) thrown to the selected side, black glossy tip, its own shadow"""
    N = TOGGLE_FRAME * ss
    R = TOGGLE_R * SCALE * ss
    c = (N - 1) / 2.0
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    u, v = (xx - c) / R, (yy - c) / R
    r = np.sqrt(u * u + v * v)
    sgn = -1.0 if side == 0 else 1.0
    aa = 1.2 / R * ss

    def cov(d):  # anti-aliased coverage from a signed distance (in R units)
        return 1 - smoothstep(-aa, aa, d)

    layers = []   # (rgb, alpha) bottom -> top

    # -- hex nut (polished chrome, bevelled edges)
    hexd = np.maximum(np.abs(u) * 0.8660 + np.abs(v) * 0.5, np.abs(v))   # flats top/bottom
    nut_d = hexd - 0.74
    h_nut = 0.075 * smoothstep(0.0, 0.17, -nut_d)
    # -- threaded bushing
    bush_d = r - 0.50
    h_bush = 0.20 + 0.05 * np.sqrt(np.clip(1 - (r / 0.5) ** 2, 0, 1)) + 0.008 * np.sin(r * 95.0)
    h_base = np.where(bush_d < 0, h_bush, h_nut)
    hb = ndimage.gaussian_filter(h_base, R * 0.012)
    nb = normals_from_height(hb, 1.0 / R)
    base = shade_metal(nb, (62, 62, 66), rough=0.06, front=1.25)      # black-anodised nut
    base *= np.where(bush_d < 0, 0.80, 1.0)[..., None]
    hole = cov(r - 0.24)
    base = base * (1 - hole[..., None]) + np.array([10, 10, 10], np.float32) * hole[..., None]
    layers.append((base, cov(nut_d)))

    # -- shadow thrown by the lever and tip (soft, downward)
    tipx, tipy, tipr = sgn * 0.62, 0.03, 0.27
    seg_t = np.clip((u * sgn) / abs(tipx), 0, 1)
    shaft_d = np.sqrt((u - seg_t * tipx) ** 2 + (v - seg_t * tipy) ** 2) - (0.12 + 0.03 * seg_t)
    tip_d = np.sqrt((u - tipx) ** 2 + (v - tipy) ** 2) - tipr
    sh_m = np.maximum(cov(shaft_d), cov(tip_d))
    sh_m = ndimage.shift(ndimage.gaussian_filter(sh_m, R * 0.09), (R * 0.22, R * 0.04), order=1)
    layers.append((np.zeros((N, N, 3), np.float32), 0.55 * sh_m))

    # -- lever shaft: a ROUND chrome rod, tapering from the bushing to the tip.
    # Seen head-on it is foreshortened; the cross-section is a true cylinder,
    # so the reflection shows a bright streak along it and dark rolled-off edges.
    rad = 0.075 + 0.065 * seg_t
    dist = np.sqrt((u - seg_t * tipx) ** 2 + (v - seg_t * tipy) ** 2)
    q = np.clip(dist / rad, 0, 1)
    h_sh = 0.25 + 0.20 * seg_t + 1.6 * rad * np.sqrt(np.clip(1 - q ** 2, 0, 1))
    ns = normals_from_height(ndimage.gaussian_filter(h_sh, R * 0.006), 1.0 / R)
    shaft = shade_metal(ns, (84, 84, 90), rough=0.04, front=1.0)      # dark gunmetal lever
    shaft *= (1 - 0.55 * q ** 6)[..., None]                    # edges fall off into shadow
    shaft *= (0.70 + 0.30 * seg_t)[..., None]                  # base sits in the bushing's shade
    shaft_d = dist - rad
    layers.append((shaft, cov(shaft_d)))

    # -- black glossy tip (the bat's end, closest to the camera)
    dt = np.sqrt((u - tipx) ** 2 + (v - tipy) ** 2) / tipr
    h_tip = 0.70 + 0.30 * np.sqrt(np.clip(1 - dt ** 2, 0, 1))
    nt = normals_from_height(ndimage.gaussian_filter(h_tip, R * 0.01), 1.0 / R)
    tip = shade_photo(nt, np.array([26, 25, 25], np.float32), f0=0.05, gloss=0.9, soft=0.02)
    layers.append((tip, cov(tip_d)))

    rgb = np.zeros((N, N, 3), np.float32)
    alpha = np.zeros((N, N), np.float32)
    for col, al in layers:
        rgb = col * al[..., None] + rgb * (1 - al[..., None])
        alpha = al + alpha * (1 - al)
    img = to_img(rgb / np.maximum(alpha, 1e-4)[..., None] * (alpha > 0)[..., None], alpha)
    img = img.resize((TOGGLE_FRAME, TOGGLE_FRAME), Image.LANCZOS)
    return photo_finish(img, blur=0.4, grain=1.4, seed=7 + side)


def render_toggle_strip():
    strip = Image.new("RGBA", (TOGGLE_FRAME, TOGGLE_FRAME * 2))
    for i in range(2):
        strip.paste(toggle_frame(i), (0, i * TOGGLE_FRAME))
    strip.save(os.path.join(OUT, "toggle_strip.png"), optimize=True)


# --------------------------------------------------------------------------
# FOLD-DOWN WINDOW PANEL
# --------------------------------------------------------------------------
def _fmt_hz(f):
    return (f"{f / 1000:g}k" if f >= 1000 else f"{f:g}")


def spec_x(f):
    x0, _, x1, _ = SPEC
    return x0 + (x1 - x0) * math.log10(f / 20.0) / 3.0


def render_window():
    """the fold-down panel: same brushed aluminium, a big glass display with a
    printed grid, and the side-chain section with six small knobs"""
    global H, PH
    saveH, savePH = H, PH
    PH = WIN_H
    H = int(PH * SCALE)
    try:
        yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
        u, v = xx / W, yy / H

        def streaks(length, amp):
            n = box_blur_x(rng.normal(size=(H, W)).astype(np.float32), length)
            return amp * n / n.std()
        brushed = streaks(160, 3.4) + streaks(40, 2.6) + streaks(9, 1.6) + streaks(900, 1.8)
        sheen = (0.95 * np.exp(-((u - 0.22) / 0.075) ** 2) + 0.70 * np.exp(-((u - 0.47) / 0.11) ** 2)
                 + 1.00 * np.exp(-((u - 0.76) / 0.06) ** 2) + 0.45 * np.exp(-((u - 0.93) / 0.05) ** 2))
        sheen *= 0.55 + 0.45 * (1 - v)
        metal = 182.0 + 44.0 * sheen + brushed * (0.75 + 1.3 * sheen)
        rgb = np.dstack([metal * 1.000, metal * 0.990, metal * 0.968]).astype(np.float32)

        # ---- legends: section titles, knob scales, labels -------------------
        L = Legend()
        hx = WIN_KNOBS["scHpf"][0]
        e1 = WIN_KNOBS["scEq1Freq"][0]
        e2 = WIN_KNOBS["scEq2Freq"][0]
        L.text("SIDECHAIN HPF", hx, 16.0, 8.6)
        L.text("SC EQ 1", e1, 16.0, 8.6)
        L.text("SC EQ 2", e2, 16.0, 8.6)
        for xsep in ((hx + e1) / 2, (e1 + e2) / 2, e2 + 85.0):
            L.line(xsep, 10.0, xsep, 186.0, 1.0)
        L.text("WINDOW", WIN_SWITCH[0], WIN_SWITCH[1] - 24.0, 7.8)
        L.text("CLOSE", WIN_SWITCH[0] + 2.0, WIN_SWITCH[1] - 13.0, 6.0)

        def dial(cx, cy, lo, hi, name, mid=None):
            for k in range(11):
                x, y = polar(cx, cy, -SWEEP + 30 * k, SMALL_R + 8.0)
                L.dot(x, y, 1.25)
            xl, yl = polar(cx, cy, -SWEEP, SMALL_R + 15.0)
            xh, yh = polar(cx, cy, SWEEP, SMALL_R + 15.0)
            L.text(lo, xl - 3.0, yl, 6.4)
            L.text(hi, xh + 3.0, yh, 6.4)
            if mid:
                L.text(mid, cx, cy - SMALL_R - 15.0, 6.4)
            L.text(name, cx, cy + SMALL_R + 15.0, 7.0)

        dial(*WIN_KNOBS["scHpf"], "OFF", "300", "FREQ", "150")
        cx, cy = WIN_KNOBS["scSlope"]
        for ang, lab in zip(SLOPE_ANGLES, ("6", "12", "18", "24")):
            x, y = polar(cx, cy, ang, SMALL_R + 8.0)
            L.dot(x, y, 1.4)
            x, y = polar(cx, cy, ang, SMALL_R + 17.0)
            L.text(lab, x, y, 6.4)
        L.text("dB/OCT", cx, cy + SMALL_R + 15.0, 7.0)
        dial(*WIN_KNOBS["scEq1Freq"], "40", "8k", "FREQ", "570")
        dial(*WIN_KNOBS["scEq1Gain"], "-15", "+15", "GAIN", "0")
        dial(*WIN_KNOBS["scEq2Freq"], "40", "16k", "FREQ", "800")
        dial(*WIN_KNOBS["scEq2Gain"], "-15", "+15", "GAIN", "0")
        ink = ndimage.gaussian_filter(L.mask(), 0.75)
        ink_col = np.array(INK, np.float32)[None, None, :] + (26.0 * sheen)[..., None]
        rgb = rgb * (1 - 0.94 * ink[..., None]) + ink_col * (0.94 * ink[..., None])

        # ---- glass display with printed grid ------------------------------
        glass_window(rgb, WIN_DISPLAY)
        G = Legend()       # faint grid lines
        T = Legend()       # labels
        sx0, sy0, sx1, sy1 = SPEC
        for f in (50, 100, 200, 500, 1000, 2000, 5000, 10000):
            x = spec_x(f)
            G.line(x, sy0, x, sy1, 0.7)
            T.text(_fmt_hz(f), x, sy1 + 9.0, 6.0, bold=True)
        for d in range(0, int(SPEC_DB) + 1, 12):
            y = sy0 + (sy1 - sy0) * d / SPEC_DB
            G.line(sx0, y, sx1, y, 0.7)
            T.text(str(-d), sx0 - 6.0, y, 6.0, align="r", bold=True)
        gx0, gy0, gx1, gy1 = GRH
        for d in range(0, int(GRH_DB) + 1, 6):
            y = gy0 + (gy1 - gy0) * d / GRH_DB
            G.line(gx0, y, gx1, y, 0.7)
            T.text(str(d), gx0 - 6.0, y, 5.6, align="r", bold=True)
        T.text("dB", sx0 - 24.0, sy0 + 2.0, 6.0, bold=True)
        T.text("GR", sx0 - 36.0, (gy0 + gy1) / 2, 6.0, bold=True)
        T.text("SPECTRUM   IN / OUT", sx1 - 4.0, sy0 + 7.0, 6.0, align="r", bold=True)
        T.text("GAIN REDUCTION HISTORY", sx1 - 4.0, gy0 - 7.5, 6.0, align="r", bold=True)
        blend(rgb, (206, 206, 200), ndimage.gaussian_filter(G.mask(), 0.5) * 0.16)
        blend(rgb, (206, 206, 200), ndimage.gaussian_filter(T.mask(), 0.5) * 0.80)

        # ---- shadows of the small knobs ---------------------------------
        sh = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(sh)
        for (cx, cy) in WIN_KNOBS.values():
            X, Y, RR = P(cx + 1.0), P(cy + 4.0), P(SMALL_R)
            d.ellipse([X - RR, Y - RR, X + RR, Y + RR], fill=255)
        X, Y, RR = P(WIN_SWITCH[0] + 1.0), P(WIN_SWITCH[1] + 3.0), P(TOGGLE_R * 0.95 * WINDOW_TOGGLE[2])
        d.ellipse([X - RR, Y - RR, X + RR, Y + RR], fill=255)
        shadow = np.asarray(sh.filter(ImageFilter.GaussianBlur(P(5.0)))).astype(np.float32) / 255.0
        rgb *= (1 - 0.40 * shadow)[..., None]

        # ---- lighting, edges --------------------------------------------
        light = (1.16 - 0.28 * v ** 1.25) * (1 - 0.16 * np.abs(2 * u - 1) ** 2.4)
        light *= 1 - 0.15 * np.exp(-((u - 0.5) / 0.24) ** 2)
        rgb *= light[..., None]
        cov = rounded_rect_mask(W, H, P(3), P(1), P(1993), P(WIN_H - 2), P(7))
        dist = ndimage.distance_transform_edt(cov > 0.5).astype(np.float32)
        edge = np.exp(-dist / P(1.4))
        rgb *= (1 - 0.14 * edge)[..., None]
        back = np.full_like(rgb, 18.0)
        out = back * (1 - cov[..., None]) + rgb * cov[..., None]
        out = photo_grade(out, contrast=1.14, bloom=0.45)
        img = photo_finish(to_img(out), blur=0.85, grain=2.4, seed=17, sharpen=0.55)
        img.save(os.path.join(OUT, "window.png"), optimize=True)
        return img
    finally:
        H, PH = saveH, savePH


# --------------------------------------------------------------------------
# POWER LAMP - pilot light: thin polished metal ring, glossy black gasket,
# milky translucent green dome lens with window reflections. Frames: off, on
# --------------------------------------------------------------------------
LAMP_FRAME = 200


def lamp_frame(on, ss=3):
    N = LAMP_FRAME * ss
    Rb = LAMP[2] * SCALE * ss
    c = (N - 1) / 2.0
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    u, v = (xx - c) / Rb, (yy - c) / Rb
    r = np.sqrt(u * u + v * v)
    R_METAL, R_GASKET = 0.94, 0.80

    # geometry
    tm = np.clip((r - R_METAL) / (1 - R_METAL), 0, 1)
    h_metal = 0.10 * np.sin(np.pi * np.clip(tm * 0.9 + 0.1, 0, 1))
    tg = np.clip((r - R_GASKET) / (R_METAL - R_GASKET), 0, 1)
    h_gasket = 0.10 + 0.08 * np.sin(np.pi * tg)
    h_lens = 0.10 + 0.55 * np.sqrt(np.clip(1 - (r / R_GASKET) ** 2, 0, 1))
    h = np.where(r >= R_METAL, h_metal, np.where(r >= R_GASKET, h_gasket, h_lens))
    h = ndimage.gaussian_filter(h, Rb * 0.008)
    n = normals_from_height(h, 1.0 / Rb)

    metal = shade_metal(n, (205, 196, 175), rough=0.02)
    gasket = shade_photo(n, np.array([14, 14, 14], np.float32), f0=0.05, gloss=0.8, soft=0.02)

    # translucent milky lens: light scatters inside -> brighter core, darker rim
    rl = np.clip(r / R_GASKET, 0, 1)
    core = np.exp(-(((u) / 0.70) ** 2 + ((v + 0.05) / 0.66) ** 2))
    if on:
        edge_c = np.array([62, 178, 88], np.float32)
        mid_c = np.array([128, 226, 140], np.float32)
        hot_c = np.array([178, 244, 178], np.float32)
        body = edge_c + (mid_c - edge_c) * smoothstep(0.0, 0.6, core)[..., None] \
               + (hot_c - mid_c) * smoothstep(0.55, 1.0, core)[..., None]
    else:
        edge_c = np.array([22, 70, 32], np.float32)
        mid_c = np.array([58, 118, 64], np.float32)
        body = edge_c + (mid_c - edge_c) * core[..., None]
        body = body * (0.75 + 0.35 * np.clip((n * LIGHT).sum(2), 0, 1))[..., None]
    body *= (1 - 0.35 * smoothstep(0.80, 1.0, rl))[..., None]          # meniscus at the edge
    rx, ry, rz = reflect_view(n)
    env = studio_env(rx, ry, rz, 0.0)
    nz = np.clip(n[..., 2], 0, 1)
    fres = 0.05 + 0.95 * (1 - nz) ** 5
    lens = body + 255 * (fres * env * 0.85)[..., None]

    lensm = 1 - smoothstep(R_GASKET - 0.012, R_GASKET + 0.004, r)
    metm = smoothstep(R_METAL - 0.006, R_METAL + 0.01, r)
    col = gasket * (1 - metm[..., None]) + metal * metm[..., None]
    col = col * (1 - lensm[..., None]) + lens * lensm[..., None]
    # small occlusion where parts meet
    col *= (1 - 0.35 * np.exp(-((r - R_GASKET) / 0.02) ** 2))[..., None]

    body_a = 1 - smoothstep(1.0 - 1.5 / Rb * ss, 1.0, r)
    alpha = body_a
    if on:
        glow = np.exp(-((np.clip(r - 0.98, 0, None)) / 0.22) ** 2) * (r >= 0.99)
        gcol = np.array([110, 255, 150], np.float32)
        col = col * body_a[..., None] + gcol * (1 - body_a)[..., None]
        alpha = body_a + 0.12 * glow * (1 - body_a)
    img = to_img(col, alpha).resize((LAMP_FRAME, LAMP_FRAME), Image.LANCZOS)
    return photo_finish(img, blur=0.5, grain=1.5, seed=11 + int(on))


def render_lamp_strip():
    strip = Image.new("RGBA", (LAMP_FRAME, LAMP_FRAME * 2))
    for i in range(2):
        strip.paste(lamp_frame(i == 1), (0, i * LAMP_FRAME))
    strip.save(os.path.join(OUT, "lamp_strip.png"), optimize=True)


# --------------------------------------------------------------------------
# MIX TRIM SCREW - zinc-plated slotted cheese-head screw, turns with the value
# --------------------------------------------------------------------------
SCREW_FRAME = 48
SCREW_FRAMES = 180


def trim_screw_frame(angle_deg, ss=4):
    N = SCREW_FRAME * ss
    R = MIX[2] * SCALE * ss
    c = (N - 1) / 2.0
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    u, v = (xx - c) / R, (yy - c) / R
    r = np.sqrt(u * u + v * v)
    a = math.radians(angle_deg)
    px_, py_ = math.sin(a), -math.cos(a)
    across = -u * py_ + v * px_
    along = u * px_ + v * py_
    th = np.arctan2(v, u)
    h = 0.30 * np.sqrt(np.clip(1 - r ** 2, 0, 1)) ** 0.5 + 0.10       # slightly domed head
    slot_w = 0.13
    slot = (1 - smoothstep(slot_w - 0.03, slot_w + 0.01, np.abs(across))) * (1 - smoothstep(0.90, 0.99, np.abs(along)))
    h = h - 0.30 * slot
    h = ndimage.gaussian_filter(h, R * 0.025)
    n = normals_from_height(h, 1.0 / R)
    # spun / satin finish: fine circular grain (turns with the screw)
    grain = ndimage.gaussian_filter(rng.normal(size=h.shape).astype(np.float32), (R * 0.01, R * 0.01))
    spin = 1 + 0.05 * np.sin(r * 90.0 + 3.0 * grain) + 0.04 * grain
    satin = shade_photo(n, np.array([168, 168, 164], np.float32), f0=0.22, gloss=0.28, soft=0.22, wrap=0.4)
    col = satin * spin[..., None]
    # slot: deep and dark, with the lit wall catching a thin highlight
    floor = slot * smoothstep(0.0, 0.05, slot_w - np.abs(across))
    col *= (1 - 0.82 * floor)[..., None]
    # head edge rolls off into the dark recess (the darker outer ring in the photo)
    col *= (1 - 0.55 * smoothstep(0.80, 1.0, r))[..., None]
    alpha = 1 - smoothstep(1.0 - 1.5 / R * ss, 1.0, r)
    img = to_img(col, alpha).resize((SCREW_FRAME, SCREW_FRAME), Image.LANCZOS)
    return photo_finish(img, blur=0.45, grain=1.5, seed=31 + int(angle_deg))


def render_screw_strip():
    strip = Image.new("RGBA", (SCREW_FRAME, SCREW_FRAME * SCREW_FRAMES))
    for i in range(SCREW_FRAMES):
        strip.paste(trim_screw_frame(i * 360.0 / SCREW_FRAMES), (0, i * SCREW_FRAME))
    strip.save(os.path.join(OUT, "mix_strip.png"), optimize=True)


# --------------------------------------------------------------------------
# VALUE POPUP (MIX): smoked-glass plate + photographed digit glyphs
# --------------------------------------------------------------------------
POPUP_W, POPUP_H = 46.0, 18.0                         # photo px
GLYPHS = "0123456789%Hz"
GLYPH_CAP = 8.4
GLYPH_CELL = 12                                       # asset px per glyph cell (height = GLYPH_H)
GLYPH_H = 24


def glyph_advances():
    f = ImageFont.truetype(FONT_BOLD, 1000)
    cap_ratio = -f.getbbox("H", anchor="ls")[1] / 1000.0
    size = GLYPH_CAP / cap_ratio
    return [f.getlength(ch) / 1000.0 * size for ch in GLYPHS]


def render_popup():
    # plate
    w, h = int(P(POPUP_W)) + 16, int(P(POPUP_H)) + 16
    cov = rounded_rect_mask(w, h, 8, 8, w - 8, h - 8, P(4.0))
    yy = np.mgrid[0:h, 0:w][0].astype(np.float32)
    v = (yy - 8) / (h - 16)
    base = 22 + 18 * np.exp(-((v - 0.12) / 0.10) ** 2) + 6 * (1 - v)
    plate = np.dstack([base, base, base * 1.04])
    edge = ndimage.distance_transform_edt(cov > 0.5).astype(np.float32)
    plate *= (1 + 0.6 * np.exp(-edge / 1.2) * (v < 0.5) - 0.4 * np.exp(-edge / 1.5) * (v >= 0.5))[..., None]
    shadow = ndimage.shift(ndimage.gaussian_filter(cov, 3.0), (3, 0), order=1)
    alpha = np.clip(cov + 0.45 * shadow * (1 - cov), 0, 1)
    rgb = plate * cov[..., None] / np.maximum(alpha, 1e-4)[..., None]
    photo_finish(to_img(rgb, alpha), blur=0.4, grain=1.2, seed=91).save(os.path.join(OUT, "popup_plate.png"), optimize=True)
    # glyphs: same white legend ink as the meter window, rendered per cell
    f = ImageFont.truetype(FONT_BOLD, 1000)
    cap_ratio = -f.getbbox("H", anchor="ls")[1] / 1000.0
    k = 4
    font = ImageFont.truetype(FONT_BOLD, int(round(P(GLYPH_CAP) * k / cap_ratio)))
    strip = Image.new("RGBA", (GLYPH_CELL * 2 * len(GLYPHS), GLYPH_H))
    advances = []
    for i, ch in enumerate(GLYPHS):
        cw = GLYPH_CELL * 2
        m = Image.new("L", (cw * k, GLYPH_H * k), 0)
        d = ImageDraw.Draw(m)
        adv = font.getlength(ch)
        d.text(((cw * k - adv) / 2, (GLYPH_H * k + P(GLYPH_CAP) * k) / 2), ch, font=font, fill=255, anchor="ls")
        mm = np.asarray(m.resize((cw, GLYPH_H), Image.LANCZOS)).astype(np.float32) / 255.0
        mm = ndimage.gaussian_filter(mm, 0.45)
        col = np.dstack([np.full_like(mm, 232), np.full_like(mm, 232), np.full_like(mm, 224)])
        strip.paste(to_img(col, mm), (i * cw, 0))
        advances.append(adv / k / SCALE)
    strip.save(os.path.join(OUT, "popup_glyphs.png"), optimize=True)
    return advances


# --------------------------------------------------------------------------
# LIT LEDs
# --------------------------------------------------------------------------
SEG_PAD = 14   # photo px padding around a lit segment for its glow


def lit_segment(color_core, color_glow, name):
    w = int((SEG_W + 2 * SEG_PAD) * SCALE)
    h = int((SEG_Y1 - SEG_Y0 + 2 * SEG_PAD) * SCALE)
    x0, y0 = SEG_PAD * SCALE, SEG_PAD * SCALE
    x1, y1 = x0 + SEG_W * SCALE, y0 + (SEG_Y1 - SEG_Y0) * SCALE
    cov = rounded_rect_mask(w, h, x0, y0, x1, y1, 0.8 * SCALE)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    # hot spot in the middle of the diffuser
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    d = np.sqrt(((xx - cx) / ((x1 - x0) * 0.6)) ** 2 + ((yy - cy) / ((y1 - y0) * 0.75)) ** 2)
    hot = np.exp(-d ** 2)
    core = np.asarray(color_core, np.float32)
    col = core[None, None, :] * (0.85 + 0.15 * hot[..., None]) + 255 * 0.45 * hot[..., None] ** 2
    glow = ndimage.gaussian_filter(cov, 4.5 * SCALE)
    glow = glow / glow.max()
    gcol = np.asarray(color_glow, np.float32)
    a = np.clip(cov + 0.55 * glow * (1 - cov), 0, 1)
    out = (col * cov[..., None] + gcol * (0.55 * glow * (1 - cov))[..., None]) / np.maximum(a, 1e-4)[..., None]
    to_img(out, a).save(os.path.join(OUT, name), optimize=True)


def lit_round_led(name, r_photo=2.4, pad=10):
    s = int((2 * (r_photo + pad)) * SCALE)
    c = (s - 1) / 2
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32)
    rr = np.sqrt((xx - c) ** 2 + (yy - c) ** 2) / SCALE
    core = 1 - smoothstep(r_photo - 0.5, r_photo + 0.3, rr)
    glow = np.exp(-(rr / (r_photo * 2.6)) ** 2)
    col = np.dstack([np.full_like(rr, 255), 60 + 150 * np.exp(-(rr / (r_photo * 0.6)) ** 2),
                     50 + 140 * np.exp(-(rr / (r_photo * 0.6)) ** 2)])
    a = np.clip(core + 0.75 * glow * (1 - core), 0, 1)
    to_img(col, a).save(os.path.join(OUT, name), optimize=True)


# --------------------------------------------------------------------------
# Layout.h (shared with C++)
# --------------------------------------------------------------------------
def write_layout_header():
    lines = [
        "// AUTO-GENERATED by tools/render_assets.py - do not edit by hand.",
        "// All coordinates are in reference-photo units (panel = 2000 x 196).",
        "#pragma once",
        "",
        "namespace layout",
        "{",
        f"    constexpr float panelW = {PW}.0f, panelH = {PH}.0f;",
        f"    constexpr float assetScale = {SCALE}.0f;",
        f"    constexpr float knobRadius = {KNOB_R}f;",
        f"    constexpr int   knobFrame = {KNOB_FRAME}, knobFrames = {KNOB_FRAMES};",
        f"    constexpr float knobSweepDeg = {SWEEP}f;",
        f"    constexpr float toggleRadius = {TOGGLE_R}f;",
        f"    constexpr int   toggleFrame = {TOGGLE_FRAME};",
        f"    constexpr int   lampFrame = {LAMP_FRAME};",
        f"    constexpr float lampX = {LAMP[0]}f, lampY = {LAMP[1]}f, lampR = {LAMP[2]}f;",
        f"    constexpr float grY0 = {GR_Y0}f, grY1 = {GR_Y1}f;",
        f"    constexpr float grWinY1 = {float(GR_WIN[3])}f;",
        f"    constexpr int   numGrSegments = {NUM_GR};",
        f"    constexpr float popupW = {POPUP_W}f, popupH = {POPUP_H}f;",
        f"    constexpr int   glyphCell = {GLYPH_CELL * 2}, glyphH = {GLYPH_H};",
        '    inline const char* const popupGlyphs = "' + GLYPHS + '";',
        "    constexpr float glyphAdvance[] = { " + ", ".join(f"{a:.3f}f" for a in glyph_advances()) + " };   // photo px",
        f"    constexpr float mixX = {MIX[0]}f, mixY = {MIX[1]}f, mixR = {MIX[2]}f;",
        f"    constexpr float hpfX = {HPF[0]}f, hpfY = {HPF[1]}f;",
        f"    constexpr float windowToggleX = {WINDOW_TOGGLE[0]}f, windowToggleY = {WINDOW_TOGGLE[1]}f, windowToggleScale = {WINDOW_TOGGLE[2]}f;",
        f"    constexpr float winH = {float(WIN_H)}f, smallKnobR = {SMALL_R}f;",
        f"    constexpr float winSwitchX = {WIN_SWITCH[0]}f, winSwitchY = {WIN_SWITCH[1]}f;",
        f"    constexpr float dispX0 = {WIN_DISPLAY[0]}f, dispY0 = {WIN_DISPLAY[1]}f, dispX1 = {WIN_DISPLAY[2]}f, dispY1 = {WIN_DISPLAY[3]}f;",
        f"    constexpr float specX0 = {SPEC[0]}f, specY0 = {SPEC[1]}f, specX1 = {SPEC[2]}f, specY1 = {SPEC[3]}f, specDb = {SPEC_DB}f;",
        f"    constexpr float grhX0 = {GRH[0]}f, grhY0 = {GRH[1]}f, grhX1 = {GRH[2]}f, grhY1 = {GRH[3]}f, grhDb = {GRH_DB}f;",
        *[f"    constexpr float {k}X = {x}f, {k}Y = {y}f;" for k, (x, y) in WIN_KNOBS.items()],
        f"    constexpr int   mixFrame = {SCREW_FRAME}, mixFrames = {SCREW_FRAMES};",
        f"    constexpr float charDotR = {CHAR_R_DOT}f;",
        f"    constexpr float segX0 = {SEG_X0}f, segPitch = {SEG_PITCH}f, segW = {SEG_W}f;",
        f"    constexpr float segY0 = {SEG_Y0}f, segY1 = {SEG_Y1}f, clipX0 = {CLIP_X0}f;",
        f"    constexpr float segPad = {float(SEG_PAD)}f;",
        "    constexpr int   numSegments = 22;",
        "    constexpr float roundLedR = 2.4f, roundLedPad = 10.0f;",
        "",
    ]
    for k, (x, y) in KNOBS.items():
        lines.append(f"    constexpr float {k}X = {x}f, {k}Y = {y}f;")
    for k, (x, y) in TOGGLES.items():
        lines.append(f"    constexpr float {k}ToggleX = {x}f, {k}ToggleY = {y}f;")
    lines.append("")
    lines.append("    inline const char* const characterNames[16] = {")
    lines.append("        " + ", ".join(f'"{n}"' for n in CHAR_NAMES))
    lines.append("    };")
    lines.append("    inline const char* const segmentLabels[22] = {")
    lines.append("        " + ", ".join(f'"{n}"' for n in SEG_LABELS))
    lines.append("    };")
    lines.append("}")
    with open(os.path.join(ROOT, "Source", "Layout.h"), "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    import sys
    what = sys.argv[1:] or ["all"]
    if "all" in what or "panel" in what:
        render_panel(); print("panel")
    if "all" in what or "knob" in what:
        render_knob_strip(); print("knob")
    if "all" in what or "small" in what:
        render_toggle_strip(); render_lamp_strip(); render_screw_strip()
        lit_segment((40, 255, 110), (40, 255, 110), "seg_green.png")
        lit_segment((255, 52, 40), (255, 40, 30), "seg_red.png")
        lit_round_led("led_red.png")
        lit_segment((255, 176, 40), (255, 150, 30), "seg_amber.png")
        render_popup()
        print("small")
    write_layout_header()
