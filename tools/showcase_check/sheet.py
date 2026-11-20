# sheet.py <out.png> <cols> <cell width> <image> [<image> ...]: a contact sheet (grid) of pictures, each labelled with its file name.
import sys
from PIL import Image, ImageDraw

out, cols, cw = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
files = sys.argv[4:]
imgs = []
for f in files:
    im = Image.open(f).convert("RGB")
    ch = int(im.height * cw / im.width)
    imgs.append((f, im.resize((cw, ch), Image.LANCZOS)))
if not imgs:
    sys.exit("no images")
ch = max(i.height for _, i in imgs)
rows = (len(imgs) + cols - 1) // cols
sheet = Image.new("RGB", (cols * cw, rows * (ch + 14)), (20, 20, 20))
d = ImageDraw.Draw(sheet)
for n, (f, im) in enumerate(imgs):
    x, y = (n % cols) * cw, (n // cols) * (ch + 14)
    sheet.paste(im, (x, y + 14))
    d.text((x + 3, y + 1), f.replace("\\", "/").split("/")[-1][:60], fill=(255, 255, 255))
sheet.save(out)
print(out, sheet.size)
