# Prints every "key = value" line of the cfg files given as sec<TAB>key<TAB>value (one per line), in file order.
# The one parser of tools/testprofile: prepare.sh, coverage.sh and check.sh all read settings files through it.
# Comment lines (# or ;) and blank lines are skipped. Values are not split on '#': settings files have no inline comments.
{ line = $0; sub(/\r$/, "", line) }
FNR == 1 { sec = "" }
line ~ /^[ \t]*[#;]/ { next }
line ~ /^[ \t]*\[/ { sec = line; sub(/^[ \t]*\[/, "", sec); sub(/\][ \t]*$/, "", sec); next }
line ~ /=/ {
    k = line; sub(/[ \t]*=.*/, "", k); sub(/^[ \t]+/, "", k)
    v = line; sub(/^[^=]*=[ \t]*/, "", v); sub(/[ \t]+$/, "", v)
    if (k != "") printf "%s\t%s\t%s\n", sec, k, v
}
