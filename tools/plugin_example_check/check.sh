#!/bin/bash
# Checks the example plugin end to end in the real game (task 0018): every line plugins/example/README.md documents,
# a save and reload, and the failure message for a broken plugin. No unit test runs these together.
#
# Usage: tools/plugin_example_check/check.sh <build dir> <example suite> <empty work dir>
# On Windows, PATH must already find the game's runtime libraries.
# With OPENEDEN_CHECK_INSTALL_DIR set, also checks the plugin layout of that installed build.
#
# Three runs share one config and user data folder, never the user's own:
#   1. new game, save "a", load it.
#   2. load "a", in a plugin that started afresh.
#   3. the same, with a broken plugin also installed and enabled: one failure message, and the example still works.

set -u
[ "$#" -eq 3 ] || { echo "usage: $0 <build dir> <example suite> <empty work dir>"; exit 1; }
# pwd -W gives a path Windows programs can read; elsewhere there is no such option.
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; }) || exit 1
SUITE=$2
WORK=$3
HERE=$(cd "$(dirname "$0")" && pwd)
PLUGIN="$BUILD/plugins/example"
BROKEN=oe_example_check_broken
MESSAGE="Showing plugin failure message"
FAILED=0

fail() {
    echo "FAIL: $1"
    FAILED=1
}

check() { # description, text, file
    if grep -qF -- "$2" "$3"; then
        echo "ok: $1"
    else
        fail "$1: no \"$2\" in $3"
    fi
}

check_regex() { # description, extended regular expression, file
    if grep -qE -- "$2" "$3"; then
        echo "ok: $1"
    else
        fail "$1: no match for \"$2\" in $3"
    fi
}

check_not() { # description, text, file
    if grep -qF -- "$2" "$3"; then
        fail "$1: found \"$2\" in $3"
    else
        echo "ok: $1"
    fi
}

check_count() { # description, expected number of lines, text, file
    local found
    found=$(grep -cF -- "$3" "$4")
    if [ "$found" -eq "$2" ]; then
        echo "ok: $1"
    else
        fail "$1: \"$3\" is on $found lines of $4, expected $2"
    fi
}

check_at_least() { # description, actual, minimum
    if [ -n "$2" ] && [ "$2" -ge "$3" ]; then
        echo "ok: $1 ($2)"
    else
        fail "$1: got \"$2\", expected at least $3"
    fi
}

last_number() { # sed pattern ending just before the number, file
    sed -n "s/.*$1\([0-9][0-9]*\).*/\1/p" "$2" | tail -1
}

# The value of games_seen in [plugin.example] only, so the same key elsewhere cannot pass.
persisted_games_seen() {
    tr -d '\r' < "$WORK/config/settings.cfg" | awk '
        /^\[/ { inSection = ($0 == "[plugin.example]") }
        inSection && /^games_seen[ ]*=/ { sub(/^[^=]*=[ ]*/, ""); value = $0 }
        END { print value }'
}

check_install() { # install folder
    local root=$1
    echo "checking the install at $root"
    for id in compat_morrowind example; do
        if [ -f "$root/plugins/$id/plugin.json" ]; then
            echo "ok: $id has its manifest"
        else
            fail "$root/plugins/$id/plugin.json is missing"
        fi
        if [ -f "$root/plugins/$id/$id.dll" ] || [ -f "$root/plugins/$id/lib$id.so" ]; then
            echo "ok: $id has its library"
        else
            fail "no $id.dll or lib$id.so in $root/plugins/$id"
        fi
    done
    if [ -d "$root/plugins/example/data" ]; then
        echo "ok: example has its data"
    else
        fail "$root/plugins/example/data is missing"
    fi
    local strays
    strays=$(find "$root" -iname '*openeden_test_plugin*' | head -5)
    if [ -z "$strays" ]; then
        echo "ok: no test plugin is installed"
    else
        fail "test plugins in the install: $strays"
    fi
}

if [ -n "${OPENEDEN_CHECK_INSTALL_DIR:-}" ]; then
    check_install "$OPENEDEN_CHECK_INSTALL_DIR"
fi

[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "FAIL: $WORK is not empty"; exit 1; }
[ -f "$PLUGIN/plugin.json" ] || { echo "FAIL: no built plugin at $PLUGIN"; exit 1; }
[ -f "$PLUGIN/example.dll" ] || [ -f "$PLUGIN/libexample.so" ] || { echo "FAIL: no library in $PLUGIN"; exit 1; }
[ -d "$PLUGIN/data" ] || { echo "FAIL: no data folder in $PLUGIN"; exit 1; }

GREETING=$(tr -d '\r' < "$PLUGIN/data/example/greeting.txt")
[ -n "$GREETING" ] || { echo "FAIL: no text in $PLUGIN/data/example/greeting.txt"; exit 1; }

mkdir -p "$WORK/config" "$WORK/userdata" "$WORK/gamedata"
cp "$HERE"/data/* "$WORK/gamedata/"
find "$WORK/gamedata" -maxdepth 1 -type f -exec sed -i 's/\r$//' {} +
cp "$SUITE/settings.cfg" "$WORK/config/settings.cfg"
printf '[Video]\nresolution x = 640\nresolution y = 480\nframerate limit = 60\n' >> "$WORK/config/settings.cfg"
cat > "$WORK/config/openmw.cfg" <<CFG
resources="$BUILD/resources"
data="$SUITE/game_template/data"
data="$WORK/gamedata"
user-data="$WORK/userdata"
content=template.omwgame
content=example_check.omwscripts
content=example.omwscripts
plugin=example
CFG

run() { # name, extra arguments
    local name=$1
    shift
    timeout 200 "$BUILD/openmw" --replace=config --config="$WORK/config" --no-grab --no-sound "$@" \
        > "$WORK/$name.out" 2>&1
    local status=$?
    echo "run $name: exit $status"
    [ "$status" -eq 0 ] || FAILED=1
    [ -s "$WORK/$name.out" ] || fail "run $name wrote no log"
}

# What every run with the plugin must log.
check_common() { # run name
    local log="$WORK/$1.out"
    check "$1: the data file is read through the VFS" "[example] greeting: $GREETING" "$log"
    check_count "$1: greeting_count has its default" 1 "[example] setting greeting_count=1" "$log"
    check_regex "$1: games_seen is logged" '\[example\] setting games_seen=[0-9]+' "$log"
    check_regex "$1: the ray is cast" '\[example\] ray hit=(yes|no)' "$log"
    check_regex "$1: the sphere is cast" '\[example\] shape hit=(yes|no)' "$log"
    check_regex "$1: an event is sent" '\[example\] event sent count=[0-9]+' "$log"
    check "$1: game loads are seen" "[example] event game loaded" "$log"
    check "$1: cell changes are seen" "[example] event cell changed" "$log"
    check_count "$1: debug drawing is logged once" 1 "[example] debug draw submitted" "$log"
    check_count "$1: the profiler scope is logged once" 1 "[example] profiler scope used" "$log"
    check_regex "$1: contacts are counted on stop" '\[example\] contacts=[0-9]+' "$log"

    local spawned freed
    spawned=$(last_number '\[example\] entities spawned=' "$log")
    freed=$(sed -n 's/.*\[example\] entities spawned=[0-9]* freed=\([0-9][0-9]*\).*/\1/p' "$log" | tail -1)
    check_at_least "$1: entities are spawned" "$spawned" 1
    if [ -n "$freed" ] && [ -n "$spawned" ] && [ "$freed" -le "$spawned" ]; then
        echo "ok: $1: freed $freed of $spawned entities"
    else
        fail "$1: freed \"$freed\" of \"$spawned\" entities"
    fi
}

# Every event Lua prints was sent by the plugin, with the greeting, and at least one arrives.
check_lua() { # run name
    local log="$WORK/$1.out"
    local lines=0 line count text
    while IFS= read -r line; do
        lines=$((lines + 1))
        count=$(printf '%s\n' "$line" | sed -n 's/.*EXAMPLE_LUA count=\([0-9][0-9]*\) text=.*/\1/p')
        text=$(printf '%s\n' "$line" | sed -n 's/.*EXAMPLE_LUA count=[0-9][0-9]* text=//p' | tr -d '\r')
        if [ -z "$count" ] || [ -z "$text" ]; then
            fail "$1: Lua line without count and text: $line"
            continue
        fi
        grep -qE "\[example\] event sent count=$count([^0-9]|$)" "$log" \
            || fail "$1: Lua got count $count, which the plugin never sent"
        # An event's text has a fixed capacity, so it may hold only the start of a long greeting.
        case "$GREETING" in
            "$text"*) ;;
            *) fail "$1: Lua got text \"$text\", not the greeting \"$GREETING\"" ;;
        esac
    done < <(grep -F 'EXAMPLE_LUA count=' "$log")
    if [ "$lines" -ge 1 ]; then
        echo "ok: $1: $lines events reached Lua"
    else
        fail "$1: no EXAMPLE_LUA line"
    fi
}

# Run 1: new game, save, load

run first
FIRST="$WORK/first.out"
check_common first
check_lua first
check "first: the save is seen" "[example] event game saved" "$FIRST"
check_not "first: no failure message without a failure" "$MESSAGE" "$FIRST"
if ls "$WORK"/userdata/saves/*/a.omwsave > /dev/null 2>&1; then
    echo "ok: run 1 saved a"
else
    fail "run 1 saved nothing"
fi

if grep -qaF 'openeden_example' "$WORK"/userdata/saves/*/a.omwsave; then
    fail "first: the save names the plugin's events"
else
    echo "ok: first: the save names none of the plugin's events"
fi

GAMES_FIRST=$(persisted_games_seen)
check_at_least "first: games_seen is persisted in [plugin.example]" "$GAMES_FIRST" 1

# Run 2: load only, in a plugin that starts afresh

run second
SECOND="$WORK/second.out"
check_common second
check_lua second
check_not "second: nothing was saved" "[example] event game saved" "$SECOND"
check_not "second: no failure message without a failure" "$MESSAGE" "$SECOND"
GAMES_SECOND=$(persisted_games_seen)
if [ -n "$GAMES_FIRST" ] && [ -n "$GAMES_SECOND" ] && [ "$GAMES_SECOND" -gt "$GAMES_FIRST" ]; then
    echo "ok: games_seen goes up across runs ($GAMES_FIRST to $GAMES_SECOND)"
else
    fail "games_seen did not go up across runs: \"$GAMES_FIRST\" to \"$GAMES_SECOND\""
fi

# Run 3: a broken plugin beside it

mkdir -p "$WORK/userdata/plugins/$BROKEN"
printf '{ this is not json' > "$WORK/userdata/plugins/$BROKEN/plugin.json"

run third "--plugin=$BROKEN"
THIRD="$WORK/third.out"
check_common third
check_lua third
check_count "third: one failure message" 1 "$MESSAGE" "$THIRD"
if grep -F "$BROKEN" "$THIRD" | grep -qF "plugin.json"; then
    echo "ok: third: the broken plugin's failure is in the log"
else
    fail "third: no line naming $BROKEN and its plugin.json in $THIRD"
fi

[ "$FAILED" -eq 0 ] && echo "ALL OK" || echo "FAILED"
exit $FAILED
