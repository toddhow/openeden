import sys
# mkin.py out  y0,y1,...  : first 2600..4000 goodbye clicks, then one click per 800 frames at the given y values (0 = no click), shots 60 before each
ys=[int(v) for v in sys.argv[2].split(',')]
l=[]
for f in range(2600,4000,300):
    l+= [f"{f} mouse 640 389", f"{f+4} down L 640 389", f"{f+6} up L 640 389"]
t=5000
for y in ys:
    l.append(f"{t-60} shot")
    if y:
        l+=[f"{t} mouse 640 {y}", f"{t+4} down L 640 {y}", f"{t+6} up L 640 {y}"]
    t+=800
l.append(f"{t-60} shot")
open(sys.argv[1],'w').write('\n'.join(l)+'\n')
print(t)
