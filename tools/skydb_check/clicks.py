# usage: clicks.py out.txt "frame:x:y" ... | "frame:shot" | "frame:key:j"  -> input script for OPENEDEN_RMLUI_INPUT_SCRIPT
import sys
l=[]
for a in sys.argv[2:]:
    p=a.split(':'); f=int(p[0])
    if p[1]=='shot': l.append(f"{f} shot")
    elif p[1]=='key': l+= [f"{f} key {p[2]}", f"{f+3} key {p[2]} up"]
    else:
        x,y=p[1],p[2]; l+=[f"{f} mouse {x} {y}",f"{f+4} down L {x} {y}",f"{f+6} up L {x} {y}"]
l.sort(key=lambda s:int(s.split()[0]))
open(sys.argv[1],'w').write("\n".join(l)+"\n")
