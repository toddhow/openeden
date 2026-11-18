# tools/skybards_check (task 0296): writes the real input script of the first leg (in_<name>.txt in the current folder) and prints the
# PLAN for check.sh: talk to Viarmo (SolitudeBardsCollege), "I'm looking to apply to the college.", "What do you need me to do?",
# "And that's where I come in?" (MS05 stage 75), Goodbye, the world map (M) and the journal (J), screenshots between.
#   python3 gen_input.py <first talk frame, 2500> <name>
import sys
T = int(sys.argv[1]); name = sys.argv[2]
def click(f, x, y): return [f"{f} mouse {x} {y}", f"{f+4} down L {x} {y}", f"{f+6} up L {x} {y}"]
o = T - 2500
L = []
L += click(o + 2800, 643, 324)
L += click(o + 3300, 592, 420)
L += click(o + 3900, 592, 428)
L += [f"{o+4400} shot"]
L += click(o + 4600, 643, 467)
L += [f"{o+5200} key m", f"{o+5203} key m up", f"{o+5800} shot", f"{o+5900} key m", f"{o+5903} key m up",
      f"{o+6200} key j", f"{o+6203} key j up", f"{o+6700} shot"]
open(f"in_{name}.txt", "w").write("\n".join(L) + "\n")
print(f"{{ {T}, 'talk' }}")
