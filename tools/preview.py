#!/usr/bin/env python3
"""Quick composite of the rendered assets (no build needed).
    python3 tools/preview.py out.png [width]
Knobs are shown at their default positions, buttons out, switches IN / ON."""
import os
import sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_assets as A  # noqa: E402

DEFAULT_T = dict(lowcut=0.0, colsrc=0.0, colour=0.0, gain=0.5, freq=None, bw=0.45)
FREQ_DEFAULT = [3, 5, 6, 7]


def main(out, width=1916):
    k = A.SCALE
    panel = Image.open(os.path.join(A.OUT, "panel.png")).convert("RGBA")

    def strip_frame(name, fs, i):
        s = Image.open(os.path.join(A.OUT, name))
        return s.crop((0, i * fs, fs, (i + 1) * fs))

    def put(img, cx, cy):
        panel.alpha_composite(img, (int(round(cx * k - img.width / 2)), int(round(cy * k - img.height / 2))))

    for c, kind, band, x, y, large in A.knob_list():
        t = DEFAULT_T[kind] if kind != "freq" else FREQ_DEFAULT[band - 1] / 11.0
        a0, a1 = A.SWEEP[kind]
        ang = (a0 + t * (a1 - a0)) % 360.0
        fi = int(round(ang / 360.0 * A.KNOB_FRAMES)) % A.KNOB_FRAMES
        if large:
            put(strip_frame("knob_large_strip.png", A.LARGE_FRAME, fi), x, y)
        else:
            put(strip_frame("knob_small_strip.png", A.SMALL_FRAME, fi), x, y)
    for name, colour, x, y in A.button_list():
        put(strip_frame(f"button_{colour}_strip.png", A.BUTTON_FRAME, 0), x, y)
    for name, (x, y) in A.TOGGLES.items():
        put(strip_frame("toggle_strip.png", A.T.TOGGLE_FRAME, 0), x, y)
    put(strip_frame("lamp_strip.png", A.T.LAMP_FRAME, 1), A.LAMP[0], A.LAMP[1])
    img = panel.convert("RGB")
    if width != img.width:
        img = img.resize((width, round(width * img.height / img.width)), Image.LANCZOS)
    img.save(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "preview.png", int(sys.argv[2]) if len(sys.argv) > 2 else 1916)
