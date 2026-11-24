#!/usr/bin/env python3
"""
Builds the bitmap GUI assets from a front-panel photo.

    python3 tools/extract_panel.py <photo.jpg>

Writes:
  Resources/panel/*.png   panel background (rotating caps removed), knob caps,
                          pressed-button states, toggle states, unlit lamp
  Source/PanelLayout.h    positions, sizes and photo angles of every control

The coordinates below are for the 2000x620 reference photo. Use a photo with the
same framing, or re-measure the seed points, PANEL box and POINTER angles.
Needs numpy, pillow and scipy.
"""
import sys, os, math
import numpy as np
from PIL import Image
from scipy import ndimage as ndi

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'Resources', 'panel')
HDR = os.path.join(ROOT, 'Source', 'PanelLayout.h')

PANEL = (45, 168, 1961, 538)  # x0, y0, x1, y1 in photo pixels

# Seed points (photo coords) for every knob, per channel
SEEDS = {
 1: dict(lowcut=(256,251), colsrc=(255,403), colour=(256,492),
         gain=[(403,250),(551,251),(696,252),(845,250)], freq=[(402,359),(550,359),(697,358),(845,358)],
         bw=[(402,494),(551,492),(699,493),(847,492)]),
 2: dict(lowcut=(1128,249), colsrc=(1128,400), colour=(1128,490),
         gain=[(1273,248),(1420,247),(1568,247),(1714,245)], freq=[(1275,355),(1422,355),(1569,354),(1717,352)],
         bw=[(1277,490),(1423,491),(1571,488),(1718,487)]),
}

# Where each knob points in the photo (degrees, 0 = 12 o'clock, clockwise),
# read off the white pointer line / dot.
POINTER = {
 # Large knobs: direction of the white dot. Small knobs: direction of the tip of the pointer line.
 (1,'lowcut'): -137, (1,'colsrc'): 33.0, (1,'colour'): -117.8,
 (1,'gain1'): 96.5,  (1,'freq1'): -124.7, (1,'bw1'): 4.9,
 (1,'gain2'): -118,  (1,'freq2'): -92.9,  (1,'bw2'): -100.1,
 (1,'gain3'): 20,    (1,'freq3'): -67.5,  (1,'bw3'): -153.6,
 (1,'gain4'): 70,    (1,'freq4'): -33.2,  (1,'bw4'): -2.3,
 (2,'lowcut'): -78,  (2,'gain1'): 51, (2,'gain2'): 31, (2,'gain3'): -107, (2,'gain4'): 30,
}
# CH2's small caps have chrome glare in their cut-outs; they reuse CH1's caps (same part).
SHARED_SMALL_CAPS = True

# Push buttons: centre x, centre y, radius
BUTTONS = {
  'c1_shelf1': (475.5, 302.0, 19), 'c1_shelf4': (914.0, 302.5, 19),
  'c2_shelf1': (1341.5, 300.5, 19), 'c2_shelf4': (1779.5, 297.5, 19),
  'c1_steep': (327.0, 324.0, 18), 'c2_steep': (1196.5, 321.5, 18),
  'c1_step1': (420.0, 422.0, 17.5), 'c1_step2': (564.0, 422.0, 17.5), 'c1_step3': (711.0, 420.5, 17.5), 'c1_step4': (858.0, 421.0, 17.5),
  'c2_step1': (1286.5, 418.5, 17.5), 'c2_step2': (1432.5, 416.5, 17.5), 'c2_step3': (1579.0, 418.5, 17.5), 'c2_step4': (1724.5, 418.5, 17.5),
}
TOGGLES = {'ch1': (990.0, 223.0), 'ch2': (992.0, 300.0), 'power': (993.0, 477.0)}  # lever points left in the photo
LAMP = (993.0, 379.0, 27)

def main(photo):
    os.makedirs(OUT, exist_ok=True)
    img = Image.open(photo).convert('RGB')
    im = np.asarray(img).astype(np.float32)
    R, G, B = im[..., 0], im[..., 1], im[..., 2]
    H, W = R.shape
    yy, xx = np.mgrid[0:H, 0:W]
    OX, OY, OX2, OY2 = PANEL
    green = (G > R + 25) & (G > B - 10) & (G > 60)
    panel = im.copy()

    def save(arr, name, mode):
        Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), mode).save(os.path.join(OUT, name))

    knobs = []
    for ch, s in SEEDS.items():
        items = [('lowcut', s['lowcut'], True, 0), ('colsrc', s['colsrc'], False, 0), ('colour', s['colour'], False, 0)]
        for b in range(4):
            items += [(f'gain{b+1}', s['gain'][b], True, b+1), (f'freq{b+1}', s['freq'][b], False, b+1), (f'bw{b+1}', s['bw'][b], False, b+1)]
        for name, (sx, sy), large, band in items:
            rad = 50 if large else 34
            win = np.zeros_like(green); win[sy-rad:sy+rad, sx-rad:sx+rad] = True
            m = ndi.binary_fill_holes(ndi.binary_closing(green & win, iterations=2))
            lab, n = ndi.label(m)
            l = lab[sy, sx] or int(np.argmax(ndi.sum(m, lab, range(1, n+1)))) + 1
            ys, xs = np.nonzero(lab == l)
            r = (xs.max() - xs.min() + 1) / 2.0
            cx = (xs.min() + xs.max()) / 2.0
            cy = ys.min() + r
            disk = np.hypot(xx - cx, yy - cy) <= r + 0.5
            cap = ndi.binary_fill_holes((lab == l) & disk)
            if not large:
                # Only the green plastic (and the pointer line it encloses) rotates;
                # chrome glare from the nut stays in the background.
                plastic = ndi.binary_fill_holes(ndi.binary_opening(green & disk, iterations=1))
                cap = ndi.binary_dilation(plastic, iterations=1) & cap

            S = int(math.ceil(r + 3))
            bx, by = int(round(cx)) - S, int(round(cy)) - S
            crop = im[by:by+2*S+1, bx:bx+2*S+1]
            alpha = ndi.gaussian_filter(cap[by:by+2*S+1, bx:bx+2*S+1].astype(np.float32), 0.6) * 255
            if large:
                d = np.hypot(*np.mgrid[-S:S+1, -S:S+1].astype(np.float32)) + 0.0
                d = np.hypot(np.arange(-S, S+1)[None, :] + (bx + S - cx), np.arange(-S, S+1)[:, None] + (by + S - cy))
                alpha = np.maximum(alpha, np.clip(r - 0.5 - d, 0, 1) * 255)
            res = f'knob_c{ch}_{name}.png'
            save(np.dstack([crop, alpha]), res, 'RGBA')

            # Remove the photographed cap from the background so a rotated cap leaves no ghost.
            fill = ndi.binary_dilation(cap | (disk if large else False), iterations=2)
            ring = ndi.binary_dilation(fill, iterations=3) & ~fill
            colr = np.array([20, 70, 55], np.float32) if large else np.median(im[ring], axis=0)
            for c in range(3):
                panel[..., c][fill] = colr[c]

            entry = dict(ch=ch, kind=''.join(ch_ for ch_ in name if not ch_.isdigit()), band=band, res=res,
                         x=bx - OX, y=by - OY, size=2*S+1, px=cx - OX, py=cy - OY, angle=POINTER.get((ch, name)))
            if ch == 2 and not large and SHARED_SMALL_CAPS:
                os.remove(os.path.join(OUT, res))
                src = next(k for k in knobs if k['ch'] == 1 and k['name'] == name)
                entry.update(res=src['res'], size=src['size'], angle=src['angle'],
                             x=int(round(entry['px'] - (src['px'] - src['x']))), y=int(round(entry['py'] - (src['py'] - src['y']))))
                entry['px'] = entry['x'] + (src['px'] - src['x']); entry['py'] = entry['y'] + (src['py'] - src['y'])
            entry['name'] = name
            knobs.append(entry)

    buttons = []
    for name, (cx, cy, r) in BUTTONS.items():
        S = int(math.ceil(r + 4))
        bx, by = int(round(cx)) - S, int(round(cy)) - S
        crop = im[by:by+2*S+1, bx:bx+2*S+1].copy()
        oy, ox = np.mgrid[-S:S+1, -S:S+1].astype(np.float32)
        ox += bx + S - cx; oy += by + S - cy
        d = np.hypot(ox, oy)
        # Pressed-in state: a dark recess, with the cap sunk 10%, darker, and a touch lower.
        recess = np.clip(1.0 - (r + 0.8 - d), 0, 1)[..., None]
        hole = (0.18 + 0.10 * np.clip(oy / r, -1, 1))[..., None] * 255 * np.ones(3)
        pressed = crop * recess + hole * (1 - recess)
        k = 0.9
        coords = [(oy - 0.8) / k + cy - by, ox / k + cx - bx]
        sunk = np.dstack([ndi.map_coordinates(crop[..., c], coords, order=1, mode='nearest') for c in range(3)]) * 0.82
        a = np.clip(r * k + 0.5 - np.hypot(ox, oy - 0.8), 0, 1)[..., None]
        pressed = pressed * (1 - a) + sunk * a
        alpha = np.clip(r + 2.5 - d, 0, 1) * 255
        save(np.dstack([pressed, alpha]), f'btn_{name}_on.png', 'RGBA')
        buttons.append(dict(name=name, res=f'btn_{name}_on.png', x=bx - OX, y=by - OY, size=2*S+1))

    toggles = []
    for name, (cx, cy) in TOGGLES.items():
        h = 26
        x0, y0 = int(round(cx)) - h, int(round(cy)) - h
        crop = np.asarray(img.crop((x0, y0, x0 + 2*h + 1, y0 + 2*h + 1))).astype(np.float32)
        flipped = crop[:, ::-1]
        # Flip only the lever and nut (an ellipse around the nut), not the printed lettering.
        oy, ox = np.mgrid[-h:h+1, -h:h+1].astype(np.float32)
        e = np.hypot(ox / 21.0, oy / 18.0)
        alpha = np.clip((1.0 - e) * 18.0, 0, 1) * 255
        save(np.dstack([flipped, alpha]), f'toggle_{name}_right.png', 'RGBA')
        toggles.append(dict(name=name, res=f'toggle_{name}_right.png', x=x0 - OX, y=y0 - OY, size=2*h + 1))

    lcx, lcy, lr = LAMP
    S = 33
    x0, y0 = int(lcx) - S, int(lcy) - S
    crop = im[y0:y0+2*S+1, x0:x0+2*S+1].copy()
    d = np.hypot(*np.mgrid[-S:S+1, -S:S+1])
    lum = crop.mean(-1, keepdims=True)
    dim = (0.35 * crop + 0.65 * lum) * 0.42 + np.array([4, 10, 6])
    w = np.clip(lr + 0.5 - d, 0, 1)[..., None]
    save(crop * (1 - w) + dim * w, 'lamp_off.png', 'RGB')

    save(panel[OY:OY2, OX:OX2], 'panel.png', 'RGB')

    # ---- header ----
    def ident(res): return res.replace('.', '_')
    L = ['// Generated by tools/extract_panel.py. Do not edit by hand.', '#pragma once', '',
         'namespace heron::panel', '{',
         f'    constexpr int width = {OX2-OX}, height = {OY2-OY};', '',
         '    enum class KnobKind { LowCut, ColourSource, Colour, Gain, Freq, Bandwidth };', '',
         '    struct Knob { int channel; KnobKind kind; int band; const char* resource; int x, y, size; float pivotX, pivotY, photoAngleDeg; };',
         '    struct Sprite { const char* name; const char* resource; int x, y, size; };', '',
         '    inline constexpr Knob knobs[] =', '    {']
    kinds = dict(lowcut='LowCut', colsrc='ColourSource', colour='Colour', gain='Gain', freq='Freq', bw='Bandwidth')
    for k in knobs:
        L.append(f'        {{ {k["ch"]}, KnobKind::{kinds[k["kind"]]}, {k["band"]}, "{ident(k["res"])}", {k["x"]}, {k["y"]}, {k["size"]}, {k["px"]:.1f}f, {k["py"]:.1f}f, {k["angle"]:.1f}f }},')
    L += ['    };', '', '    // Pressed-in push buttons', '    inline constexpr Sprite buttons[] =', '    {']
    for b in buttons:
        L.append(f'        {{ "{b["name"]}", "{ident(b["res"])}", {b["x"]}, {b["y"]}, {b["size"]} }},')
    L += ['    };', '', '    // Toggle levers flipped to the right (OUT / OFF)', '    inline constexpr Sprite toggles[] =', '    {']
    for t in toggles:
        L.append(f'        {{ "{t["name"]}", "{ident(t["res"])}", {t["x"]}, {t["y"]}, {t["size"]} }},')
    L += ['    };', '', f'    inline constexpr Sprite lampOff {{ "lamp", "lamp_off_png", {x0-OX}, {y0-OY}, {2*S+1} }};', '}', '']
    open(HDR, 'w').write('\n'.join(L))
    print(f'wrote {len(knobs)} knobs, {len(buttons)} buttons, {len(toggles)} toggles to {OUT}')

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
