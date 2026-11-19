#!/bin/bash
# tools/neutral_session_check (tasks 0107 and 0109, neutral world stages N3a-2b and N3a-3): starts the game with
# game=skyrimse and [Game] neutral world on and the five Skyrim SE masters as the ONLY content (no Morrowind.esm), the
# player in a named Skyrim interior (--start), and checks the log: the pack was handed only the Skyrim plugins, built its
# world view, registered its source, the session started through its own start path, the player stub stands in the
# cell, the engine inserted the cell's objects (counts by kind), and a menu script's attempt to save was refused. Prints
# aggregates only. Read only: nothing is written outside <work dir>. The test game opens off-screen
# (OPENEDEN_QUIET_WINDOW).
#
#   check.sh <build dir> <base config dir> <Skyrim Data folder> <work dir> [interior editor id] [frames] [screenshot frame]
#
# <base config dir> holds the openmw.cfg of a Morrowind setup (a COPY; its resources/extension/content/data-local lines
# are replaced; its Morrowind data folder stays in the VFS for the engine's interface files). <work dir> must be under
# I:/Projects/OpenEden-work.
set -e
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
BUILD=$1; BASE=$2; SKYRIM=$3; WORK=$4; CELL=${5:-WhiterunBanneredMare}; FRAMES=${6:-400}; SHOT=${7:-}
native() { cygpath -m "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -n "${KEEP_USER:-}" ] && [ -d "$WORK/user" ]; then
    mv "$WORK/user" "$WORK.user.keep"; rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/luatest"; mv "$WORK.user.keep" "$WORK/user"
else
    rm -rf "$WORK"; mkdir -p "$WORK/config" "$WORK/user" "$WORK/luatest"
fi
cp -r "$HERE/luatest/." "$WORK/luatest/"
# Task 0127 W-S4: W5_PLAN=<file> replaces the w5 scenario's plan (scripts/w5/plan.lua) for this run.
[ -n "${W5_PLAN:-}" ] && cp "$W5_PLAN" "$WORK/luatest/scripts/w5/plan.lua"
B=$(native "$BUILD")
grep -vE '^(content|data-local|resources|extension|user-data|config)=|vfs-mw|worldobjects_check|omwscripts' "$BASE/openmw.cfg" > "$WORK/config/openmw.cfg"
cat >> "$WORK/config/openmw.cfg" <<CFG
resources=$B/resources
data="$B/resources/vfs-mw"
data="$(native "$SKYRIM")"
data="$(native "$WORK/luatest")"
game=skyrimse
fallback-archive=Skyrim - Animations.bsa
fallback-archive=Skyrim - Interface.bsa
fallback-archive=Skyrim - Meshes0.bsa
fallback-archive=Skyrim - Meshes1.bsa
fallback-archive=Skyrim - Misc.bsa
fallback-archive=Skyrim - Textures0.bsa
fallback-archive=Skyrim - Textures1.bsa
fallback-archive=Skyrim - Textures2.bsa
fallback-archive=Skyrim - Textures3.bsa
fallback-archive=Skyrim - Textures4.bsa
fallback-archive=Skyrim - Textures5.bsa
fallback-archive=Skyrim - Textures6.bsa
fallback-archive=Skyrim - Textures7.bsa
fallback-archive=Skyrim - Textures8.bsa
content=Skyrim.esm
content=Update.esm
content=Dawnguard.esm
content=HearthFires.esm
content=Dragonborn.esm
content=n3a3save.omwscripts
CFG
# Optional extra openmw.cfg text (for example a further "fallback-archive=" line), appended as given. (The textures\skyrimhd\... paths some tree
# meshes name are in NO archive: `_ResourcePack.bsa` does not hold them, measured; the engine maps them to plain textures\... instead.)
[ -n "${CFG_EXTRA:-}" ] && printf '%b' "$CFG_EXTRA" >> "$WORK/config/openmw.cfg"
# Task 0121: "n5actors" visits each neutral actor of the Bannered Mare and takes a picture of it (use at least 1500 frames).
[ "$SHOT" = n5actors ] && echo "content=n5actors.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = n5idle ] && echo "content=n5idle.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0141: "n5talk" activates each neutral actor of the Bannered Mare and takes pictures of the conversation box; the presses come from OPENEDEN_NEUTRAL_DIALOGUE_PRESS (use at least 1500 frames).
[ "$SHOT" = n5talk ] && echo "content=n5talk.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0230: "bqtalk" activates Hulda 70 simulation seconds in (the bounty quest has started by then) and takes pictures; clicks come from OPENEDEN_RMLUI_INPUT_SCRIPT.
[ "$SHOT" = bqtalk ] && echo "content=bqtalk.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0146: "n5lip" stands before Hulda, talks to her and takes a picture of her face every quarter second (use at least 6000 frames).
[ "$SHOT" = n5lip ] && echo "content=n5lip.omwscripts" >> "$WORK/config/openmw.cfg"
echo "return { place = ${LIP_PLACE:-6}, activate = ${LIP_ACTIVATE:-9} }" > "$WORK/luatest/scripts/n5/lipcfg.lua" # task 0243
# Task 0135: "n5body" walks the player into each neutral actor of the Bannered Mare (use at least 3600 frames).
[ "$SHOT" = n5body ] && echo "content=n5body.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = n5bodydraw ] && echo "content=n5bodydraw.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0135 stage S2: "n5walk" takes pictures of a neutral actor walked by OPENEDEN_NEUTRAL_ACTOR_WALK (use at least 900 frames).
[ "$SHOT" = n5walk ] && echo "content=n5walk.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0170 (combat stage S5-2): "n5swing" takes a picture every 0.25 s of Hulda while the pack makes her swing (see luatest/scripts/n5/swing.lua).
[ "$SHOT" = n5swing ] && echo "content=n5swing.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0171 (combat stage S5-3): "n5hit" stands the player beside Hulda (scripts/n5/hit.lua) and takes a picture every 0.25 s; the melee test settings decide who swings.
[ "$SHOT" = n5hit ] && echo "content=n5hit.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0171: "n5trade" stands the player 100 units from Hulda and takes a picture every 0.5 s while she and the player trade blows (the melee test settings in SETTINGS_EXTRA).
[ "$SHOT" = n5trade ] && echo "content=n5trade.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = n5kill ] && echo "content=n5kill.omwscripts" >> "$WORK/config/openmw.cfg" # task 0278
[ "$SHOT" = n5death ] && echo "content=n5death.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = n5tradewall ] && echo "content=n5tradewall.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0178 (combat stage S5-6): "n5bow" stands the player BOW_DIST (default 400) units east of Hulda and takes a picture every BOW_EVERY (0.5) seconds,
# and every frame inside the simulation-time windows of BOW_BURST ("t0,t1;t2,t3"), from the static camera BOW_CAM ("x,y,z,lookX,lookY,lookZ", default: the player's own); the bow test settings go in SETTINGS_EXTRA (see luatest/scripts/n5/bow.lua).
[ "$SHOT" = n5bow ] && echo "content=n5bow.omwscripts" >> "$WORK/config/openmw.cfg"
burst=""; IFS=';' read -ra windows <<< "${BOW_BURST:-}"; for w in "${windows[@]}"; do [ -n "$w" ] && burst="$burst{${w}},"; done
cam=""; [ -n "${BOW_CAM:-}" ] && cam=", cam = { ${BOW_CAM} }"
echo "return { dist = ${BOW_DIST:-400}, every = ${BOW_EVERY:-0.5}, burst = { ${burst} }${cam} }" > "$WORK/luatest/scripts/n5/bowcfg.lua"
# Task 0192 (combat on many actors): "n5many" is a crowd fight in Whiterun's street (CELL='@WhiterunWorld,25670,-7633'; MANY_* variables, see luatest/scripts/n5/manyglobal.lua); "n5manyprobe" only lists what Lua sees.
case "$SHOT" in n5many|n5manyscan) echo "content=$SHOT.omwscripts" >> "$WORK/config/openmw.cfg" ;; esac
echo "return { hostile = ${MANY_HOSTILE:-8}, duels = ${MANY_DUELS:-0}, park = ${MANY_PARK:-2200}, away = ${MANY_AWAY:-0}, awayAt = ${MANY_AWAY_AT:-25}, every = ${MANY_EVERY:-0}, maxShots = ${MANY_SHOTS:-12}, camBack = ${MANY_CAM_BACK:-420}, camUp = ${MANY_CAM_UP:-260} }" > "$WORK/luatest/scripts/n5/manycfg.lua"
# Task 0197 (NPC against NPC): "n5npcfight" builds a fight in Whiterun street (CELL=@WhiterunWorld,25670,-7633); NF_MODE=guards|follower,
# NF_ATTACKER and NF_FOLLOWER are base numbers (decimal) of street guards, NF_COUNT guards in the ring, NF_DIST, NF_CAM_BACK, NF_CAM_UP, NF_EVERY, NF_SHOTS
# (see luatest/scripts/n5/npcfightglobal.lua); the settings (combat test attacker, follower test recruit) go in SETTINGS_EXTRA.
[ "$SHOT" = n5npcfight ] && echo "content=n5npcfight.omwscripts" >> "$WORK/config/openmw.cfg"
echo "return { mode = '${NF_MODE:-guards}', attacker = ${NF_ATTACKER:-1048812}, follower = ${NF_FOLLOWER:-1048819}, count = ${NF_COUNT:-6}, parkMode = '${NF_PARK:-teleport}', dist = ${NF_DIST:-1100}, camBack = ${NF_CAM_BACK:-380}, camUp = ${NF_CAM_UP:-300}, every = ${NF_EVERY:-1.0}, maxShots = ${NF_SHOTS:-12} }" > "$WORK/luatest/scripts/n5/npcfightcfg.lua"
# Task 0190 (combat stage S5-9): "n5ai" stands the player AI_DIST (default 300) units east of Hulda, keeps him facing her and
# takes a picture every AI_EVERY (0.5) seconds of real time for AI_WINDOW (60) seconds; the AI settings go in SETTINGS_EXTRA
# (see luatest/scripts/n5/aiglobal.lua).
[ "$SHOT" = n5ai ] && echo "content=n5ai.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0231 (barks): "n5bark" visits the Mare's actors one after the other (far, then near; luatest/scripts/n5/barkglobal.lua) and takes a picture
# BARK_SHOT seconds after each arrival; BARK_TAGS="'b80803','b108144'" BARK_FAR BARK_NEAR BARK_WAIT BARK_APPROACH BARK_HOLD BARK_SHOT BARK_START. The bark settings go in SETTINGS_EXTRA.
[ "$SHOT" = n5bark ] && echo "content=n5bark.omwscripts" >> "$WORK/config/openmw.cfg"
echo "return { tags = { ${BARK_TAGS:-'b80803','b108144','b80802','b80807'} }, far = ${BARK_FAR:-900}, near = ${BARK_NEAR:-250}, wait = ${BARK_WAIT:-2}, approach = ${BARK_APPROACH:-1}, hold = ${BARK_HOLD:-8}, shot = ${BARK_SHOT:-2.5}, start = ${BARK_START:-30} }" > "$WORK/luatest/scripts/n5/barkcfg.lua"
# Task 0212 (creatures): "n5wolf" is n5ai in daylight with a side camera between the player and the wolf (luatest/scripts/n5/wolfglobal.lua; same AI_* variables).
[ "$SHOT" = n5wolf ] && echo "content=n5wolf.omwscripts" >> "$WORK/config/openmw.cfg"
echo "return { start = ${AI_START:-0.3}, dist = ${AI_DIST:-300}, every = ${AI_EVERY:-0.5}, window = ${AI_WINDOW:-60}, target = '${AI_TARGET:-b80803}'${AI_CAM:+, cam = { $AI_CAM }} }" > "$WORK/luatest/scripts/n5/aicfg.lua"
# Task 0214 (real weapons): "n5gear" puts the actor with record id GEAR_TARGET (default skyrimse:b1048812, a Whiterun guard) or a fresh GEAR_CREATE
# object GEAR_DIST (default 130) units east of the player, faces them, and takes a picture every GEAR_EVERY (0.5) seconds (GEAR_SHOTS of them) from
# GEAR_CAM (x,y,z relative to the player); the combat settings go in SETTINGS_EXTRA (see luatest/scripts/n5/geargobal.lua).
[ "$SHOT" = n5gear ] && echo "content=n5gear.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0222: "pbody" shows the player's Skyrim body in third person: the standard third person camera, a static camera in front
# (standing, then dressed: PB_ITEMS are the hex ids the pack equips), a walk, a run and swings (the pack setting
# "combat test player swing"); PB_PLAN=short stops after the dressed picture; PB_HOUR sets and freezes the hour (see luatest/scripts/pbody/player.lua).
[ "$SHOT" = pbody ] && echo "content=pbody.omwscripts" >> "$WORK/config/openmw.cfg"
mkdir -p "$WORK/luatest/scripts/pbody"
items=""; IFS=',' read -ra ids <<< "${PB_ITEMS:-}"; for i in "${ids[@]}"; do [ -n "$i" ] && items="$items'$i',"; done
echo "return { swingAt = ${PB_SWING_AT:-9}, hour = ${PB_HOUR:-nil}, plan = '${PB_PLAN:-full}', items = { $items }, frontDistance = ${PB_FRONT:-230}, frontHeight = ${PB_FRONT_H:-90}, sideDistance = ${PB_SIDE:-260}, sideHeight = ${PB_SIDE_H:-100}, bursts = { ${PB_BURSTS:-} }, swingStep = ${PB_STEP:-0.1}, swingCount = ${PB_COUNT:-30}, pitch = ${PB_PITCH:-0.6}, race = ${PB_RACE:-nil}, female = ${PB_FEMALE:-false}, raceAt = ${PB_RACE_AT:-2.0}, drawAt = ${PB_DRAW_AT:-4.0} }" > "$WORK/luatest/scripts/pbody/cfg.lua"
echo "return { start = ${GEAR_START:-2}, dist = ${GEAR_DIST:-130}, every = ${GEAR_EVERY:-0.5}, maxShots = ${GEAR_SHOTS:-40}, target = '${GEAR_TARGET:-skyrimse:b1048812}', create = '${GEAR_CREATE:-}', hour = ${GEAR_HOUR:-nil}, cam = { ${GEAR_CAM:-0, -300, 120} } }" > "$WORK/luatest/scripts/n5/gearcfg.lua"
# Task 0204 (crime): "n8crime" stands the player N8_DIST (default 100) units east of the witness N8_TARGET (default b80803, Hulda) and commits the crime of N8_MODE
# (theft from the owned container N8_PLACEMENT/N8_BASE at N8_T seconds; assault is the pack setting combat test player swing); N8_CHOICE (pay, jail, resist or empty)
# is chosen N8_DELAY seconds after the guard's offer (luatest/scripts/n8/crimeglobal.lua). The crime settings go in SETTINGS_EXTRA.
[ "$SHOT" = n8crime ] && echo "content=n8crime.omwscripts" >> "$WORK/config/openmw.cfg"
echo "return { mode = '${N8_MODE:-theft}', target = '${N8_TARGET:-b80803}', dist = ${N8_DIST:-100}, item = '${N8_ITEM:-}', placement = '${N8_PLACEMENT:-0009cafb}', base = '${N8_BASE:-0009cafa}', tCrime = ${N8_T:-4}, choice = '${N8_CHOICE:-}', choiceDelay = ${N8_DELAY:-2}, every = ${N8_EVERY:-5}, maxShots = ${N8_SHOTS:-30}, guard = '${N8_GUARD:-}', phase = '${N8_PHASE:-none}', saveT = ${N8_SAVE_T:-10} }" > "$WORK/luatest/scripts/n8/cfg.lua"
# Task 0208 (pickup, pickpocket): "n9pick" stands the player N9_DIST (default 60) units from the nearest object whose record id is N9_BASE
# (default: any skyrimse object near N9_NEAR "x,y,z"), on the side of N9_FROM "x,y" and tilted to N9_PITCH (radians, down is positive), and logs
# N9 lines every second (the count of N9_ITEM (8 hex digits) in the inventory, whether the object is still enabled, the pickup state, the bounty);
# the activate key comes from OPENEDEN_RMLUI_INPUT_SCRIPT (needs [GUI] rmlui = true). N9_SNEAK=1 holds the sneak control. N9_SHOTS: real seconds of pictures.
[ "$SHOT" = n9pick ] && echo "content=n9pick.omwscripts" >> "$WORK/config/openmw.cfg"
mkdir -p "$WORK/luatest/scripts/n9"
echo "return { base = '${N9_BASE:-}', near = { ${N9_NEAR:-0, 0, 0} }, from = { ${N9_FROM:-1, 0} }, dist = ${N9_DIST:-60}, drop = ${N9_DROP:-0}, pitch = ${N9_PITCH:-0}, yaw = ${N9_YAW:-0}, phase = '${N9_PHASE:-none}', saveT = ${N9_SAVE_T:-20}, item = '${N9_ITEM:-}', sneak = $([ -n "${N9_SNEAK:-}" ] && echo true || echo false), advanceAt = ${N9_ADV_T:-0}, advanceHours = ${N9_ADV_H:-0}, hour = ${N9_HOUR:-nil}, scan = ${N9_SCAN:-nil}, scanr = ${N9_SCANR:-nil}, hide = $([ -n "${N9_HIDE:-}" ] && echo "'${N9_HIDE}'" || echo nil), autoaim = $([ -n "${N9_AUTOAIM:-}" ] && echo true || echo false), aim = $([ -n "${N9_AIM:-}" ] && echo "{ ${N9_AIM} }" || echo nil), shots = { ${N9_SHOTS:-8, 14} } }" > "$WORK/luatest/scripts/n9/cfg.lua"
[ "$SHOT" = n5sightprobe ] && echo "content=n5sightprobe.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0237 (standing stones and shrines): "stn" stands the player in front of the objects of STN_STEPS (Lua table text: { t = 6, base = "skyrimse:b860973",
# near = { 2454, -59141, 1430 }, from = { 0, -1 }, dist = 90 }, ...) at those simulation seconds and takes a picture at each STN_SHOTS time; the activations and
# clicks come from OPENEDEN_RMLUI_INPUT_SCRIPT (needs [GUI] rmlui = true and the pack settings papyrus and player progression in SETTINGS_EXTRA).
# Task 0241: STN_ADV_T / STN_ADV_H advance the game clock by STN_ADV_H hours at that simulation second (a wait; needs the pack setting rest).
[ "$SHOT" = stn ] && echo "content=stn.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0260 (vampirism): "vamp" runs the plan V_PLAN (steps at simulation seconds; see luatest/scripts/vamp/global.lua) in the Mare: clock jumps, hour, the front door, standing before a
# sleeper and activating it, pictures, a save. The settings (vampirism, diseases test infect, rest, papyrus ...) go in SETTINGS_EXTRA.
[ "$SHOT" = vamp ] && echo "content=vamp.omwscripts" >> "$WORK/config/openmw.cfg"
mkdir -p "$WORK/luatest/scripts/vamp"
echo "return { plan = { ${V_PLAN:-} } }" > "$WORK/luatest/scripts/vamp/cfg.lua"
# Task 0257 (Night Eye): "ne" leaves the Mare into WhiterunWorld at game hour NE_HOUR (default 23, clock stopped) and takes a picture every NE_EVERY (3) simulation
# seconds, NE_COUNT (30) times, from NE_FIRST (3) seconds after the door, looking along NE_YAW / NE_PITCH (radians). The cast is the pack test rule, e.g.
# SETTINGS_EXTRA with [extension.skyrimse] combat test player spell = PowerKhajiitNightEye, combat test player cast every = 15, night eye = true, and [Game] neutral image space effects = true.
[ "$SHOT" = ne ] && echo "content=ne.omwscripts" >> "$WORK/config/openmw.cfg"
mkdir -p "$WORK/luatest/scripts/ne"
echo "return { hour = ${NE_HOUR:-23}, every = ${NE_EVERY:-3}, count = ${NE_COUNT:-30}, first = ${NE_FIRST:-3}, yaw = ${NE_YAW:-0}, pitch = ${NE_PITCH:--0.05} }" > "$WORK/luatest/scripts/ne/cfg.lua"
mkdir -p "$WORK/luatest/scripts/stn"
echo "return { steps = { ${STN_STEPS:-} }, shots = { ${STN_SHOTS:-} }, saveAt = ${STN_SAVE:-nil}, advanceAt = ${STN_ADV_T:-nil}, advanceHours = ${STN_ADV_H:-0} }" > "$WORK/luatest/scripts/stn/cfg.lua"
# Task 0135 stage S3 (actor hooks): the same scenario, but the pack walks the actor: put "[extension.skyrimse]" and
# "actor test walk = <editor id>" into SETTINGS_EXTRA and OPENEDEN_NEUTRAL_ACTOR_TRACE=1 in the environment; the NACTWALK
# lines then follow the pacing actor. Leave OPENEDEN_NEUTRAL_ACTOR_WALK unset.
# Task 0135 stage S4 (go-to): "n5goto" takes pictures every 2 s of the actor the pack sends along a navmesh path (luatest/scripts/n5/goto.lua; use at least 2400 frames).
[ "$SHOT" = n5goto ] && echo "content=n5goto.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0135 stage S5 (AI packages): "n5pack" takes pictures every 12 s of the Mare while the NPCs run their packages (set "[extension.skyrimse] actor packages = true" in SETTINGS_EXTRA and OPENEDEN_NEUTRAL_ACTOR_TRACE=1; use at least 6000 frames).
[ "$SHOT" = n5pack ] && echo "content=n5pack.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = n5furn ] && echo "content=n5furn.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0135 stages S6f and S6g: "n5wake" activates a sleeper (luatest/scripts/n5/wakeglobal.lua) and takes pictures before, while it gets up (the box opens only once it stands), during and after the talk (midnight; needs [GUI] rmlui = true and OPENEDEN_RMLUI_INPUT_SCRIPT with a click on Goodbye after the box opened, frame 16500 on the author's machine; use at least 36000 frames).
[ "$SHOT" = n5wake ] && echo "content=n5wake.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0135 stage S6f: "n5follow" follows three actors with the camera, one picture a second (luatest/scripts/n5/followglobal.lua; use at least 40000 frames).
[ "$SHOT" = n5follow ] && echo "content=n5follow.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0196 (followers): "n7follow" stands the player at F7_STAND, lets the pack recruit the actor (the test settings in SETTINGS_EXTRA), walks him to F7_LEG1, uses
# the Mare's front door and walks on in Whiterun (luatest/scripts/n7f/global.lua; simulation time, so use OPENEDEN_FIXED_FRAME_TIME and at least 6000 frames).
[ "$SHOT" = n7follow ] && echo "content=n7follow.omwscripts" >> "$WORK/config/openmw.cfg"
mkdir -p "$WORK/luatest/scripts/n7f"
echo "return { tag = '${F7_TAG:-b108144}', start = ${F7_START:-8}, stand = { ${F7_STAND:--60, -180, 72, 0} }, leg1 = { ${F7_LEG1:-100, -500, 72} }, rest = ${F7_REST:-6}, doorStand = { ${F7_DOORSTAND:--128, -1000, 72, 3.1416} }, doorBase = 'skyrimse:b152263', doorNear = { -128, -1152, 120 }, outsideWait = ${F7_OUTSIDE:-8}, mode = '${F7_MODE:-run}', awayAt = ${F7_AWAYAT:-14}, away = ${F7_AWAY:-1500}, leg2 = { ${F7_LEG2:-400, 0} } }" > "$WORK/luatest/scripts/n7f/followcfg.lua"
# Task 0157: "skyreach" runs path queries with several agent sizes between the Mare's ground floor, stairs and beds (NAV lines).
[ "$SHOT" = skyreach ] && echo "content=skyreach.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0148: "n5loco" is "n5goto" with a picture every 0.2 s (use at least 2400 frames; also set [Game] neutral actor locomotion).
[ "$SHOT" = n5loco ] && echo "content=n5loco.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = look ] && echo "content=n3a4look.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0355 stage g: "l55g" visits the camera spots L55G_SPOTS ("{x,y,z,yaw,pitch},{...}"; yaw 0 = +y, pitch down positive) in the start cell and takes a picture
# 1.2 s after each move (luatest/scripts/l55g/cam.lua; use about 700 frames plus 180 per spot).
[ "$SHOT" = l55g ] && echo "content=l55gcam.omwscripts" >> "$WORK/config/openmw.cfg"
echo "return { ${L55G_SPOTS:-} }" > "$WORK/luatest/scripts/l55g/spots.lua"
# Task 0131 stage N3a-6: "ms0sweep" runs the Lua API sweep over the neutral cell (use at least 1500 frames).
[ "$SHOT" = ms0sweep ] && echo "content=ms0sweep.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0110 stage N3a-4b: "collision" as the screenshot argument runs the player through the collision script (stands, then
# walks four legs, logging height above the floor and what stops it); use at least 1000 frames.
[ "$SHOT" = collision ] && echo "content=n3a4bcollision.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = colldraw ] && echo "content=n3a4bdraw.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0128: "stairs" walks the player up and down the Bannered Mare stairs (use at least 1500 frames); "keylocked" and "keycontainer" are the lock scenarios (give the key with OPENEDEN_NEUTRAL_KEYS).
[ "$SHOT" = stairs ] && echo "content=n3a4bstairs.omwscripts" >> "$WORK/config/openmw.cfg"
case "$SHOT" in keylocked|keycontainer) echo "content=n4$SHOT.omwscripts" >> "$WORK/config/openmw.cfg" ;; esac
# Task 0115 stage N4 (doors): "doorshop", "doorsexterior" and "doorslocked" run the door scenarios (use at least 2400 frames).
case "$SHOT" in doorshop|doorsexterior|doorslocked|doorsstreet|doorsgate|doorsdrift|doorssweep) echo "content=n4$SHOT.omwscripts" >> "$WORK/config/openmw.cfg" ;; esac
# Task 0125 stage E2: "e2terrain" leaves the Mare by its front door into WhiterunWorld, walks, and hops across cell borders
# (use at least 4000 frames).
# Task 0134 (M-S3 stage 1): "navpath" runs path queries over the navmesh of the Mare and of Whiterun's street, with the
# navmesh debug view (use at least 6000 frames; set [Navigator] settings through SETTINGS_EXTRA).
[ "$SHOT" = navpath ] && echo "content=navpath.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = e2terrain ] && echo "content=n4e2.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0149: "tlayers" leaves the Mare, visits spots of Whiterun's plains and takes a picture of the ground at each (use at least 3000 frames).
[ "$SHOT" = tlayers ] && echo "content=terrainlayers.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0153: "grass" leaves the Mare, visits plains spots of Whiterun in first person and takes a picture at each after 14 s (use at least 8000 frames and CHECK_TIMEOUT=300; compare [Game] neutral grass off and on).
[ "$SHOT" = grass ] && echo "content=grass.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0129: "e2water" visits a lake and a river of WhiterunWorld and takes pictures (use at least 4000 frames).
[ "$SHOT" = e2water ] && echo "content=n4e2water.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0127 stage W-S1: "w1" leaves the Mare into WhiterunWorld, looks at the sky at several hours, then speeds the clock up and
# takes a picture at each weather change (use at least 4000 frames; set [Game] neutral weather = true through SETTINGS_EXTRA).
[ "$SHOT" = w1 ] && echo "content=w1.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0127 stage W-S4: "w5" walks scripts/w5/plan.lua (game hour, yaw, pitch and wait of each picture, after leaving the Mare into
# WhiterunWorld); it is the moons, the storm's sounds and the image space scenario (use at least 4000 frames; the sound
# archive is a CFG_EXTRA line: fallback-archive=Skyrim - Sounds.bsa). The log lines are "Neutral weather sound:" and "moon".
[ "$SHOT" = w5 ] && echo "content=w5.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0291 (swimming and water): "swim291" drops the player into water at SW_X,SW_Y,SW_Z (water surface SW_WATER; CELL names the world, for
# example CELL='@RiftenWorld,168000,-95000'), swims at the surface, dives, rises and (god mode off) drowns, logging "S291" lines every SW_EVERY frames;
# SW_FALL, SW_SURFACE, SW_DIVE, SW_RISE and SW_DRAWN are the lengths of the phases in frames, SW_SHOTS the frames of the pictures ("100,400").
if [ "$SHOT" = swim291 ]; then
    echo "content=n4swim291.omwscripts" >> "$WORK/config/openmw.cfg"
    shots=""; IFS=',' read -ra frames <<< "${SW_SHOTS:-}"; for fr in "${frames[@]}"; do [ -n "$fr" ] && shots="$shots[$fr]=true,"; done
    probe=""; [ -n "${SW_PROBE:-}" ] && probe=", probe = { $SW_PROBE }"
    echo "return { x = ${SW_X:-0}, y = ${SW_Y:-0}, z = ${SW_Z:-0}, yaw = ${SW_YAW:-0}, water = ${SW_WATER:-0}, startFrame = ${SW_START:-600}, fall = ${SW_FALL:-240}, surface = ${SW_SURFACE:-300}, dive = ${SW_DIVE:-300}, rise = ${SW_RISE:-300}, drown = ${SW_DROWN:-1500}, every = ${SW_EVERY:-30}, shots = { $shots }$probe${SW_KEEP:+, keep = true} }" > "$WORK/luatest/scripts/swim291/swimcfg.lua"
fi
# Task 0129 swimming follow-up: "e2swim" swims in the Whiterun lake and walks out (use at least 6000 frames).
[ "$SHOT" = e2swim ] && echo "content=n4e2swim.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0127 stage W-S2: "w2" leaves the Mare into WhiterunWorld and takes pictures on a fast clock; force a weather with
# OPENEDEN_NEUTRAL_FORCE_WEATHER=<editor id> (use at least 3500 frames and CHECK_TIMEOUT=240).
[ "$SHOT" = w2 ] && echo "content=w2.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0127 stage W-S3: "w3" leaves the Mare and aims the camera at the sun's direction for several hours (use at least 4000 frames).
[ "$SHOT" = w3 ] && echo "content=w3.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0133 (milestone M-S2): "lodbake" runs `openmw --bake-lod-terrain` in the session (the world is OPENEDEN_LOD_BAKE_WORLD,
# default Tamriel); the pack lands in <work>/user/openeden/lod/skyrimse/<world>/terrain.edenpak. "lodview" is the e2 walk
# with the world LOD on (put the [WorldLod] settings in SETTINGS_EXTRA and keep the baked user folder with KEEP_USER=1).
BAKE=""
[ "$SHOT" = lodbake ] && BAKE="--bake-lod-terrain"
[ "$SHOT" = lodview ] && echo "content=n4lodview.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0136: "trees" walks out of the Mare, stands before the nearest Skyrim tree and takes four pictures half a second apart
# (use at least 2500 frames; set [Game] neutral tree sway = true through SETTINGS_EXTRA; OPENEDEN_NEUTRAL_TREE_WIND forces the wind).
[ "$SHOT" = trees ] && echo "content=trees.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0133 stage 2: "lodobjbake" bakes the far object clusters (--bake-lod-objects) of the same world; "lodobjview" is "lodview" for them.
[ "$SHOT" = lodobjbake ] && BAKE="--bake-lod-objects"
case "$SHOT" in lodview|lodobjview) echo "content=n4lodview.omwscripts" >> "$WORK/config/openmw.cfg" ;; esac
# Task 0136 (skinned trunks): the same walk to a grove of the 35 TREE records whose model has a BSTreeNode (trunk and branches are skinned shapes).
[ "$SHOT" = trunks ] && echo "content=trunks.omwscripts" >> "$WORK/config/openmw.cfg"
[ "$SHOT" = poslog ] && echo "content=poslog.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0150: "actactor" and "actdoor" stand the player before Hulda or the Mare front door, facing it;
# the activate key then comes from OPENEDEN_RMLUI_INPUT_SCRIPT (needs [GUI] rmlui = true in SETTINGS_EXTRA) and
# OPENEDEN_ACTIVATE_TRACE=1 logs the focus query (use at least 2400 frames; the script's press frame must be after real second 6).
case "$SHOT" in actactor|actdoor|seqdoor|seqwalk) echo "content=$SHOT.omwscripts" >> "$WORK/config/openmw.cfg" ;; esac
# Task skyext: "skyext" turns the camera through eight headings at a game hour (SKYEXT_HOUR in global.lua) from an exterior start
# (CELL=@Tamriel,x,y) with a screenshot and a ray survey per heading (use at least 2500 frames).
[ "$SHOT" = skyext ] && echo "content=skyext.omwscripts" >> "$WORK/config/openmw.cfg"
# Task 0152: "maredump" logs every object's model and position (MAREOBJ lines); "marecam" visits the camera spots of
# luatest/scripts/mare/cam.lua and takes a picture at each (use at least 2500 frames).
case "$SHOT" in maredump|marecam|fxcam|floormap|landnormal) echo "content=$SHOT.omwscripts" >> "$WORK/config/openmw.cfg" ;; esac
# Task 0359: "fxcam" is "marecam" with its own spot list (luatest/scripts/fx/spots.lua) for fire and effect models in any start cell.
# Task 0151: "trim" surveys the floating MountainTrim01 slab at @Tamriel,104403,18631 (CELL=@Tamriel,104000,18000; use at least 3500 frames, CHECK_TIMEOUT=300).
[ "$SHOT" = trim ] && echo "content=trim.omwscripts" >> "$WORK/config/openmw.cfg"
# TRIM_VARIANT=old|fix re-applies the slab's rotation from Lua with the pack's old (Rz*Ry*Rx) or the new (Rx*Ry*Rz) order; default none.
echo "return '${TRIM_VARIANT:-none}'" > "$WORK/luatest/scripts/trim/variant.lua"
# Task 0135 stage S6f ("n5follow"): FOLLOW_TARGETS="'b108144','b80802'" FOLLOW_WINDOW=36 FOLLOW_STEP=1.0 FOLLOW_DIST=190 FOLLOW_OFFSET=0.9
echo "return { targets = { ${FOLLOW_TARGETS:-'b108144','b80802','b80807'} }, window = ${FOLLOW_WINDOW:-36}, step = ${FOLLOW_STEP:-1.0}, dist = ${FOLLOW_DIST:-190}, offset = ${FOLLOW_OFFSET:-0.9} }" > "$WORK/luatest/scripts/n5/followcfg.lua"
printf '[Game]\nneutral world = true\n' > "$WORK/config/settings.cfg"
# Optional extra settings text (for example "[Rendering]\nframe counters = true\n"), appended as given.
[ -n "${SETTINGS_EXTRA:-}" ] && printf '%b' "$SETTINGS_EXTRA" >> "$WORK/config/settings.cfg"
cd "$BUILD"
set +e
PROBE_CELL="$CELL"
# Task 0142: a start text beginning with @ is an exterior spot (--start @<world>,<x>,<y>[,<z>]); the probe cell is then a fixed interior.
case "$CELL" in @*) PROBE_CELL=WhiterunBanneredMare ;; esac
OPENEDEN_SKYRIMSE_PROBE_CELL="$PROBE_CELL" timeout "${CHECK_TIMEOUT:-170}" ./openmw.exe --replace=config --config "$(native "$WORK/config")" \
    --user-data "$(native "$WORK/user")" --skip-menu --no-grab=true --random-seed 7 ${LOADARG:---start "$CELL"} $BAKE \
    ${SHOT:+$(case "$SHOT" in look|ms0sweep|collision|colldraw|doors*|n5actors|n5idle|e2terrain|e2water|e2swim|w1|stairs|keylocked|keycontainer|w2|w5|lodbake|lodview|navpath|n5body|n5bodydraw|trees|lodobjbake|lodobjview|w3|trunks|n5talk|bqtalk|n5lip|poslog|n5walk|n5goto|actactor|actdoor|skyext|trim|n5pack|tlayers|n5loco|grass|maredump|marecam|floormap|landnormal|n5furn|skyreach|n5wake|n5follow|n5swing|n5hit|n5trade|n5kill|n5tradewall|n5sightprobe|n5death|n5bow|n5many|n5manyscan|n7follow|n5ai|n5bark|n5wolf|n5npcfight|n8crime|n9pick|stn|ne|vamp|n5gear|pbody|seqdoor|seqwalk|swim291|l55g|fxcam) ;; *) echo --auto-screenshot "$SHOT" ;; esac)} ${SCRIPT_RUN:+--script-run "$(native "$SCRIPT_RUN")"} --auto-quit "$FRAMES" > "$WORK/log.txt" 2>&1
code=$?
set -e
dumps=$(ls "$WORK"/user/*.dmp "$WORK"/config/*.dmp 2>/dev/null | wc -l)
grep -E "Session content|world view built|world source registered|probe cell|Starting a new game|Neutral start|Neutral cell|N3A3|Skyrim models|Skyrim collision|N4B|N4S|NDOOR|N4D|MS0|NLOCK|Neutral door|E2 |E2W|NWATER|Neutral terrain|Neutral grass|Neutral exterior|W1 |W2 |Neutral weather|LV |LOD terrain bake|World LOD|NAV |Navmesh|navmesh|N5B|NACT|TREES |Skyrim tree sway|LOD object bake|LOD clusters|TRUNKS |POSLOG |NTALK|N5T|N5A|ACTKEY|NLIP|Skyrim lip sync |SKYEXT |TRIM " "$WORK/log.txt" | sed 's/^\[[^]]*\] //'
# The one expected error line is the refusal to save (a neutral session has no save support until stage N6).
errors=$(grep -E " E\] " "$WORK/log.txt" | grep -vc "Saving refused" || true)
echo "exit code $code, $dumps crash dumps, $errors unexpected error lines"
grep -q "world view built from the session: 5 of 5 plugins" "$WORK/log.txt" || { echo "FAILED: the pack was not given the five Skyrim plugins"; exit 1; }
grep -q "probe cell found: yes" "$WORK/log.txt" || { echo "FAILED: the probe cell was not found"; exit 1; }
grep -q "Starting a new game (neutral world)" "$WORK/log.txt" || { echo "FAILED: the neutral start path did not run"; exit 1; }
case "$CELL" in
    @*) grep -q "Neutral start: the player stands at" "$WORK/log.txt" || { echo "FAILED: the exterior start did not run"; exit 1; } ;;
    *) grep -q "Neutral cell '$CELL' inserted into the scene: [1-9]" "$WORK/log.txt" || { echo "FAILED: no objects were inserted"; exit 1; } ;;
esac
grep -q "Saving refused" "$WORK/log.txt" || { echo "FAILED: the save attempt was not refused"; exit 1; }
[ "$code" = 0 ] && [ "$dumps" = 0 ] && [ "$errors" = 0 ] || { echo FAILED; exit 1; }
echo OK
