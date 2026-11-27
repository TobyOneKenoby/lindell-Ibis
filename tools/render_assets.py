#!/usr/bin/env python3
"""
Photo-real GUI renderer for Lindell - Ibis.

It uses the Lindell Tracker 2 renderer (tools/lindell_render.py, vendored
unchanged) for everything that the two plug-ins share: the brushed-aluminium
faceplate and silk-screen ink, the studio lighting and camera finish, the teal
knobs, bat toggles, power lamp, hole plugs and logos. This file only adds the
Ibis layout, its printed scales, and the push buttons.

Coordinates are panel units of a 2U front panel (2000 x 370, the same 19-inch 2U
proportions as Lindell STC-8; Tracker 2 is the 1U, 2000 x 196). Assets are
rendered at SCALE x that, so the background is 3832 x 740 px (sharp on retina
screens at the default 1440 px editor width).

Outputs (in ../assets) and ../Source/Layout.h:
  panel.png                         faceplate: legends, scales, plugs, logos, shadows
  knob_large_strip.png              gain / low-cut knob, 180 frames over 360 deg
  knob_small_strip.png              freq / bandwidth / colour knob, 180 frames
  button_{red,black,white}_strip.png  push buttons, 2 frames (out, pushed in)
  toggle_strip.png                  bat toggle, 2 frames (lever left, right)
  lamp_strip.png                    power lamp, 2 frames (off, on)

    python3 tools/render_assets.py              # everything (a few minutes)
    python3 tools/render_assets.py panel        # faceplate only
"""
import math
import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lindell_render as T  # noqa: E402

OUT = os.path.join(ROOT, "assets")
os.makedirs(OUT, exist_ok=True)

# --------------------------------------------------------------------------
# Branding (edit freely)
# --------------------------------------------------------------------------
LOGO_IMAGE = "ibis_logo.png"          # the IBIS logo, printed in green ink
LOGO_BOX = (15.0 + 42.0, 100.0, 129.0 + 42.0, 190.0)   # (+ OFF: the wider rack ear)  # x0, y0, x1, y1 panel units
LOGO_INK = (22, 72, 58)
SUBTITLE = ["DISCRETE", "CLASS A", "EQUALIZER"]
BRAND = "LINDELL PLUGINS"

# --------------------------------------------------------------------------
# Panel geometry (panel units)
# --------------------------------------------------------------------------
SCALE = 3                       # 3x the panel units: sharp on retina, also when enlarged
PW, PH = 2000, 370
# The panel was first drawn 1916 wide. For a true 19-inch 2U face (as STC-8) the rack
# ears grew: everything on the face moved OFF to the right, the right ear 2 * OFF.
OFF = 42.0
W, H = PW * SCALE, PH * SCALE
PANEL = (2, 2, 1998, 368)

LARGE_R = 37.0                  # gain / low cut
SMALL_R = 23.5                  # freq / bandwidth / colour source / colour
BUTTON_R = 12.0                 # push buttons (and the former toggles), each with a green LED ring
TOGGLE_R = 18.5
LAMP = (948.0 + OFF, 245.0, 27.0)     # cx, cy, bezel radius

CH_X = {0: dict(lc=212.0 + OFF, bands=[358.0 + OFF, 506.0 + OFF, 652.0 + OFF, 800.0 + OFF]),
        1: dict(lc=1083.0 + OFF, bands=[1229.0 + OFF, 1376.0 + OFF, 1523.0 + OFF, 1671.0 + OFF])}
Y_GAIN, Y_FREQ, Y_BW = 80.0, 186.0, 313.0
Y_LC, Y_COLSRC, Y_COLOUR = 80.0, 228.0, 313.0
Y_STEP, Y_SHELF, Y_STEEP = 254.0, 147.0, 160.0
# the switches are round black push buttons with the green LED ring (lit = the left legend)
TOGGLES = {"ch1": (948.0 + OFF, 48.0), "ch2": (948.0 + OFF, 112.0), "link": (948.0 + OFF, 176.0),
           "power": (948.0 + OFF, 320.0), "display": (1791.0 + 2 * OFF, 262.0)}   # DISPLAY on the line of the screw under it (as on STC-8)
# printed legends of each toggle: title, left position, right position
TOGGLE_LEGENDS = {"ch1": ("CH1", "IN", "OUT"), "ch2": ("CH2", "IN", "OUT"), "link": ("STEREO LINK", "ON", "OFF"),
                  "power": ("POWER", "ON", "OFF"), "display": ("DISPLAY", "ON", "OFF")}
SCREWS = [(125.0, 47.0), (125.0, 323.0), (1791.0 + 2 * OFF, 47.0), (1791.0 + 2 * OFF, 323.0)]
SLOTS = [(10, 16, 59, 38), (10, 332, 59, 354), (1941, 16, 1990, 38), (1941, 332, 1990, 354)]
DIVIDERS = [x + OFF for x in (287.0, 432.0, 579.0, 726.0, 875.0, 1157.0, 1302.0, 1449.0, 1597.0, 1745.0)]
LOGOS = [("logo_lsr.png", 1782.0 + OFF, 185.0, 22.0), ("logo_l.png", 1833.0 + OFF, 185.5, None)]

# Analyser display under the faceplate (its own coordinates, same width)
WIN_H = 190
WIN_DISPLAY = (30.0, 10.0, 1970.0, 180.0)       # glass window (full width, as on STC-8)
SPEC = (84.0, 24.0, 1916.0, 150.0)              # plot area inside the glass
SPEC_DB = 72.0                                  # spectrum: 0 .. -72 dBFS (left scale)
EQ_DB = 18.0                                    # EQ curve: +18 .. -18 dB (right scale), same grid lines

# Knob sweeps (degrees, 0 = 12 o'clock, clockwise); the editor reads these from Layout.h
SWEEP = dict(gain=(-148.0, 148.0), freq=(-165.0, 165.0), lowcut=(-165.0, 165.0),
             bw=(-150.0, 150.0), colour=(-150.0, 150.0), colsrc=(-60.0, 40.0))

GAIN_LABELS = ["12", "10", "8", "6", "4", "3", "2", "1", "0", "1", "2", "3", "4", "6", "8", "10", "12"]
LC_LABELS = ["OUT", "20", "30", "40", "50", "60", "70", "80", "90", "100", "120", "150"]
BW_LABELS = [".1", ".25", "1", "1.5", "2", "", "3", "", "3.5", "", "4"]
FREQ_LABELS = [
    ["C 33", "E", "G# 52", "C", "E", "G# 104", "C", "E", "G# 208", "C 262", "E", "G# 415"],
    ["C# 139", "F", "A 220", "C#", "F", "A440", "C#", "F 700", "A 880", "C#", "F", "A 1.76K"],
    ["A# 466", "D", "F#", "A# 932", "D", "F# 1.48K", "A#", "D 2.35K", "F#", "A# 3.73K", "D", "F# 5.9K"],
    ["G 1.57K", "B", "D#", "G", "B", "D# 4.98K", "G", "B", "D# 9.9K", "G", "B", "D# 19.9K"],
]
COLSRC_LABELS = [["OFF", "FREQ 1", "FREQ 2", "FREQ 3", "FREQ 4", "FULL"],
                 ["OFF", "BAND 1", "BAND 2", "BAND 3", "BAND 4", "FULL"]]

# --------------------------------------------------------------------------
# point the shared renderer at this panel
# --------------------------------------------------------------------------
T.ROOT, T.OUT = ROOT, OUT
T.SCALE, T.PW, T.PH, T.W, T.H = SCALE, PW, PH, W, H
T.LOGOS = LOGOS
T.LAMP = LAMP
T.TOGGLE_R = TOGGLE_R
T.TOGGLE_FRAME = 144            # Tracker toggle / lamp frames at 3x
T.LAMP_FRAME = 300


def clean_finish(img, blur=0.55, grain=1.6, seed=0, sharpen=0.45):
    """A clean product-render finish (replaces the Tracker's camera look):
    no sensor grain or chroma noise, only a trace of lens softness, a light
    sharpen without halos, and the same filmic highlight roll-off."""
    rgba = img.mode == "RGBA"
    a = np.asarray(img).astype(np.float32)
    rgb = a[..., :3]
    b = 0.30 * blur
    if b > 0:
        rgb = ndimage.gaussian_filter(rgb, (b, b, 0))
    rgb = rgb + 0.20 * sharpen * (rgb - ndimage.gaussian_filter(rgb, (1.5, 1.5, 0)))
    x = np.clip(rgb / 255.0, 0, 3.0)
    x = np.where(x > 0.80, 0.80 + (1 - np.exp(-(x - 0.80) / 0.20)) * 0.20, x)
    rgb = x * 255
    if rgba:
        return Image.fromarray(np.dstack([np.clip(rgb, 0, 255), a[..., 3:]]).astype(np.uint8), "RGBA")
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")


T.photo_finish = clean_finish
P = T.P
polar = T.polar
smoothstep = T.smoothstep


def knob_list():
    """(channel, kind, band, x, y, large)"""
    out = []
    for c in (0, 1):
        x = CH_X[c]
        out.append((c, "lowcut", 0, x["lc"], Y_LC, True))
        out.append((c, "colsrc", 0, x["lc"], Y_COLSRC, False))
        out.append((c, "colour", 0, x["lc"], Y_COLOUR, False))
        for b, bx in enumerate(x["bands"]):
            out.append((c, "gain", b + 1, bx, Y_GAIN, True))
            out.append((c, "freq", b + 1, bx, Y_FREQ, False))
            out.append((c, "bw", b + 1, bx, Y_BW, False))
    return out


def button_list():
    """(name, colour, x, y)"""
    out = []
    for c in (0, 1):
        x = CH_X[c]
        out.append((f"c{c+1}_steep", "white", x["lc"] + 56.0, Y_STEEP))
        out.append((f"c{c+1}_shelf1", "black", x["bands"][0] + 57.0, Y_SHELF))
        out.append((f"c{c+1}_shelf4", "black", x["bands"][3] + 57.0, Y_SHELF))
        for b, bx in enumerate(x["bands"]):
            out.append((f"c{c+1}_step{b+1}", "red", bx + 18.0, Y_STEP))
    return out


def angles(kind, n):
    a0, a1 = SWEEP[kind]
    return [a0 + (a1 - a0) * i / (n - 1) for i in range(n)]


# --------------------------------------------------------------------------
# printed legends
# --------------------------------------------------------------------------
NUM = 8.4        # scale numbers (cap height)
SMALL = 6.9      # note names, small words
TITLE = 9.0      # control names


def side_text(L, s, x, y, ang, cap, gap=0.0):
    """place a label at a scale position: centred at the top/bottom, flush toward the knob at the sides"""
    sx = math.sin(math.radians(ang))
    if sx < -0.05:                       # left half: text runs away from the knob, to the left
        L.text(s, x + gap, y, cap, align="r")
    elif sx > 0.05:
        L.text(s, x - gap, y, cap, align="l")
    else:
        L.text(s, x, y, cap)


def scale_gain(L, cx, cy):
    for s, a in zip(GAIN_LABELS, angles("gain", 17)):
        L.dot(*polar(cx, cy, a, LARGE_R + 5.5), 1.35)
        x, y = polar(cx, cy, a, LARGE_R + 14.5)
        L.text(s, x, y, NUM)
    L.text("CUT", cx - 34.0, cy - 64.0, TITLE * 0.92)
    L.text("BOOST", cx + 36.0, cy - 64.0, TITLE * 0.92)


def scale_lowcut(L, cx, cy):
    for i, (s, a) in enumerate(zip(LC_LABELS, angles("lowcut", 12))):
        L.dot(*polar(cx, cy, a, LARGE_R + 5.5), 1.35)
        if i in (0, 11):      # OUT and 150 sit under the knob, like the hardware
            x, y = polar(cx, cy, a, LARGE_R + 12.0)
            L.text(s, x + (-12.0 if i == 0 else 12.0), y + 9.0, NUM)
        else:
            x, y = polar(cx, cy, a, LARGE_R + 14.5)
            side_text(L, s, x, y, a, NUM, gap=4.0)
    L.text("LOW", cx + 58.0, cy - 66.0, TITLE * 0.92)
    L.text("CUT", cx + 58.0, cy - 55.0, TITLE * 0.92)
    L.text("IN = STEEP", cx - 14.0, Y_STEEP, TITLE * 0.85)


def scale_small(L, cx, cy, labels, kind, cap=SMALL, r_text=None):
    n = len(labels)
    for s, a in zip(labels, angles(kind, n)):
        L.dot(*polar(cx, cy, a, SMALL_R + 4.5), 1.15)
        if s:
            x, y = polar(cx, cy, a, r_text or SMALL_R + 10.5)
            side_text(L, s, x, y, a, cap)


def misc_legends(L):
    # logo + subtitle on the left
    for i, s in enumerate(SUBTITLE):
        L.text(s, 20.0 + OFF, 212.0 + i * 24.0, 13.0, align="l", weight=0.10)
    # brand on the right
    L.text("LINDELL", 1806.0 + OFF, 142.0, 12.5, track=0.8, weight=0.12)
    L.text("PLUGINS", 1806.0 + OFF, 160.0, 12.5, track=0.8, weight=0.12)
    # centre section
    for name, (x, y) in TOGGLES.items():
        title, left, right = TOGGLE_LEGENDS[name]
        L.text(title, x, y - (30.0 if name == "power" else 25.0), TITLE * (0.9 if len(title) > 8 else 1.0))
        # side legends clear the button's LED ring (they scale with the word length)
        L.text(left, x - 33.0 - 3.2 * (len(left) - 2), y, TITLE * 0.92)
        L.text(right, x + 35.0 + 3.2 * (len(right) - 3), y, TITLE * 0.92)
    # dividers between the sections
    for x in DIVIDERS:
        L.line(x, 14.0, x, 356.0, 1.3)


def channel_legends(L, c):
    x = CH_X[c]
    lc = x["lc"]
    scale_lowcut(L, lc, Y_LC)
    for a in angles("colsrc", 6):
        L.dot(*polar(lc, Y_COLSRC, a, SMALL_R + 4.5), 1.15)
    # (text, dx, dy, align) relative to the knob centre, as laid out on the hardware
    for s_, (dx, dy, al) in zip(COLSRC_LABELS[c], [(-30.0, -5.0, "r"), (-30.0, -18.0, "r"), (-25.0, -31.0, "r"),
                                                    (-3.0, -45.0, "r"), (6.0, -45.0, "l"), (27.0, -31.0, "l")]):
        L.text(s_, lc + dx, Y_COLSRC + dy, SMALL * 1.05, align=al)
    L.text("COLOR SOURCE", lc, Y_COLSRC + 36.0, TITLE * 0.9)
    scale_small(L, lc, Y_COLOUR, [str(i) for i in range(11)], "colour", cap=NUM * 0.92)
    L.text("COLOR", lc, Y_COLOUR + 44.0, TITLE)
    for b, bx in enumerate(x["bands"]):
        scale_gain(L, bx, Y_GAIN)
        scale_small(L, bx, Y_FREQ, FREQ_LABELS[b], "freq")
        scale_small(L, bx, Y_BW, BW_LABELS, "bw", cap=NUM * 0.92)
        L.text("BANDWIDTH", bx, Y_BW + 44.0, TITLE)
        # +1 STEP legend to the left of the red button
        sx = bx + 18.0
        L.text("IN", sx - 36.0, Y_STEP - 10.0, SMALL * 1.1)
        L.text("+1", sx - 36.0, Y_STEP, SMALL * 1.1)
        L.text("STEP", sx - 36.0, Y_STEP + 10.0, SMALL * 1.1)
        if b in (0, 3):     # shelf buttons: "IN" over a little shelf curve
            hx = bx + 57.0
            L.text("IN", hx, Y_SHELF - 32.0, SMALL * 1.1)
            L.polyline([(hx - 9.0, Y_SHELF - 20.0), (hx - 3.0, Y_SHELF - 20.0),
                        (hx + 3.0, Y_SHELF - 25.0), (hx + 9.0, Y_SHELF - 25.0)], 1.1)


def logo_mask():
    """coverage of the IBIS logo on the full panel raster"""
    m = Image.open(os.path.join(ROOT, "third_party", "logos", LOGO_IMAGE)).convert("L")
    x0, y0, x1, y1 = LOGO_BOX
    w, h = int(round(P(x1 - x0))), int(round(P(y1 - y0)))
    m = np.asarray(m.resize((w, h), Image.LANCZOS)).astype(np.float32) / 255.0
    out = np.zeros((H, W), np.float32)
    ox, oy = int(round(P(x0))), int(round(P(y0)))
    out[oy:oy + h, ox:ox + w] = m
    return out


def apply_ink(rgb, ink, colour, brushed, sheen, rng):
    """silk-screen ink exactly as on the Tracker faceplate, in any colour"""
    # crisp print: the anti-aliased type edge, barely softened, no spread or bleed
    ink = smoothstep(0.08, 0.92, ndimage.gaussian_filter(ink, 0.45))
    mottle = ndimage.gaussian_filter(rng.normal(size=ink.shape).astype(np.float32), 3.0)
    ink = np.clip(ink * (0.985 + 0.02 * mottle / (mottle.std() + 1e-6)), 0, 1)
    ink_col = np.array(colour, np.float32)[None, None, :] * (1.0 + 0.012 * brushed[..., None])
    ink_col = ink_col + (8.0 * sheen)[..., None]
    under = ndimage.shift(ndimage.gaussian_filter(ink, 1.6), (2.2, 0.4), order=1)
    rgb *= (1 - 0.20 * under * (1 - ink))[..., None]
    rgb[...] = rgb * (1 - 0.94 * ink[..., None]) + ink_col * (0.94 * ink[..., None])
    gy = np.gradient(ndimage.gaussian_filter(ink, 0.8), axis=0)
    rgb += (55.0 * np.clip(gy, 0, None) * ink)[..., None]



# --------------------------------------------------------------------------
# PLATE MATERIAL - one brushed aluminium for the faceplate and the display
# plate (the display plate's look), lit as ONE surface: 'vg' runs 0..1 over
# faceplate + display, split by their heights, so the light carries straight
# across the seam instead of each plate starting bright at its own top.
# --------------------------------------------------------------------------
def plate_v(v_local, lower):
    total = PH + WIN_H
    return (WIN_H * v_local + PH) / total if lower else (PH * v_local) / total


def plate_material(u, vg, rng):
    hh, ww = u.shape

    def streaks(length, amp):
        n = T.box_blur_x(rng.normal(size=(hh, ww)).astype(np.float32), length)
        return amp * n / n.std()
    brushed = streaks(240, 2.0) + streaks(60, 1.4) + streaks(14, 0.7) + streaks(1350, 1.4)
    sheen = (0.95 * np.exp(-((u - 0.22) / 0.075) ** 2) + 0.70 * np.exp(-((u - 0.47) / 0.11) ** 2)
             + 1.00 * np.exp(-((u - 0.76) / 0.06) ** 2) + 0.45 * np.exp(-((u - 0.93) / 0.05) ** 2))
    sheen *= 0.55 + 0.45 * (1 - vg)
    return brushed, sheen


def plate_rgb(brushed, sheen):
    metal = 182.0 + 44.0 * sheen + brushed * (0.75 + 1.3 * sheen)
    return np.dstack([metal * 1.000, metal * 0.990, metal * 0.968]).astype(np.float32)


def plate_light(u, vg):
    light = (1.14 - 0.24 * vg ** 1.1) * (1 - 0.16 * np.abs(2 * u - 1) ** 2.4)
    return light * (1 - 0.15 * np.exp(-((u - 0.5) / 0.24) ** 2))


# --------------------------------------------------------------------------
# PANEL - same material, ink and lighting pipeline as the Tracker faceplate
# --------------------------------------------------------------------------
def render_panel():
    rng = T.rng
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    u = xx / W
    v = yy / H

    def streaks(length, amp):
        n = T.box_blur_x(rng.normal(size=(H, W)).astype(np.float32), length)
        return amp * n / n.std()
    brushed, sheen = plate_material(u, plate_v(v, False), rng)
    rgb = plate_rgb(brushed, sheen)

    # ---- silk-screen ink
    L = T.Legend()
    misc_legends(L)
    channel_legends(L, 0)
    channel_legends(L, 1)
    ink = L.mask()
    apply_ink(rgb, ink, T.INK, brushed, sheen, rng)
    apply_ink(rgb, logo_mask(), LOGO_INK, brushed, sheen, rng)

    for (cx, cy) in SCREWS:
        T.screw(rgb, cx, cy)

    # ---- shadows of everything standing proud of the panel
    shadow = np.zeros((H, W), np.float32)

    def disc_shadow(items, r, off_y, blur, strength):
        img = Image.new("L", (W, H), 0)
        d = ImageDraw.Draw(img)
        for (cx, cy) in items:
            X, Y, RR = P(cx), P(cy + off_y), P(r)
            d.ellipse([X - RR, Y - RR, X + RR, Y + RR], fill=255)
        return strength * np.asarray(img.filter(ImageFilter.GaussianBlur(P(blur)))).astype(np.float32) / 255.0

    large = [(k[3], k[4]) for k in knob_list() if k[5]]
    small = [(k[3], k[4]) for k in knob_list() if not k[5]]
    buttons = [(b[2], b[3]) for b in button_list()] + list(TOGGLES.values())   # all push buttons
    for items, R_ in ((large, LARGE_R), (small, SMALL_R)):
        k = R_ / T.KNOB_R
        shadow += disc_shadow(items, R_ * 1.10, 10.0 * k, 9.0 * k, 0.16)   # short, soft pool
        shadow += disc_shadow(items, R_ * 0.98, 6.0 * k, 5.0 * k, 0.30)
        shadow += disc_shadow(items, R_ * 1.0, 3.0 * k, 3.0 * k, 0.40)
        shadow += disc_shadow(items, R_ * 1.03, 0.6, 1.2, 0.55)
    shadow += disc_shadow(buttons, BUTTON_R * 1.05, 4.0, 4.0, 0.40)
    shadow += disc_shadow(buttons, BUTTON_R * 1.02, 0.8, 1.2, 0.45)
    shadow += disc_shadow([(LAMP[0], LAMP[1])], LAMP[2] * 0.97, 3.0, 3.0, 0.40)
    logos = list(T.place_logos(rgb, shadow))
    rgb *= (1 - 0.66 * np.clip(shadow, 0, 1))[..., None]
    for (ox, oy, a) in logos:
        al = a[..., 3:4] / 255.0
        reg = rgb[oy:oy + a.shape[0], ox:ox + a.shape[1]]
        reg[...] = reg * (1 - al) + a[..., :3] * al

    # ---- lighting: one light over faceplate + display
    rgb *= plate_light(u, plate_v(v, False))[..., None]

    # ---- outline, ear slots, rack behind
    x0, y0, x1, y1 = PANEL
    panel_cov = T.rounded_rect_mask(W, H, P(x0), P(y0), P(x1), P(y1), P(7))
    for (a_, b_, c_, d_) in SLOTS:
        panel_cov *= 1 - T.rounded_rect_mask(W, H, P(a_), P(b_), P(c_), P(d_), P((d_ - b_) / 2))
    dist = ndimage.distance_transform_edt(panel_cov > 0.5).astype(np.float32)
    edge = np.exp(-dist / P(1.4))
    rgb *= (1 - 0.14 * edge)[..., None]                       # same edge as the display plate
    back = np.zeros_like(rgb)
    rail = 22 + 10 * (1 - v) + 1.2 * streaks(9, 1.0)
    back[...] = np.dstack([rail, rail, rail * 1.04])
    back *= (1 - 0.80 * ndimage.gaussian_filter(panel_cov, P(2.2)))[..., None]

    out = back * (1 - panel_cov[..., None]) + rgb * panel_cov[..., None]
    out = T.photo_grade(out, contrast=1.14, bloom=0.45)
    img = T.photo_finish(T.to_img(out), blur=0.85, grain=2.4, seed=3, sharpen=0.55)
    img.save(os.path.join(OUT, "panel.png"), optimize=True)
    return img


# --------------------------------------------------------------------------
# KNOBS - the Tracker knob, at two sizes
# --------------------------------------------------------------------------
LARGE_FRAME = 264          # the Tracker knob frame at 3x (radius 111 px)
SMALL_FRAME = 168          # radius 70 px
KNOB_FRAMES = 180


def render_knob_strip(radius, frame, name):
    T.KNOB_R, T.KNOB_FRAME = radius, frame
    strip = Image.new("RGBA", (frame, frame * KNOB_FRAMES))
    for i in range(KNOB_FRAMES):
        strip.paste(T.knob_frame(i * 360.0 / KNOB_FRAMES), (0, i * frame))
    strip.save(os.path.join(OUT, name), optimize=True)


# --------------------------------------------------------------------------
# SMALL KNOB - the fluted IBIS knob: a teal cap with a scalloped edge and an
# engraved white pointer line, seated on a polished hex nut. Same teal,
# studio lighting and camera finish as the Tracker knob.
# --------------------------------------------------------------------------
SMALL_LOBES = 12


def small_knob_frame(angle_deg, ss=3):
    N = SMALL_FRAME * ss
    R = SMALL_R * SCALE * ss
    c = (N - 1) / 2.0
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    u, v = (xx - c) / R, (yy - c) / R
    r = np.sqrt(u * u + v * v)
    th = np.arctan2(u, -v)                              # 0 = 12 o'clock, clockwise
    a = math.radians(angle_deg)
    KL = np.array([-0.56, -0.62, 0.55]); KL /= np.linalg.norm(KL)
    aa = 1.2 / R * ss

    # -- hex nut behind the cap (polished, dark chrome), flats top and bottom
    hexd = np.maximum(np.abs(u) * 0.8660 + np.abs(v) * 0.5, np.abs(v))
    nut_d = hexd - 0.90
    h_nut = 0.045 * smoothstep(0.0, 0.14, -nut_d)
    n_nut = T.normals_from_height(ndimage.gaussian_filter(h_nut, R * 0.012), 1.0 / R)
    nut = T.shade_metal(n_nut, (215, 214, 212), rough=0.06, front=1.3)
    nut_a = 1 - smoothstep(-aa, aa, nut_d)

    # -- scalloped cap outline: rounded lobes, turning with the knob
    lobe = 0.5 + 0.5 * np.cos(SMALL_LOBES * (th - a))
    edge = 0.845 + 0.05 * lobe ** 0.8
    d_cap = r - edge                                     # signed distance (approx.)
    side = np.clip(-d_cap / 0.22, 0, 1)                  # 0 at the edge -> 1 inside
    h_cap = 0.30 + 0.17 * np.sqrt(1 - (1 - side) ** 2)   # rounded shoulder
    h_cap += 0.035 * (1 - np.clip(r / 0.75, 0, 1) ** 2)  # gentle dome
    # flutes: shallow grooves running in from each valley
    h_cap -= 0.02 * (1 - lobe) ** 2 * smoothstep(0.55, 0.84, r)

    # -- engraved pointer line from the centre to the edge
    along = u * math.sin(a) - v * math.cos(a)
    across = u * math.cos(a) + v * math.sin(a)
    line = (1 - smoothstep(0.022, 0.036, np.abs(across))) * smoothstep(0.04, 0.08, along) \
        * (1 - smoothstep(0.76, 0.80, along))
    h_cap -= 0.012 * line

    capm = 1 - smoothstep(-aa, aa, d_cap)
    h = np.where(capm > 0.5, h_cap, h_nut)
    hs = ndimage.gaussian_filter(h, R * 0.018)
    n = T.normals_from_height(hs, 1.0 / R)
    sh = 0.25 + 0.75 * T.cast_shadow(hs, R, yy, xx, slope=0.50, reach=0.6, light=KL)
    hb = ndimage.gaussian_filter(hs, R * 0.12)
    ao = np.clip(1 - 2.4 * np.clip(hb - hs, 0, None), 0.45, 1)
    curv = ndimage.laplace(ndimage.gaussian_filter(hs, R * 0.04)) * (R * R * 0.02)
    gmap = np.clip(0.45 - 2.2 * curv, 0.18, 1.0)
    cap = T.shade_photo(n, T.TEAL, f0=0.045, gloss=1.05, ao=ao, shadow=sh, soft=0.03,
                        light=KL, strip_gain=0.25, key_size=1.25, gloss_map=gmap)
    paint = T.shade_photo(n, np.array([222, 228, 226], np.float32), f0=0.05, gloss=0.4, ao=ao,
                          shadow=sh, light=KL, strip_gain=0.25)
    linem = ndimage.gaussian_filter(line, R * 0.006)
    cap = cap * (1 - linem[..., None]) + paint * linem[..., None]
    hv = KL + np.array([0.0, 0.0, 1.0]); hv /= np.linalg.norm(hv)
    cap += (255.0 * 0.6 * np.clip((n * hv).sum(2), 0, 1) ** 160 * sh)[..., None]
    # the cap's own soft shadow on the nut
    cap_sh = ndimage.shift(ndimage.gaussian_filter(capm, R * 0.06), (R * 0.06, R * 0.02), order=1)
    nut = nut * (1 - 0.6 * cap_sh)[..., None]

    col = cap * capm[..., None] + nut * (1 - capm)[..., None]
    alpha = np.maximum(capm, nut_a)
    img = T.to_img(col, alpha).resize((SMALL_FRAME, SMALL_FRAME), Image.LANCZOS)
    return T.photo_finish(img, blur=0.45, grain=1.5, seed=int(abs(angle_deg) * 10) + 5)


def render_small_knob_strip():
    strip = Image.new("RGBA", (SMALL_FRAME, SMALL_FRAME * KNOB_FRAMES))
    for i in range(KNOB_FRAMES):
        strip.paste(small_knob_frame(i * 360.0 / KNOB_FRAMES), (0, i * SMALL_FRAME))
    strip.save(os.path.join(OUT, "knob_small_strip.png"), optimize=True)


# --------------------------------------------------------------------------
# PUSH BUTTONS - glossy moulded caps in a hole in the panel, 2 frames:
# 0 = out, 1 = pushed in (sits deeper: the hole's edge shades its top)
# --------------------------------------------------------------------------
BUTTON_FRAME = int(round((BUTTON_R + 6.0) * SCALE)) * 2
BUTTON_COLOURS = {"red": (196, 26, 20), "black": (24, 23, 23), "white": (226, 226, 220)}


def button_frame(colour, pushed, ss=4):
    N = BUTTON_FRAME * ss
    R = BUTTON_R * SCALE * ss
    c = (N - 1) / 2.0
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    u, v = (xx - c) / R, (yy - c) / R
    r = np.sqrt(u * u + v * v)
    CAP = 0.86
    # cap: flat top with a generous rounded edge and a slight dome
    t = np.clip((r - 0.55) / (CAP - 0.55), 0, 1)
    top = (0.48 if not pushed else 0.34) + 0.04 * (1 - (r / CAP) ** 2)
    h = top - 0.30 * (1 - np.sqrt(np.clip(1 - t ** 2, 0, 1)))
    # the hole around it (a dark ring, its wall catching a little light at the bottom)
    hole_h = -0.15 + 0.05 * smoothstep(CAP, 1.0, r)
    h = np.where(r < CAP, h, hole_h)
    hs = ndimage.gaussian_filter(h, R * 0.02)
    n = T.normals_from_height(hs, 1.0 / R)
    alb = np.array(BUTTON_COLOURS[colour], np.float32)
    gloss = 0.9 if colour != "white" else 0.7
    sh = 0.25 + 0.75 * T.cast_shadow(hs, R, yy, xx, slope=0.45, reach=0.5)
    capcol = T.shade_photo(n, alb, f0=0.045, gloss=gloss, shadow=sh, soft=0.02, strip_gain=0.4)
    if pushed:
        # the panel's edge shades the top of a pushed-in cap
        capcol *= (1 - 0.45 * smoothstep(-0.2, -0.95, v) * smoothstep(0.3, 0.86, r))[..., None]
        capcol *= 0.88
    holecol = np.array([12, 11, 11], np.float32)[None, None, :] * (1 + 2.5 * smoothstep(0.0, 0.9, v))[..., None]
    capm = 1 - smoothstep(CAP - 0.02, CAP + 0.01, r)
    col = capcol * capm[..., None] + holecol * (1 - capm[..., None])
    alpha = 1 - smoothstep(1.0 - 1.2 / R * ss, 1.0, r)
    img = T.to_img(col, alpha).resize((BUTTON_FRAME, BUTTON_FRAME), Image.LANCZOS)
    return T.photo_finish(img, blur=0.45, grain=1.4, seed=21 + int(pushed) + len(colour))


def render_button_strips():
    for colour in BUTTON_COLOURS:
        strip = Image.new("RGBA", (BUTTON_FRAME, BUTTON_FRAME * 2))
        for i in range(2):
            strip.paste(button_frame(colour, i == 1), (0, i * BUTTON_FRAME))
        strip.save(os.path.join(OUT, f"button_{colour}_strip.png"), optimize=True)


# --------------------------------------------------------------------------
# LED RING round every push button (as on Lindell STC-8), 2 frames: 0 = off, 1 = lit
# --------------------------------------------------------------------------
RING_R0, RING_R1 = BUTTON_R * 1.08, BUTTON_R * 1.26
RING_FRAME = int(round((RING_R1 + 9.0) * SCALE)) * 2


def ring_frame(lit, ss=4):
    """the LED ring set into the panel round a push button: a frosted light-pipe,
    dark smoked green when off, glowing green when the button is in"""
    N = RING_FRAME * ss
    c = (N - 1) / 2.0
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32)
    px = SCALE * ss                                     # px per photo px
    r = np.hypot(xx - c, yy - c) / px                   # photo px
    v = (yy - c) / px / RING_R1                         # -1 top .. +1 bottom
    mid, half = 0.5 * (RING_R0 + RING_R1), 0.5 * (RING_R1 - RING_R0)
    t = np.clip(1 - np.abs(r - mid) / half, 0, 1)       # 0 at the edges .. 1 on the centre line
    ring = smoothstep(0.0, 0.35, t)
    groove = smoothstep(0.0, 0.25, np.clip(1 - np.abs(r - mid) / (half + 0.8), 0, 1)) * (1 - ring)
    if not lit:
        col = np.dstack([28 + 10 * t, 42 + 16 * t, 34 + 11 * t])
        col *= (1.0 - 0.35 * smoothstep(-0.2, -1.0, v))[..., None]               # shaded at the top lip
        col += (14 * np.exp(-((r - (RING_R0 + 0.35 * half)) / 0.35) ** 2) * smoothstep(0.0, 0.9, v))[..., None]
        alpha = np.clip(0.92 * ring + 0.30 * groove, 0, 1)
        col = col * ring[..., None] + np.array([16, 16, 15], np.float32) * (1 - ring[..., None])
    else:
        core = t ** 1.6
        col = 0.6 * np.dstack([40 + 150 * core, 215 + 40 * core, 105 + 120 * core])   # 40 % below full brightness
        halo = ndimage.gaussian_filter(ring, 2.6 * px) * 1.9 + ndimage.gaussian_filter(ring, 0.9 * px) * 0.9
        glow_a = np.clip(halo, 0, 1) * (1 - ring) * 0.75 * 0.6
        alpha = np.clip(ring + glow_a, 0, 1)
        glow_col = 0.6 * np.array([60, 255, 120], np.float32)
        col = (col * ring[..., None] + glow_col * glow_a[..., None]) / np.maximum(alpha, 1e-4)[..., None]
    img = T.to_img(col, alpha).resize((RING_FRAME, RING_FRAME), Image.LANCZOS)
    return T.photo_finish(img, blur=0.35, grain=0.0, seed=7 + int(lit))


def render_ring_strip():
    strip = Image.new("RGBA", (RING_FRAME, RING_FRAME * 2))
    for i in range(2):
        strip.paste(ring_frame(i == 1), (0, i * RING_FRAME))
    strip.save(os.path.join(OUT, "button_ring_strip.png"), optimize=True)



# --------------------------------------------------------------------------
# ANALYSER DISPLAY - a second plate under the faceplate: brushed aluminium,
# a smoked glass window with a printed grid (as the Tracker's WINDOW display)
# --------------------------------------------------------------------------
def spec_x(f):
    x0, _, x1, _ = SPEC
    return x0 + (x1 - x0) * math.log10(f / 20.0) / 3.0


def render_window():
    WH = int(WIN_H * SCALE)
    saved = (T.PH, T.H)
    T.PH, T.H = WIN_H, WH
    try:
        rng = T.rng
        yy, xx = np.mgrid[0:WH, 0:W].astype(np.float32)
        u, v = xx / W, yy / WH

        def streaks(length, amp):
            n = T.box_blur_x(rng.normal(size=(WH, W)).astype(np.float32), length)
            return amp * n / n.std()
        brushed, sheen = plate_material(u, plate_v(v, True), rng)
        rgb = plate_rgb(brushed, sheen)

        T.glass_window(rgb, WIN_DISPLAY)
        G = T.Legend()       # faint grid
        Lb = T.Legend()      # labels
        sx0, sy0, sx1, sy1 = SPEC
        for f in (50, 100, 200, 500, 1000, 2000, 5000, 10000):
            x = spec_x(f)
            G.line(x, sy0, x, sy1, 0.7)
            Lb.text(f"{f // 1000}k" if f >= 1000 else str(f), x, sy1 + 10.0, 6.4, bold=True)
        Lb.text("20", sx0, sy1 + 10.0, 6.4, bold=True)
        Lb.text("20k", sx1, sy1 + 10.0, 6.4, bold=True)
        for i in range(7):                               # shared grid: 12 dB (spectrum) = 6 dB (EQ)
            y = sy0 + (sy1 - sy0) * i / 6.0
            G.line(sx0, y, sx1, y, 0.7 if i != 3 else 1.0)
            Lb.text(str(-12 * i), sx0 - 7.0, y, 6.4, align="r", bold=True)
            eq = int(EQ_DB - 6 * i)
            Lb.text(("+" if eq > 0 else "") + str(eq), sx1 + 7.0, y, 6.4, align="l", bold=True)
        Lb.text("dBFS", sx0 - 7.0, sy0 - 9.0, 5.8, align="r", bold=True)
        Lb.text("dB", sx1 + 7.0, sy0 - 9.0, 5.8, align="l", bold=True)
        Lb.text("SPECTRUM   IN / OUT", sx0 + 6.0, sy0 + 8.0, 6.4, align="l", bold=True)
        Lb.text("EQ CURVE", sx1 - 6.0, sy0 + 8.0, 6.4, align="r", bold=True)
        T.blend(rgb, (206, 206, 200), ndimage.gaussian_filter(G.mask(), 0.5) * 0.16)
        T.blend(rgb, (206, 206, 200), ndimage.gaussian_filter(Lb.mask(), 0.5) * 0.80)

        rgb *= plate_light(u, plate_v(v, True))[..., None]     # one light over both plates
        cov = T.rounded_rect_mask(W, WH, P(2), P(1), P(PW - 2), P(WIN_H - 2), P(7))
        dist = ndimage.distance_transform_edt(cov > 0.5).astype(np.float32)
        rgb *= (1 - 0.14 * np.exp(-dist / P(1.4)))[..., None]
        back = np.full_like(rgb, 18.0)
        out = back * (1 - cov[..., None]) + rgb * cov[..., None]
        out = T.photo_grade(out, contrast=1.14, bloom=0.45)
        img = T.photo_finish(T.to_img(out), blur=0.85, grain=2.4, seed=17, sharpen=0.55)
        img.save(os.path.join(OUT, "window.png"), optimize=True)
        return img
    finally:
        T.PH, T.H = saved


# --------------------------------------------------------------------------
# Layout.h
# --------------------------------------------------------------------------
def write_layout_header():
    kinds = dict(lowcut="LowCut", colsrc="ColourSource", colour="Colour", gain="Gain", freq="Freq", bw="Bandwidth")
    colours = dict(red="Red", black="Black", white="White")
    L = [
        "// AUTO-GENERATED by tools/render_assets.py - do not edit by hand.",
        f"// Panel units: {PW} x {PH}; assets are rendered at {SCALE}x.",
        "#pragma once",
        "",
        "namespace layout",
        "{",
        f"    constexpr float panelW = {PW}.0f, panelH = {PH}.0f, assetScale = {SCALE}.0f;",
        f"    constexpr int largeKnobFrame = {LARGE_FRAME}, smallKnobFrame = {SMALL_FRAME}, knobFrames = {KNOB_FRAMES};",
        f"    constexpr float largeKnobR = {LARGE_R}f, smallKnobR = {SMALL_R}f;",
        f"    constexpr int buttonFrame = {BUTTON_FRAME};",
        f"    constexpr float buttonR = {BUTTON_R}f;",
        f"    constexpr int ringFrame = {RING_FRAME};",
        f"    constexpr int toggleFrame = {T.TOGGLE_FRAME};",
        f"    constexpr float toggleR = {TOGGLE_R}f;",
        f"    constexpr int lampFrame = {T.LAMP_FRAME};",
        f"    constexpr float lampX = {LAMP[0]}f, lampY = {LAMP[1]}f;",
        "",
        "    // analyser display under the faceplate (window coordinates, same width)",
        f"    constexpr float winH = {float(WIN_H)}f;",
        f"    constexpr float dispX0 = {WIN_DISPLAY[0]}f, dispY0 = {WIN_DISPLAY[1]}f, dispX1 = {WIN_DISPLAY[2]}f, dispY1 = {WIN_DISPLAY[3]}f;",
        f"    constexpr float specX0 = {SPEC[0]}f, specY0 = {SPEC[1]}f, specX1 = {SPEC[2]}f, specY1 = {SPEC[3]}f;",
        f"    constexpr float specDb = {SPEC_DB}f, eqDb = {EQ_DB}f;",
        "",
        "    enum class KnobKind { LowCut, ColourSource, Colour, Gain, Freq, Bandwidth };",
        "    enum class ButtonColour { Red, Black, White };",
        "",
        "    struct Sweep { float startDeg, endDeg; };",
        "    inline Sweep sweepFor (KnobKind k)",
        "    {",
        "        switch (k)",
        "        {",
    ]
    for key, kind in kinds.items():
        a0, a1 = SWEEP[{"lowcut": "lowcut", "colsrc": "colsrc", "colour": "colour", "gain": "gain", "freq": "freq", "bw": "bw"}[key]]
        L.append(f"            case KnobKind::{kind}: return {{ {a0}f, {a1}f }};")
    L += ["        }", "        return { -150.0f, 150.0f };", "    }", "",
          "    struct Knob { int channel; KnobKind kind; int band; float x, y; bool large; };",
          "    inline constexpr Knob knobs[] =", "    {"]
    for c, kind, band, x, y, large in knob_list():
        L.append(f"        {{ {c}, KnobKind::{kinds[kind]}, {band}, {x}f, {y}f, {'true' if large else 'false'} }},")
    L += ["    };", "", "    struct Button { const char* name; ButtonColour colour; float x, y; };",
          "    inline constexpr Button buttons[] =", "    {"]
    for name, colour, x, y in button_list():
        L.append(f"        {{ \"{name}\", ButtonColour::{colours[colour]}, {x}f, {y}f }},")
    L += ["    };", "", "    struct Toggle { const char* name; float x, y; };",
          "    inline constexpr Toggle toggles[] =", "    {"]
    for name, (x, y) in TOGGLES.items():
        L.append(f"        {{ \"{name}\", {x}f, {y}f }},")
    L += ["    };", "}", ""]
    with open(os.path.join(ROOT, "Source", "Layout.h"), "w") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    what = sys.argv[1:] or ["all"]
    if "all" in what or "panel" in what:
        render_panel(); print("panel")
    if "all" in what or "window" in what:
        render_window(); print("window")
    if "all" in what or "knobs" in what or "large" in what:
        render_knob_strip(LARGE_R, LARGE_FRAME, "knob_large_strip.png"); print("large knob")
    if "smallknob" in what:
        render_small_knob_strip(); print("small knob")
        render_small_knob_strip(); print("small knob")
    if "all" in what or "small" in what or "buttons" in what:
        render_button_strips(); render_ring_strip(); print("buttons + LED ring")
    if "all" in what or "small" in what:
        T.render_toggle_strip(); print("toggle")
        T.render_lamp_strip(); print("lamp")
    write_layout_header()
