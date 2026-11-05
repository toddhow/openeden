#!/bin/bash
# Adds (or removes) the Landscape Remastered texture mod's data folders to a prepared test profile's openmw.cfg.
# usage: tools/testprofile/texturemod.sh <profile folder> add|remove [--pbr] [--lava]
#   <profile folder>  a folder prepare.sh made, e.g. I:/Projects/OpenEden-work/testprofile/main (its config/openmw.cfg is edited)
#   --pbr             also add "01 PBR" (the _spec maps; the engine's own PBR maps are _rmaos, so these may do nothing)
#   --lava            also add "02 glowing lava rocks" (meshes and two 2048 textures)
# The mod is read from MOD below (extract the rar there, with 7-Zip, into three folders: 00 core, 01 PBR, 02 ...).
# Only the profile copy is edited, never your real config. prepare.sh makes a new openmw.cfg, so run this after it.
MOD="I:/Projects/OpenEden-work/mods/landscape-remastered"
profile=$1; action=$2; shift 2
[ -n "$profile" ] && [ -f "$profile/config/openmw.cfg" ] || { echo "no $profile/config/openmw.cfg"; exit 1; }
cfg="$profile/config/openmw.cfg"
folders=("00 core")
for a in "$@"; do case $a in --pbr) folders+=("01 PBR") ;; --lava) folders+=("02 glowing lava rocks") ;; esac; done
# remove every line of the mod first, so add is repeatable
grep -v -F "$MOD/" "$cfg" > "$cfg.tmp" && mv "$cfg.tmp" "$cfg"
if [ "$action" = add ]; then
    last=$(grep -n '^data=' "$cfg" | tail -1 | cut -d: -f1)
    lines=""; for f in "${folders[@]}"; do lines+="data=\"$MOD/$f\""$'\n'; done
    awk -v n="$last" -v add="$lines" '{print} NR==n{printf "%s", add}' "$cfg" > "$cfg.tmp" && mv "$cfg.tmp" "$cfg"
fi
grep -n '^data=' "$cfg"
