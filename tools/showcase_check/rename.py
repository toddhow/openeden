# rename.py <log> <screenshots dir> <out dir>: screenshots are numbered in request order; "SHOWCASE shot <idx> <name>" lines give the names.
import re, sys, os, shutil, glob
log, shots, out = sys.argv[1:4]
names = []
for line in open(log, errors="replace"):
    m = re.search(r"SHOWCASE shot (\S+)", line)
    if m: names.append(m.group(1))
files = sorted(glob.glob(os.path.join(shots, "screenshot*.png")))
print("shots requested %d, files %d" % (len(names), len(files)))
for i, f in enumerate(files):
    n = names[i] if i < len(names) else "extra%d" % i
    shutil.copy(f, os.path.join(out, "%03d_%s.png" % (i, n)))
