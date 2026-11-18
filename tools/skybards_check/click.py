import sys
# usage: click.py frame x y  -> input script lines
f,x,y=int(sys.argv[1]),sys.argv[2],sys.argv[3]
print(f"{f} mouse {x} {y}\n{f+4} down L {x} {y}\n{f+6} up L {x} {y}")
