import sys
# usage: gen.py T name  -> writes in_<name>.txt and prints PLAN; steps from marriage flow, stage selects how far
T=int(sys.argv[1]); name=sys.argv[2]; upto=int(sys.argv[3]); GB=int(sys.argv[4]) if len(sys.argv)>4 else 450
steps=[ # (offset, kind, args)
 (0,'talk'),(100,'click',643,341),(300,'click',643,467),
 (600,'talk'),(700,'click',643,341),(1000,'click',643,366),(1300,'click',643,499),
 (1600,'talk'),(1900,'click',643,341),(2200,'shot'),(2400,'click',643,GB),(2700,'key','i'),(2900,'click',369,100),(3100,'shot'),(3200,'key','i'),(3500,'cell'),(4200,'talk2'),(4400,'shot2'),(4500,'click',643,341),(4800,'shot2'),(4900,'click',529,396),(5100,'shot2'),(5250,'click',639,396),(5900,'key','j'),(6200,'shot2'),(6300,'key','j'),(6500,'cell2'),(7400,'talk3'),(7600,'shot2'),(7700,'click',643,341),(7900,'shot2'),(8100,'click',643,467),(8700,'key','j'),(8900,'shot2')]
extra=[]
inp=[];plan=["{ 200, 'go', -1600, -23, 70, 1.57 }"]
for s in steps:
    o=s[0]
    if o>upto: break
    if s[1]=='talk': plan.append("{ %d, 'talk' }"%(T+o))
    elif s[1]=='click':
        f=T+o;x,y=s[2],s[3]
        inp+= [f"{f} mouse {x} {y}",f"{f+4} down L {x} {y}",f"{f+6} up L {x} {y}"]
    elif s[1]=='cell': plan.append("{ %d, 'cell', 'WhiterunYsoldasHouse', 170, -80, 74, -1.57 }"%(T+o))
    elif s[1]=='cell2': plan.append("{ %d, 'cell', 'RiftenTempleofMara', -1600, -23, 70, 1.57 }"%(T+o))
    elif s[1]=='talk3': plan.append("{ %d, 'talk', 'skyrimse:b78683' }"%(T+o))
    elif s[1]=='talk2': plan.append("{ %d, 'talk', 'skyrimse:b80811' }"%(T+o))
    elif s[1]=='shot2': inp.append(f"{T+o} shot")
    elif s[1]=='key': inp+= [f"{T+o} key {s[2]}",f"{T+o+3} key {s[2]} up"]
    elif s[1]=='shot': inp.append(f"{T+o} shot")
open(f"in_{name}.txt","w").write("\n".join(inp)+"\n")
print(", ".join(plan))
