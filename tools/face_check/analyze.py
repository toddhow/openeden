#!/usr/bin/env python3
"""tools/face_check (task 0071): compares the mouth in the check's screenshots with the test-pattern curve.

usage: analyze.py <work dir> [x0 y0 x1 y1]   (the mouth box in pixels of the 800x600 frames; default 290 345 470 420)

For each "FACE shot N" log line it takes the last FACETRACE line at or before that time (the voice stream position t of the
frame the screenshot was requested in), computes the curve's jaw value at t (mkcurves.cpp's pattern: 0 until 0.3 s, then a
triangle wave of period 0.4 s to 1.9 s, then 0), and measures a mouth-opening score from the screenshot: a lip-line darkness score (see lipline()).
Writes montage.png (the mouth crops, labelled) and montage_eyes.png (the eye crops with the sampled blink) and prints a table
with the Pearson correlation of score and curve value. Needs Pillow.
"""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

LIPS = (340, 360, 420, 410)  # where the lip line is in the close-up
EYES = (300, 295, 440, 340)  # the eye box of the check's close-up (pixels of an 800x600 frame)


def lipline(image):
    """Mouth-opening score: how much darker the three darkest rows of the lip area are than its median row. An open mouth
    shows a dark band (and teeth), a closed one a thin line. A crude measure, only meant to follow the curve."""
    w, h = image.size
    px = image.load()
    rows = sorted(sum(px[x, y] for x in range(w)) / w for y in range(h))
    return rows[len(rows) // 2] - sum(rows[:3]) / 3


def stamp(line):
    m = re.match(r"\[(\d+):(\d+):(\d+\.\d+)", line)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else None


def curve(t):
    if t < 0.3 or t >= 1.9:
        return 0.0
    phase = ((t - 0.3) % 0.4) / 0.4
    return phase * 2 if phase < 0.5 else (1 - phase) * 2


def main():
    work = Path(sys.argv[1])
    box = tuple(int(v) for v in sys.argv[2:6]) if len(sys.argv) >= 6 else (290, 345, 470, 420)
    trace = []  # (time, stream t, talk, blink)
    shots = []  # (time, index)
    for line in (work / "log.txt").read_text(errors="replace").splitlines():
        m = re.search(r"FACETRACE (\S+) t=(\S+) active=(\d) talk=(\S+) blink=(\S+)", line)
        if m and "hlo_af002" in m.group(1):
            trace.append((stamp(line), float(m.group(2)), float(m.group(4)), float(m.group(5)), int(m.group(3))))
        m = re.search(r"FACE shot (\d+) at", line)
        if m:
            shots.append((stamp(line), int(m.group(1))))
    frames = sorted((work / "frames").glob("*.png"))
    rows = []
    crops = []
    eye_crops = []
    for (when, idx), path in zip(shots, frames):
        before = [t for t in trace if t[0] <= when + 1e-6]
        if not before:
            continue
        _, t, talk, blink, active = before[-1]
        image = Image.open(path).convert("L")
        crop = image.crop(box)
        eyes = image.crop(EYES)
        rows.append((idx, t, curve(t), talk, blink, lipline(image.crop(LIPS))))
        crops.append((idx, t, crop))
        eye_crops.append((idx, blink, eyes))
    if not rows:
        print("no data")
        return 1
    print("shot  stream_t  curve_jaw  sampled_talk  blink  lip_score")
    for r in rows:
        print("%4d  %8.3f  %9.2f  %12.2f  %5.2f  %9.2f" % r)
    n = len(rows)
    xs = [r[2] for r in rows]
    ys = [r[5] for r in rows]
    mx, my = sum(xs) / n, sum(ys) / n
    cov = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    vx = sum((x - mx) ** 2 for x in xs)
    vy = sum((y - my) ** 2 for y in ys)
    print("Pearson r (curve jaw vs lip score): %.3f" % (cov / (vx * vy) ** 0.5 if vx > 0 and vy > 0 else float("nan")))
    w, h = crops[0][2].size
    cols = 6
    rws = (len(crops) + cols - 1) // cols
    sheet = Image.new("L", (cols * w, rws * (h + 14)), 0)
    draw = ImageDraw.Draw(sheet)
    for i, (idx, t, crop) in enumerate(crops):
        x, y = (i % cols) * w, (i // cols) * (h + 14)
        sheet.paste(crop, (x, y + 14))
        draw.text((x + 2, y + 1), "#%d t=%.2f jaw=%.2f" % (idx, t, curve(t)), fill=255)
    sheet.save(work / "montage.png")
    ew, eh = eye_crops[0][2].size
    esheet = Image.new("L", (6 * ew, ((len(eye_crops) + 5) // 6) * (eh + 14)), 0)
    edraw = ImageDraw.Draw(esheet)
    for i, (idx, blink, eyes) in enumerate(eye_crops):
        x, y = (i % 6) * ew, (i // 6) * (eh + 14)
        esheet.paste(eyes, (x, y + 14))
        edraw.text((x + 2, y + 1), "#%d blink=%.2f" % (idx, blink), fill=255)
    esheet.save(work / "montage_eyes.png")
    print("wrote", work / "montage.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
