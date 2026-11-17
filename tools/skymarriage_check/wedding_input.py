# tools/skymarriage_check (task 0276): writes the real input script (in_<name>.txt, in the current folder) and prints the PLAN for check.sh: the courting chain of task 0270, the 24 hour wait in the Temple of Mara, the vows, the rings, the spouse and a second wait.
#   python3 wedding_input.py <first talk frame, 3000> <name> 99999 <buy click y, 467>
import sys
# usage: gen.py T name  -> writes in_<name>.txt and prints PLAN; steps from marriage flow, stage selects how far
T=int(sys.argv[1]); name=sys.argv[2]; upto=int(sys.argv[3]); GB=int(sys.argv[4]) if len(sys.argv)>4 else 450
steps=[ # (offset, kind, args)
 (0,'talk'),(100,'click',643,341),(300,'click',643,467),
 (600,'talk'),(700,'click',643,341),(1000,'click',643,366),(1300,'click',643,499),
 (1600,'talk'),(1900,'click',643,341),(2200,'shot'),(2400,'click',643,GB),(2700,'key','i'),(2900,'click',369,100),(3100,'shot'),(3200,'key','i'),(3500,'cell'),(4200,'talk2'),(4400,'shot2'),(4500,'click',643,341),(4800,'shot2'),(4900,'click',529,396),(5100,'shot2'),(5250,'click',639,396),(5900,'key','j'),(6200,'shot2'),(6300,'key','j'),(6500,'cell2'),(7400,'talk3'),(7600,'shot2'),(7700,'click',643,341),(7900,'shot2'),(8100,'click',643,467),(8700,'key','j'),(8900,'shot2')]
WAIT=[(9000,'key','j'),(9300,'key','t'),(9340,'shot2')]+[(9360+8*i,'click',291,87) for i in range(23)]+[(9600,'shot2'),(9640,'click',62,119)]+[(9700+300*i,'shot2') for i in range(0,40)]
VOWS=[(13100,'click',643,366),(13500,'click',639,420),(13700,'key','i'),(13800,'shot2'),(13850,'key','i'),(14100,'talk2'),(14300,'shot2'),(14500,'shot2'),(14700,'click',643,460),(14900,'key','t'),(14940,'shot2')]+[(14960+8*i,'click',291,87) for i in range(23)]+[(15200,'shot2'),(15240,'click',62,119),(15350,'key','t')]+[(15400+8*i,'click',291,87) for i in range(23)]+[(15620,'click',62,119),(15700,'shot2'),(16000,'talk2'),(16200,'shot2'),(16300,'click',643,443),(16500,'shot2'),(16800,'shot2'),(17100,'shot2')]
steps+=WAIT+VOWS
extra=[]
inp=[];plan=["{ 200, 'go', -1600, -23, 70, 0.52 }"]
for s in steps:
    o=s[0]
    if o>upto: break
    if s[1]=='talk': plan.append("{ %d, 'talk' }"%(T+o))
    elif s[1]=='click':
        f=T+o;x,y=s[2],s[3]
        inp+= [f"{f} mouse {x} {y}",f"{f+4} down L {x} {y}",f"{f+6} up L {x} {y}"]
    elif s[1]=='cell': plan.append("{ %d, 'cell', 'WhiterunYsoldasHouse', 170, -80, 74, -1.57 }"%(T+o))
    elif s[1]=='cell2': plan.append("{ %d, 'cell', 'RiftenTempleofMara', -1600, -23, 70, 0.52 }"%(T+o))
    elif s[1]=='talk3': plan.append("{ %d, 'talk', 'skyrimse:b78683' }"%(T+o))
    elif s[1]=='talk2': plan.append("{ %d, 'talk', 'skyrimse:b80811' }"%(T+o))
    elif s[1]=='shot2': inp.append(f"{T+o} shot")
    elif s[1]=='key': inp+= [f"{T+o} key {s[2]}",f"{T+o+3} key {s[2]} up"]
    elif s[1]=='shot': inp.append(f"{T+o} shot")
inp.sort(key=lambda l:int(l.split()[0]))
open(f"in_{name}.txt","w").write("\n".join(inp)+"\n")
print(", ".join(plan))
