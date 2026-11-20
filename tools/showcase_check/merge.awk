# merge.awk: merge an overrides file (INI) into a base settings file (INI). usage: awk -f merge.awk overrides.cfg base.cfg
# A key already in the base section is replaced; a key whose override value is __DEL__ is dropped; new keys are added at the end of
# their section; sections the base lacks are appended. Each key ends up once.
function flush(sec,   k) {
    for (k in ov) { split(k, p, SUBSEP); if (p[1] == sec && !(k in done)) { if (ov[k] != "__DEL__") print p[2] " = " ov[k]; done[k] = 1 } }
}
FNR == NR {
    if ($0 ~ /^\[/) { osec = $0; next }
    if ($0 ~ /^[ \t]*#/ || $0 ~ /^[ \t]*$/) next
    i = index($0, "="); if (i == 0) next
    key = substr($0, 1, i - 1); gsub(/^[ \t]+|[ \t]+$/, "", key)
    val = substr($0, i + 1); gsub(/^[ \t]+|[ \t]+$/, "", val)
    ov[osec, key] = val; secs[osec] = 1; next
}
/^\[/ { if (cur != "") flush(cur); cur = $0; seen[cur] = 1; print; next }
{
    if ($0 ~ /^[ \t]*#/ || $0 ~ /^[ \t]*$/) { print; next }
    i = index($0, "="); if (i == 0) { print; next }
    key = substr($0, 1, i - 1); gsub(/^[ \t]+|[ \t]+$/, "", key)
    if ((cur, key) in ov) { if (!((cur, key) in done)) { if (ov[cur, key] != "__DEL__") print key " = " ov[cur, key]; done[cur, key] = 1 } ; next }
    print
}
END {
    if (cur != "") flush(cur)
    for (s in secs) if (!(s in seen)) { print s; flush(s) }
}
