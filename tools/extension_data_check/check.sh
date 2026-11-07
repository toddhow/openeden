#!/bin/bash
# Test games open off-screen, out of the taskbar and without taking focus (apps/openmw/vsgengine.cpp); run with OPENEDEN_QUIET_WINDOW=0 to see the window.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# Checks an extension's data services in the real game (task 0016): data files through the game's VFS, settings in
# settings.cfg, marked events reaching Lua, and saved games that know nothing of the extension (decision 0005). No unit test runs the save and Lua code.
#
# Usage: tools/extension_data_check/check.sh <build dir> <example suite> <empty work dir>
# On Windows, PATH must already find the game's runtime libraries.
#
# Three runs share one user data folder:
#   1. extension enabled: new game, save "a", load it. The save holds nothing of the extension.
#   2. no extension: load "a", save "b". It loads with no warning about the extension.
#   3. extension enabled: load "b".

set -u
# pwd -W gives a path Windows programs can read; elsewhere there is no such option.
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
SUITE=$2
WORK=$3
HERE=$(cd "$(dirname "$0")" && pwd)
ID=openeden_test_extension_c
FAILED=0

[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "FAIL: $WORK is not empty"; exit 1; }

check() { # description, pattern, file
    if grep -qF -- "$2" "$3"; then
        echo "ok: $1"
    else
        echo "FAIL: $1: no \"$2\" in $3"
        FAILED=1
    fi
}
check_not() {
    if grep -qF -- "$2" "$3"; then
        echo "FAIL: $1: found \"$2\" in $3"
        FAILED=1
    else
        echo "ok: $1"
    fi
}

mkdir -p "$WORK/config" "$WORK/userdata" "$WORK/extensions/$ID/data/openeden_test" "$WORK/gamedata/openeden_test"
cp "$HERE"/data/* "$WORK/gamedata/"
find "$WORK/gamedata" -maxdepth 1 -type f -exec sed -i 's/\r$//' {} +
printf 'from the game' > "$WORK/gamedata/openeden_test/overridden.txt"
printf 'from the extension' > "$WORK/extensions/$ID/data/openeden_test/overridden.txt"
printf 'hello from extension data' > "$WORK/extensions/$ID/data/openeden_test/hello.txt"
cat > "$WORK/extensions/$ID/extension.json" <<EOF
{
  "format": "openeden.extension",
  "version": 1,
  "id": "$ID",
  "name": "Data check",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": { "major": 4, "minor": 0 },
  "toolchain": "",
  "library": { "windows": "LIBRARY", "linux": "LIBRARY" },
  "provides": [],
  "requires": [],
  "optional": [],
  "loadAfter": [],
  "data": "data",
  "settings": [
    { "key": "answer", "type": "int", "default": 1 },
    { "key": "broken", "type": "int", "default": 5 },
    { "key": "changed", "type": "int", "default": 0 },
    { "key": "untouched", "type": "bool", "default": false }
  ]
}
EOF
LIBRARY=$(find "$BUILD" -name "openeden_test_extension_c*.dll" -o -name "libopeneden_test_extension_c*.so" | head -1)
[ -n "$LIBRARY" ] || { echo "FAIL: test extension library not found in $BUILD"; exit 1; }
cp "$LIBRARY" "$WORK/extensions/$ID/"
sed -i "s/LIBRARY/$(basename "$LIBRARY")/g" "$WORK/extensions/$ID/extension.json"

cp "$SUITE/settings.cfg" "$WORK/config/settings.cfg"
printf '[Video]\nresolution x = 640\nresolution y = 480\nframerate limit = 60\n[extension.%s]\nanswer = 42\nbroken = abc\n' \
    "$ID" >> "$WORK/config/settings.cfg"
cat > "$WORK/config/openmw.cfg" <<EOF
resources="$BUILD/resources"
data="$SUITE/game_template/data"
data="$WORK/gamedata"
user-data="$WORK/userdata"
content=template.omwgame
content=extension_data_check.omwscripts
EOF
# Extensions in the build folder are found first and would hide this copy.
[ -e "$BUILD/extensions/$ID" ] && { echo "FAIL: remove $BUILD/extensions/$ID first"; exit 1; }
cp -r "$WORK/extensions" "$WORK/userdata/extensions"

run() { # name, extra arguments
    local name=$1
    shift
    OPENEDEN_TEST_EXTENSION_LOG="$WORK/$name.txt" OPENEDEN_TEST_EXTENSION_DATA=1 timeout 200 "$BUILD/openmw" \
        --replace=config --config="$WORK/config" --no-grab --no-sound "$@" > "$WORK/$name.out" 2>&1
    local status=$?
    echo "run $name: exit $status"
    [ "$status" -eq 0 ] || FAILED=1
}

run first --extension=$ID
check "the extension reads its data file through the game's VFS" "vfs hello from extension data" "$WORK/first.txt"
check "the extension's file wins over the game's" "vfs-override from the extension" "$WORK/first.txt"
check "the override is logged" "overrides game data file openeden_test/overridden.txt" "$WORK/first.out"
check "the user's setting is read" "setting answer 42" "$WORK/first.txt"
check "a wrong-typed setting falls back" "setting broken 5" "$WORK/first.txt"
check "a wrong-typed setting warns" "is not a int; using the default" "$WORK/first.out"
check "a changed setting is written" "changed = 7" "$WORK/config/settings.cfg"
check_not "an unchanged setting is not written" "untouched" "$WORK/config/settings.cfg"
# The extension's ID is in every forwarded event's name, so this also catches an event waiting in Lua when the save was made.
if ls "$WORK"/userdata/saves/*/a.omwsave > /dev/null 2>&1; then
    grep -qaF "$ID" "$WORK"/userdata/saves/*/a.omwsave && { echo "FAIL: the save names the extension"; FAILED=1; } \
        || echo "ok: the save names no extension"
else
    echo "FAIL: run 1 saved nothing"
    FAILED=1
fi
check "the first event reaches Lua with its payload" "EXTENSION_EVENT 1 0.5 true first" "$WORK/first.out"
check "the second event follows" "EXTENSION_EVENT 2 0.5 true second" "$WORK/first.out"
check "an unconvertible event is dropped with a warning" "dropped: field 'text' is not valid UTF-8" "$WORK/first.out"
check_not "an unmarked event never reaches Lua" "EXTENSION_UNMARKED" "$WORK/first.out"

run second
check_not "without the extension nothing about it is warned" "Ignoring unknown record" "$WORK/second.out"
[ ! -f "$WORK/second.txt" ] && echo "ok: the extension did not load" || { echo "FAIL: the extension loaded"; FAILED=1; }

ls "$WORK"/userdata/saves/*/b.omwsave > /dev/null 2>&1 && echo "ok: run 2 saved b" || { echo "FAIL: run 2 saved nothing"; FAILED=1; }

run third --extension=$ID
check_not "a save made without the extension loads with no warning about it" "Ignoring unknown record" "$WORK/third.out"
check "run 3 did load that save" "Loading saved game 'b'" "$WORK/third.out"
check "the extension still reads its setting after that load" "setting answer 42" "$WORK/third.txt"

[ "$FAILED" -eq 0 ] && echo "ALL OK" || echo "FAILED"
exit $FAILED
