"""Contact sheet of the legs from the screenshots of a SHOTS=1 run (task 0107).

Usage: python -I sheet.py <screenshots folder> <out.png> [columns] [first] [last]
The camera of the scenario follows the actor from the side at foot height, so the legs stay in the same place of every
picture and the ground texture moves past: a foot that slides over the ground slides against the floor pattern. Each
cell is a crop of the legs with the file number; the vertical ticks mark the same screen column in every cell.
"""
import glob
import os
import sys

from PIL import Image, ImageDraw

CROP = (330, 150, 690, 390)  # left, top, right, bottom of the legs in a 960x540 screenshot


def main(argv):
    files = sorted(glob.glob(os.path.join(argv[1], 'screenshot*.png')))
    cols = int(argv[3]) if len(argv) > 3 else 6
    first = int(argv[4]) if len(argv) > 4 else 0
    last = int(argv[5]) if len(argv) > 5 else len(files)
    files = files[first:last]
    w, h = CROP[2] - CROP[0], CROP[3] - CROP[1]
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * w, rows * h), (0, 0, 0))
    for i, f in enumerate(files):
        im = Image.open(f).convert('RGB').crop(CROP)
        d = ImageDraw.Draw(im)
        d.text((4, 4), os.path.basename(f)[10:13], fill=(255, 255, 0))
        for x in (w // 4, w // 2, 3 * w // 4):
            d.line([(x, h - 14), (x, h - 1)], fill=(255, 0, 0))
        sheet.paste(im, ((i % cols) * w, (i // cols) * h))
    sheet.save(argv[2])
    print('wrote', argv[2], len(files), 'cells')


if __name__ == '__main__':
    main(sys.argv)
