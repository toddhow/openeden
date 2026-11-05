#!/bin/bash
# Read-only include counter: for each source directory given, counts #include lines by the top-level
# directory they point at (components/X, apps/openmw/mwX, other apps, std/third-party).
# usage: includecount.sh dir...   (run from the repository root)
for d in "$@"; do
  echo "== $d"
  find "$d" \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 | xargs -0 grep -h '^[[:space:]]*#[[:space:]]*include' 2>/dev/null |
  sed -E 's/^[[:space:]]*#[[:space:]]*include[[:space:]]*//' |
  awk -v self="$d" '
  {
    line=$0; t="";
    if (match(line,/[<"]components\/[a-z0-9_]+/)) { t=substr(line,RSTART+1,RLENGTH-1); }
    else if (match(line,/[<"](\.\.\/)*mw[a-z]+\//)) { s=substr(line,RSTART+1,RLENGTH-1); gsub(/\.\.\//,"",s); sub(/\/$/,"",s); t="openmw/" s; }
    else if (match(line,/[<"]apps\/[a-z0-9_]+/)) { t=substr(line,RSTART+1,RLENGTH-1); }
    else if (line ~ /^<(vsg|vsgXchange|MyGUI|Qt|QtCore)/ || line ~ /^<(vsg|MyGUI|Q)[A-Za-z]*\//) { t="3rd:" gensub(/^<([A-Za-z]+).*/,"\1","g",line) }
    else if (line ~ /^<[a-z_]+>$/ || line ~ /^<[a-z_]+\.h>$/) { t="std"; }
    else if (line ~ /^</) { t="3rd-other"; }
    else { t="local"; }
    c[t]++; n++
  }
  END { for (k in c) printf "%6d %s\n", c[k], k }' | sort -rn | head -${TOP:-14}
done
