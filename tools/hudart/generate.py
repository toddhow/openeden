#!/usr/bin/env python3
"""Generates the Skyrim-style HUD's frame art (task 0252, decision 0015).

Every picture is drawn here from plain geometry: polygons, lines and gradients, with 4x supersampling. No texture,
shape or palette is taken from any game or mod (the look is only "pointed ornamental frames over a dark backing with a
thin metal border", which is a layout idea, not a work). The pictures are original to this project and under its licence
(GPL-3.0-or-later).

  python tools/hudart/generate.py [output folder]     (default: files/data/textures/openeden/hud)

Needs Pillow. The output is deterministic (running it twice gives identical bytes).

Files:
  frame_h<H>_l.png, _c.png, _r.png   a bar frame H pixels high as three slices (left cap, a centre tile that repeats,
                                      right cap): the middle stretches to any width. The bar itself sits `INSET` pixels
                                      in from the top and bottom and `CAP` pixels in from each end.
  fill_<red|blue|green|trail>.png    one pixel wide gradient strips for the bars' fill and the pale damage trail.
  emblem.png                         the player cluster's level frame.
"""
import os
import sys

from PIL import Image, ImageDraw

SS = 4  # supersampling
CAP = 10  # width of a frame's end caps (the bar's interior starts this far in); skyrimstyle.lua's ART_CAP
INSET = 4  # a frame is the bar's height plus 2 * INSET; skyrimstyle.lua's ART_INSET
FRAME_HEIGHTS = (24, 20, 22, 14, 11)  # the player's health, magicka/stamina, the boss bar, an actor's bar, its small bars
EMBLEM = 48

METAL_LIGHT = (214, 200, 160)
METAL_DARK = (96, 86, 62)
BACKING = (6, 6, 8, 178)
STUD = (236, 224, 186, 255)


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def new(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def shrink(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def vertical_metal(draw_poly_mask, w, h, light, dark):
    """A vertical metal gradient (light at the top, dark at the bottom) seen through a mask."""
    grad = Image.new("RGBA", (w * SS, h * SS))
    px = grad.load()
    for y in range(h * SS):
        t = y / max(1, h * SS - 1)
        # a brighter band near the top, a dark band near the bottom
        c = lerp(light, dark, min(1.0, t * 1.25)) if t < 0.8 else lerp(dark, light, (t - 0.8) * 1.5)
        for x in range(w * SS):
            px[x, y] = (*c, 255)
    return grad


def frame_strip(height):
    """The whole frame at width 2*CAP + 8, as RGBA at 1x."""
    w = 2 * CAP + 8
    h = height
    small = h < 16
    border = 1.0 if small else 2.0
    s = SS

    def pts(inset):
        i = inset
        mid = h / 2
        return [
            (0 + i * 0.6, mid), (CAP - i * 0.2, 0 + i), (w - CAP + i * 0.2, 0 + i), (w - i * 0.6, mid),
            (w - CAP + i * 0.2, h - i), (CAP - i * 0.2, h - i),
        ]

    def scaled(p):
        return [(x * s, y * s) for x, y in p]

    img = new(w, h)
    d = ImageDraw.Draw(img)
    # the dark backing fills the whole silhouette
    d.polygon(scaled(pts(0)), fill=BACKING)
    # the metal border: the silhouette minus a slightly smaller one, painted with a vertical metal gradient
    mask = Image.new("L", (w * s, h * s), 0)
    md = ImageDraw.Draw(mask)
    md.polygon(scaled(pts(0)), fill=255)
    md.polygon(scaled(pts(border)), fill=0)
    metal = vertical_metal(None, w, h, METAL_LIGHT, METAL_DARK)
    img.paste(metal, (0, 0), mask)
    # a thin dark line just inside the border so the metal reads as raised
    inner = Image.new("L", (w * s, h * s), 0)
    idr = ImageDraw.Draw(inner)
    idr.polygon(scaled(pts(border)), fill=255)
    idr.polygon(scaled(pts(border + 1.0)), fill=0)
    shadow = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 200))
    img.paste(shadow, (0, 0), inner)
    # the little diamond studs at the tips
    if not small:
        for cx in (CAP * 0.42, w - CAP * 0.42):
            r = 1.9
            cy = h / 2
            d.polygon(scaled([(cx - r, cy), (cx, cy - r), (cx + r, cy), (cx, cy + r)]), fill=STUD)
    return shrink(img, w, h)


def save(img, path):
    img.save(path, "PNG", compress_level=9)


def write_frames(out):
    for h in FRAME_HEIGHTS:
        strip = frame_strip(h)
        w = strip.width
        save(strip.crop((0, 0, CAP, h)), os.path.join(out, "frame_h%d_l.png" % h))
        save(strip.crop((CAP, 0, CAP + 4, h)), os.path.join(out, "frame_h%d_c.png" % h))
        save(strip.crop((w - CAP, 0, w, h)), os.path.join(out, "frame_h%d_r.png" % h))


def write_fill(out, name, top, mid, bottom, gloss):
    w, h = 4, 32
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        c = lerp(top, mid, t / 0.45) if t < 0.45 else lerp(mid, bottom, (t - 0.45) / 0.55)
        if y < 3:  # the gloss line along the top edge
            c = lerp(c, (255, 255, 255), gloss * (1 - y / 3))
        for x in range(w):
            px[x, y] = (*c, 255)
    save(img, os.path.join(out, "fill_%s.png" % name))


def write_emblem(out):
    s = SS
    n = EMBLEM
    img = new(n, n)
    d = ImageDraw.Draw(img)
    c = n / 2

    def octagon(r, cut):
        return [(c - r + cut, c - r), (c + r - cut, c - r), (c + r, c - r + cut), (c + r, c + r - cut),
                (c + r - cut, c + r), (c - r + cut, c + r), (c - r, c + r - cut), (c - r, c - r + cut)]

    def sc(p):
        return [(x * s, y * s) for x, y in p]

    d.polygon(sc(octagon(n / 2 - 0.5, 9)), fill=BACKING)
    mask = Image.new("L", (n * s, n * s), 0)
    md = ImageDraw.Draw(mask)
    md.polygon(sc(octagon(n / 2 - 0.5, 9)), fill=255)
    md.polygon(sc(octagon(n / 2 - 2.6, 7.5)), fill=0)
    img.paste(vertical_metal(None, n, n, METAL_LIGHT, METAL_DARK), (0, 0), mask)
    inner = Image.new("L", (n * s, n * s), 0)
    idr = ImageDraw.Draw(inner)
    idr.polygon(sc(octagon(n / 2 - 2.6, 7.5)), fill=255)
    idr.polygon(sc(octagon(n / 2 - 3.8, 6.5)), fill=0)
    img.paste(Image.new("RGBA", (n * s, n * s), (0, 0, 0, 200)), (0, 0), inner)
    # four diamond studs at the points of the compass
    for cx, cy in ((c, 3.0), (c, n - 3.0), (3.0, c), (n - 3.0, c)):
        r = 2.0
        d.polygon(sc([(cx - r, cy), (cx, cy - r), (cx + r, cy), (cx, cy + r)]), fill=STUD)
    save(shrink(img, n, n), os.path.join(out, "emblem.png"))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "files", "data", "textures", "openeden", "hud")
    os.makedirs(out, exist_ok=True)
    write_frames(out)
    write_fill(out, "red", (222, 84, 74), (170, 34, 32), (96, 14, 18), 0.45)
    write_fill(out, "blue", (98, 150, 238), (46, 88, 182), (22, 40, 108), 0.45)
    write_fill(out, "green", (118, 206, 108), (56, 152, 62), (24, 84, 34), 0.45)
    write_fill(out, "trail", (244, 236, 220), (226, 216, 198), (178, 168, 150), 0.3)
    write_emblem(out)


if __name__ == "__main__":
    main()
