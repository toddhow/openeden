#!/usr/bin/env bash
# Eden Archive command-line tools: end-to-end check (task 0052, stage d).
#
#   check.sh <edenpack> <edenunpack> <edenlist> <edenvalidate> <workdir>
#
# Runs the four tools on a small generated folder and prints one "PASS: ..." or "FAIL: ..." line per check. Exits 0
# when every check passed, 1 when one failed, 2 on bad usage. The script works in <workdir>/check, which it deletes
# first and leaves in place afterwards so that a failure can be looked at.
#
# The contract of the tools (written for the implementer; the checks follow it exactly):
#   edenpack [--store|--optimise] [--level N] [--chunk-size N] [--threads N] [--derived --generation N
#            --source-digest HEX64] [--force] [--normalize-names] <input-folder> <output.edenpak>
#   edenunpack [--overwrite] <pack> <output-folder>        edenunpack --list <pack>
#   edenlist <pack>        one line per entry, in entry order: "<size> <64 hex content hash> <path>"
#   edenvalidate [--non-canonical] [--no-content] [--budget BYTES] <pack>
#            prints OK and exits 0, or one finding per line "<file>: E_CLASS at offset 0xHEX, field NAME: message", exit 1
#   Exit codes: 0 success or valid, 1 invalid pack or failed read or refused operation, 2 usage or I/O error.
#
# Only "\n" is used; text read back from the tools has any "\r" removed first (a Windows checkout may convert).

set -u
export LC_ALL=C

if [ "$#" -ne 5 ]; then
    echo "usage: check.sh <edenpack> <edenunpack> <edenlist> <edenvalidate> <workdir>" >&2
    exit 2
fi
PACK=$1
UNPACK=$2
LIST=$3
VALIDATE=$4
BASE=$5

for tool in "$PACK" "$UNPACK" "$LIST" "$VALIDATE"; do
    if [ ! -x "$tool" ] && [ ! -x "$tool.exe" ]; then
        echo "FAIL: tool not found or not executable: $tool"
        exit 2
    fi
done

mkdir -p "$BASE" || { echo "FAIL: cannot create $BASE"; exit 2; }
BASE=$(cd "$BASE" && pwd)
WORK="$BASE/check"
rm -rf "$WORK"
mkdir -p "$WORK" || { echo "FAIL: cannot create $WORK"; exit 2; }

failures=0
pass() { echo "PASS: $1"; }
failed() { echo "FAIL: $1"; failures=$((failures + 1)); }

# run <command...>: sets RC (exit code), OUT (standard output without carriage returns), ERR (standard error).
run() {
    OUT=$("$@" 2>"$WORK/stderr.txt")
    RC=$?
    OUT=$(printf '%s' "$OUT" | tr -d '\r')
    ERR=$(tr -d '\r' <"$WORK/stderr.txt")
}

# expect_rc <description> <wanted exit code> <command...>
expect_rc() {
    local description=$1 wanted=$2
    shift 2
    run "$@"
    if [ "$RC" -eq "$wanted" ]; then pass "$description"; else failed "$description (exit code $RC, wanted $wanted)"; fi
}

# expect_nonzero <description> <command...>: any failure exit code, 1 or 2
expect_nonzero() {
    local description=$1
    shift
    run "$@"
    if [ "$RC" -eq 1 ] || [ "$RC" -eq 2 ]; then pass "$description"; else failed "$description (exit code $RC)"; fi
}

same_bytes() { cmp -s "$1" "$2"; }

# ---------------------------------------------------------------------------------------------------------------
# A small folder tree: text, binary-ish bytes, an empty file, a 300 KiB file of pseudo-random digits, a file that
# compresses very well, a file twice, nested folders, names with a space and a leading dot.
# ---------------------------------------------------------------------------------------------------------------
IN="$WORK/in"
mkdir -p "$IN/dir/sub" "$IN/dup" "$IN/a b"
{
    i=1
    while [ "$i" -le 40 ]; do
        echo "line $i of the first text file, long enough to be more than a few bytes"
        i=$((i + 1))
    done
} >"$IN/a.txt"
printf '\000\001\002\377\376\200abc\n\000\000end' >"$IN/dir/b.bin"
printf 'nested file\n' >"$IN/dir/sub/c.txt"
: >"$IN/empty.txt"
awk 'BEGIN { x = 12345; for (i = 0; i < 40000; i++) { x = (x * 1103515245 + 12345) % 2147483648; printf "%d\n", x } }' |
    head -c 307200 >"$IN/big.dat"
{
    i=0
    while [ "$i" -lt 3000 ]; do
        echo "compressible compressible compressible compressible"
        i=$((i + 1))
    done
} >"$IN/dir/repeat.txt"
cp "$IN/a.txt" "$IN/dup/a_copy.txt"
printf 'space in the folder name\n' >"$IN/a b/with space.txt"
printf 'hidden\n' >"$IN/dir/.hidden"

EXPECTED_NAMES=$(cd "$IN" && find . -type f | sed 's|^\./||' | sort)
EXPECTED_COUNT=$(printf '%s\n' "$EXPECTED_NAMES" | wc -l | tr -d ' ')

tree_state() { (cd "$1" && find . -type f -printf '%p %s %T@\n' | sort); }
tree_sums() { (cd "$1" && find . -type f -exec cksum {} + | sort); }

STATE_BEFORE=$(tree_state "$IN")
SUMS_BEFORE=$(tree_sums "$IN")

# ---------------------------------------------------------------------------------------------------------------
# Packing is deterministic
# ---------------------------------------------------------------------------------------------------------------
expect_rc "pack with 1 thread" 0 "$PACK" --threads 1 "$IN" "$WORK/t1.edenpak"
expect_rc "pack with 4 threads" 0 "$PACK" --threads 4 "$IN" "$WORK/t4.edenpak"
if same_bytes "$WORK/t1.edenpak" "$WORK/t4.edenpak"; then pass "1 and 4 threads give identical bytes"; else failed "1 and 4 threads give different bytes"; fi

expect_rc "pack with the default thread count" 0 "$PACK" "$IN" "$WORK/tdefault.edenpak"
if same_bytes "$WORK/t1.edenpak" "$WORK/tdefault.edenpak"; then pass "default mode equals --store with 1 thread"; else failed "default pack differs from the --store pack"; fi
expect_rc "pack with --store" 0 "$PACK" --store "$IN" "$WORK/tstore.edenpak"
if same_bytes "$WORK/t1.edenpak" "$WORK/tstore.edenpak"; then pass "--store equals the default"; else failed "--store differs from the default"; fi

# The same files, created in another order and with other times: a copy made in reverse order, then touched.
IN2="$WORK/in2"
mkdir -p "$IN2"
for name in $(printf '%s\n' "$EXPECTED_NAMES" | sort -r | tr ' ' '#'); do
    real=$(printf '%s' "$name" | tr '#' ' ')
    mkdir -p "$IN2/$(dirname "$real")"
    cp "$IN/$real" "$IN2/$real"
done
for name in $(printf '%s\n' "$EXPECTED_NAMES" | tr ' ' '#'); do
    real=$(printf '%s' "$name" | tr '#' ' ')
    touch "$IN2/$real"
done
expect_rc "pack a copy made and touched in another order" 0 "$PACK" --threads 3 "$IN2" "$WORK/t2.edenpak"
if same_bytes "$WORK/t1.edenpak" "$WORK/t2.edenpak"; then pass "input order and times do not change the bytes"; else failed "input order or times changed the bytes"; fi

# ---------------------------------------------------------------------------------------------------------------
# Validation of store and optimise packs, and of other chunk sizes
# ---------------------------------------------------------------------------------------------------------------
expect_rc "pack with --optimise" 0 "$PACK" --optimise --threads 2 "$IN" "$WORK/opt.edenpak"
expect_rc "pack with --optimise --level 9 and 4 threads" 0 "$PACK" --optimise --level 9 --threads 4 "$IN" "$WORK/opt9.edenpak"
expect_rc "pack with --chunk-size 4096" 0 "$PACK" --chunk-size 4096 "$IN" "$WORK/c4096.edenpak"
expect_rc "pack with --optimise --chunk-size 65536" 0 "$PACK" --optimise --chunk-size 65536 "$IN" "$WORK/opt64k.edenpak"
opt_size=$(wc -c <"$WORK/opt.edenpak")
store_size=$(wc -c <"$WORK/t1.edenpak")
if [ "$opt_size" -lt "$store_size" ]; then pass "optimise mode makes the pack smaller on compressible input"; else failed "optimise pack ($opt_size) is not smaller than the store pack ($store_size)"; fi
expect_rc "pack --optimise a second time" 0 "$PACK" --optimise --threads 2 "$IN" "$WORK/opt_again.edenpak"
if same_bytes "$WORK/opt.edenpak" "$WORK/opt_again.edenpak"; then pass "the same optimise command gives the same bytes"; else failed "the same optimise command gave different bytes"; fi
expect_rc "optimise with 1 thread" 0 "$PACK" --optimise --threads 1 "$IN" "$WORK/opt_one.edenpak"
if same_bytes "$WORK/opt.edenpak" "$WORK/opt_one.edenpak"; then pass "optimise output does not depend on the thread count"; else failed "optimise output depends on the thread count"; fi

for pack in t1 opt opt9 c4096 opt64k; do
    run "$VALIDATE" "$WORK/$pack.edenpak"
    if [ "$RC" -eq 0 ] && [ "$OUT" = "OK" ]; then pass "edenvalidate accepts $pack.edenpak (canonical, with content)"; else failed "edenvalidate on $pack.edenpak: exit $RC, output: $OUT"; fi
done
expect_rc "edenvalidate --non-canonical on a good pack" 0 "$VALIDATE" --non-canonical "$WORK/t1.edenpak"
expect_rc "edenvalidate --no-content on a good pack" 0 "$VALIDATE" --no-content "$WORK/opt.edenpak"
expect_rc "edenvalidate with a large budget" 0 "$VALIDATE" --budget 1073741824 "$WORK/opt.edenpak"

run "$VALIDATE" --budget 10 "$WORK/t1.edenpak"
if [ "$RC" -eq 1 ] && printf '%s\n' "$OUT" | grep -q 'E_LIMIT'; then pass "a decode budget of 10 bytes is a finding of class E_LIMIT"; else failed "budget of 10 bytes: exit $RC, output: $OUT"; fi

# ---------------------------------------------------------------------------------------------------------------
# Unpack, list
# ---------------------------------------------------------------------------------------------------------------
for pack in t1 opt c4096; do
    rm -rf "$WORK/out_$pack"
    expect_rc "unpack $pack.edenpak" 0 "$UNPACK" "$WORK/$pack.edenpak" "$WORK/out_$pack"
    if diff -r "$IN" "$WORK/out_$pack" >/dev/null 2>&1; then pass "unpacked $pack.edenpak equals the input folder"; else failed "unpacked $pack.edenpak differs from the input folder"; fi
done

run "$UNPACK" --list "$WORK/t1.edenpak"
if [ "$RC" -eq 0 ]; then
    missing=0
    while IFS= read -r name; do
        printf '%s\n' "$OUT" | grep -q -x -F -e "$name" || { missing=1; echo "  not listed: $name"; }
    done <<EOF
$EXPECTED_NAMES
EOF
    if [ "$missing" -eq 0 ]; then pass "edenunpack --list names every file"; else failed "edenunpack --list misses names"; fi
else
    failed "edenunpack --list exit code $RC"
fi

run "$LIST" "$WORK/t1.edenpak"
if [ "$RC" -ne 0 ]; then
    failed "edenlist exit code $RC"
else
    lines=$(printf '%s\n' "$OUT" | wc -l | tr -d ' ')
    if [ "$lines" -eq "$EXPECTED_COUNT" ]; then pass "edenlist prints one line per entry"; else failed "edenlist printed $lines lines, wanted $EXPECTED_COUNT"; fi
    bad=$(printf '%s\n' "$OUT" | grep -c -v -E '^[0-9]+ [0-9a-f]{64} [^ ].*$')
    if [ "$bad" -eq 0 ]; then pass "edenlist lines look like '<size> <hash> <path>'"; else failed "$bad edenlist lines have the wrong form"; fi
    list_paths=$(printf '%s\n' "$OUT" | sed -E 's/^[0-9]+ [0-9a-f]{64} //')
    if [ "$list_paths" = "$EXPECTED_NAMES" ]; then pass "edenlist is in path order (bytewise) and lists every file"; else failed "edenlist order or names differ from the sorted file names"; fi
    size_of_a=$(printf '%s\n' "$OUT" | grep ' a.txt$' | cut -d' ' -f1)
    if [ "$size_of_a" = "$(wc -c <"$IN/a.txt" | tr -d ' ')" ]; then pass "edenlist shows the uncompressed size"; else failed "edenlist size of a.txt is '$size_of_a'"; fi
    empty_hash=$(printf '%s\n' "$OUT" | grep ' empty.txt$' | cut -d' ' -f2)
    if [ "$empty_hash" = "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262" ]; then pass "the empty file has the BLAKE3 hash of zero bytes"; else failed "hash of empty.txt is '$empty_hash'"; fi
    hash_a=$(printf '%s\n' "$OUT" | grep ' a.txt$' | cut -d' ' -f2)
    hash_copy=$(printf '%s\n' "$OUT" | grep ' dup/a_copy.txt$' | cut -d' ' -f2)
    if [ -n "$hash_a" ] && [ "$hash_a" = "$hash_copy" ]; then pass "equal content gives equal hashes"; else failed "a.txt and its copy have hashes '$hash_a' and '$hash_copy'"; fi
    if [ "$hash_a" != "$empty_hash" ]; then pass "different content gives different hashes"; else failed "a.txt and empty.txt have one hash"; fi
fi

run "$LIST" "$WORK/opt.edenpak"
list_opt=$OUT
run "$LIST" "$WORK/t1.edenpak"
if [ "$list_opt" = "$OUT" ]; then pass "store and optimise packs list the same sizes and hashes"; else failed "store and optimise packs list differently"; fi

# ---------------------------------------------------------------------------------------------------------------
# Refusals: overwrite, bad names
# ---------------------------------------------------------------------------------------------------------------
cp "$WORK/t1.edenpak" "$WORK/keep.edenpak"
expect_rc "pack refuses to overwrite an existing output" 1 "$PACK" "$IN" "$WORK/t1.edenpak"
if same_bytes "$WORK/t1.edenpak" "$WORK/keep.edenpak"; then pass "a refused pack leaves the existing output unchanged"; else failed "a refused pack changed the existing output"; fi
expect_rc "pack --force replaces an existing output" 0 "$PACK" --force "$IN" "$WORK/t1.edenpak"
if same_bytes "$WORK/t1.edenpak" "$WORK/keep.edenpak"; then pass "--force gives the same deterministic bytes"; else failed "--force gave other bytes"; fi
leftovers=$(ls -A "$WORK" | grep -i -E '\.tmp|\.temp|\.part|~$' | head -1)
if [ -z "$leftovers" ]; then pass "no temporary file is left next to the output"; else failed "left over: $leftovers"; fi

expect_rc "unpack refuses to overwrite existing files" 1 "$UNPACK" "$WORK/t1.edenpak" "$WORK/out_t1"
expect_rc "unpack --overwrite replaces them" 0 "$UNPACK" --overwrite "$WORK/t1.edenpak" "$WORK/out_t1"
if diff -r "$IN" "$WORK/out_t1" >/dev/null 2>&1; then pass "the overwritten folder equals the input"; else failed "the overwritten folder differs from the input"; fi

# A file with an upper-case name.
UP="$WORK/in_upper"
mkdir -p "$UP/Dir"
printf 'upper\n' >"$UP/Dir/Mixed.TXT"
printf 'lower\n' >"$UP/plain.txt"
expect_rc "pack rejects an upper-case file name without --normalize-names" 1 "$PACK" "$UP" "$WORK/upper.edenpak"
if [ ! -e "$WORK/upper.edenpak" ]; then pass "a rejected pack writes no output"; else failed "a rejected pack left an output file"; fi
expect_rc "pack --normalize-names lower-cases the names" 0 "$PACK" --normalize-names "$UP" "$WORK/upper_ok.edenpak"
run "$LIST" "$WORK/upper_ok.edenpak"
if printf '%s\n' "$OUT" | grep -q ' dir/mixed.txt$' && printf '%s\n' "$OUT" | grep -q ' plain.txt$'; then pass "the normalized names are stored"; else failed "normalized names not found in: $OUT"; fi
expect_rc "the normalized pack validates" 0 "$VALIDATE" "$WORK/upper_ok.edenpak"

# Two files that normalize to one name (only possible on a case-sensitive file system).
CL="$WORK/in_collide"
mkdir -p "$CL"
printf 'one\n' >"$CL/Same.txt"
printf 'two\n' >"$CL/same.txt"
if [ "$(ls "$CL" | wc -l | tr -d ' ')" -eq 2 ]; then
    expect_rc "pack --normalize-names refuses two names that normalize to one" 1 "$PACK" --normalize-names "$CL" "$WORK/collide.edenpak"
    if [ ! -e "$WORK/collide.edenpak" ]; then pass "a name collision writes no output"; else failed "a name collision left an output file"; fi
else
    echo "SKIP: this file system cannot hold Same.txt and same.txt together"
fi

# ---------------------------------------------------------------------------------------------------------------
# Derived packs
# ---------------------------------------------------------------------------------------------------------------
DIGEST=00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff
expect_rc "pack --derived with a generation and a source digest" 0 "$PACK" --derived --generation 3 --source-digest "$DIGEST" "$IN" "$WORK/derived.edenpak"
expect_rc "a derived pack validates" 0 "$VALIDATE" "$WORK/derived.edenpak"
if ! same_bytes "$WORK/derived.edenpak" "$WORK/t1.edenpak"; then pass "a derived pack differs from a plain one"; else failed "--derived changed nothing"; fi
derived_hex=$(od -An -tx1 -v "$WORK/derived.edenpak" | tr -d ' 
')
plain_hex=$(od -An -tx1 -v "$WORK/t1.edenpak" | tr -d ' 
')
if printf '%s' "$derived_hex" | grep -q "$DIGEST" && ! printf '%s' "$plain_hex" | grep -q "$DIGEST"; then pass "the source digest is stored in a derived pack only"; else failed "the source digest was not found in the derived pack, or was found in a plain one"; fi
expect_rc "a bad source digest is a usage error" 2 "$PACK" --derived --generation 3 --source-digest zz "$IN" "$WORK/bad_digest.edenpak"
if [ ! -e "$WORK/bad_digest.edenpak" ]; then pass "no output for a bad source digest"; else failed "output exists for a bad source digest"; fi
expect_nonzero "a chunk size that is not a power of two is refused" "$PACK" --chunk-size 5000 "$IN" "$WORK/bad_chunk.edenpak"
if [ ! -e "$WORK/bad_chunk.edenpak" ]; then pass "no output for a bad chunk size"; else failed "output exists for a bad chunk size"; fi

# ---------------------------------------------------------------------------------------------------------------
# Damaged packs
# ---------------------------------------------------------------------------------------------------------------
# Byte 4100 is inside the first entry's data ("a b/with space.txt" comes first in path order and is longer than 4
# bytes; the first data block starts at 4096); change it.
cp "$WORK/t1.edenpak" "$WORK/corrupt.edenpak"
printf '\377' | dd of="$WORK/corrupt.edenpak" bs=1 seek=4100 conv=notrunc 2>/dev/null
if ! same_bytes "$WORK/t1.edenpak" "$WORK/corrupt.edenpak"; then pass "(made a corrupted copy)"; else failed "the corrupted copy is identical to the original"; fi
run "$VALIDATE" "$WORK/corrupt.edenpak"
if [ "$RC" -eq 1 ]; then pass "edenvalidate rejects a pack with a changed content byte (exit 1)"; else failed "corrupt content: exit $RC"; fi
if printf '%s\n' "$OUT" | grep -q 'E_HASH'; then pass "the finding is E_HASH"; else failed "no E_HASH finding in: $OUT"; fi
if printf '%s\n' "$OUT" | grep -q -E ': E_[A-Z]+ at offset 0x[0-9a-fA-F]+, field [^:]+: '; then pass "the finding has the form '<file>: E_CLASS at offset 0xHEX, field NAME: message'"; else failed "finding format: $OUT"; fi
if printf '%s\n' "$OUT" | grep -q 'corrupt.edenpak'; then pass "the finding names the file"; else failed "the finding does not name the file: $OUT"; fi
expect_rc "edenvalidate --no-content does not read the content and accepts the structure" 0 "$VALIDATE" --no-content "$WORK/corrupt.edenpak"
rm -rf "$WORK/out_corrupt"
expect_rc "edenunpack fails on a corrupted chunk" 1 "$UNPACK" "$WORK/corrupt.edenpak" "$WORK/out_corrupt"
if [ ! -e "$WORK/out_corrupt/a b/with space.txt" ]; then pass "the damaged file is not left in the output folder"; else failed "the damaged file exists in the output folder after a failed read"; fi

cp "$WORK/t1.edenpak" "$WORK/badheader.edenpak"
printf '\125' | dd of="$WORK/badheader.edenpak" bs=1 seek=400 conv=notrunc 2>/dev/null
run "$VALIDATE" "$WORK/badheader.edenpak"
if [ "$RC" -eq 1 ] && printf '%s\n' "$OUT" | grep -q 'E_CHECKSUM'; then pass "a changed header byte is E_CHECKSUM"; else failed "bad header: exit $RC, output: $OUT"; fi
expect_rc "edenlist refuses a pack with a bad header" 1 "$LIST" "$WORK/badheader.edenpak"
expect_rc "edenunpack --list refuses a pack with a bad header" 1 "$UNPACK" --list "$WORK/badheader.edenpak"

head -c 5000 "$WORK/t1.edenpak" >"$WORK/cut.edenpak"
run "$VALIDATE" "$WORK/cut.edenpak"
if [ "$RC" -eq 1 ] && printf '%s\n' "$OUT" | grep -q 'E_TRUNCATED'; then pass "a cut file is E_TRUNCATED"; else failed "cut file: exit $RC, output: $OUT"; fi
printf 'this is not a pack at all, only text\n' >"$WORK/garbage.edenpak"
run "$VALIDATE" "$WORK/garbage.edenpak"
if [ "$RC" -eq 1 ] && printf '%s\n' "$OUT" | grep -q 'E_MAGIC'; then pass "a text file is E_MAGIC"; else failed "garbage: exit $RC, output: $OUT"; fi

# ---------------------------------------------------------------------------------------------------------------
# Usage and input/output errors are exit code 2
# ---------------------------------------------------------------------------------------------------------------
expect_rc "edenpack without arguments" 2 "$PACK"
expect_rc "edenpack with an unknown option" 2 "$PACK" --no-such-option "$IN" "$WORK/x.edenpak"
expect_rc "edenpack with a missing output argument" 2 "$PACK" "$IN"
expect_rc "edenpack with a missing option value" 2 "$PACK" --threads
expect_rc "edenpack with a non-numeric thread count" 2 "$PACK" --threads many "$IN" "$WORK/x.edenpak"
expect_rc "edenpack on a folder that does not exist" 2 "$PACK" "$WORK/no_such_folder" "$WORK/x.edenpak"
expect_rc "edenunpack without arguments" 2 "$UNPACK"
expect_rc "edenunpack on a pack that does not exist" 2 "$UNPACK" "$WORK/no_such.edenpak" "$WORK/x_out"
expect_rc "edenunpack --list on a pack that does not exist" 2 "$UNPACK" --list "$WORK/no_such.edenpak"
expect_rc "edenlist without arguments" 2 "$LIST"
expect_rc "edenlist on a pack that does not exist" 2 "$LIST" "$WORK/no_such.edenpak"
expect_rc "edenvalidate without arguments" 2 "$VALIDATE"
expect_rc "edenvalidate on a pack that does not exist" 2 "$VALIDATE" "$WORK/no_such.edenpak"
expect_rc "edenvalidate with an unknown option" 2 "$VALIDATE" --no-such-option "$WORK/t1.edenpak"
expect_rc "edenvalidate --budget without a number" 2 "$VALIDATE" --budget
if [ ! -e "$WORK/x.edenpak" ] && [ ! -e "$WORK/x_out" ]; then pass "usage errors create nothing"; else failed "a usage error created x.edenpak or x_out"; fi

# ---------------------------------------------------------------------------------------------------------------
# The input folder was only read
# ---------------------------------------------------------------------------------------------------------------
STATE_AFTER=$(tree_state "$IN")
SUMS_AFTER=$(tree_sums "$IN")
if [ "$STATE_BEFORE" = "$STATE_AFTER" ] && [ "$SUMS_BEFORE" = "$SUMS_AFTER" ]; then pass "the input folder is unchanged (names, sizes, times, checksums)"; else failed "the input folder changed"; fi

echo
if [ "$failures" -eq 0 ]; then
    echo "PASS: all checks passed"
    exit 0
fi
echo "FAIL: $failures check(s) failed"
exit 1
