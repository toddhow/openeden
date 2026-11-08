#!/bin/bash
# Task 0085, move 1: generic engine files that used to name Morrowind's data conventions must ask
# Misc::gameConventions() (components/misc/gameconventions.hpp) instead. Fails when one of them names a convention
# again. The only place that names the values is that header (the fallback of a session with no pack) and the
# compat_morrowind pack that provides them. Run from the repository root.
set -u
files="components/vsgadapters/nif/nif.cpp
apps/openmw/mwrender/bodyparts.cpp
apps/openmw/mwrender/animcontext.cpp
components/mwanimation/actor.cpp
components/mwanimation/play.cpp
apps/openmw/mwrender/creativemotiondriver.cpp"
# character.cpp is checked for the unit only: its debug bone list is a later move (a skeleton profile).
pattern='"bip01|"root bone"|"base_anim|"skin"|\.1st"|xbase_anim\.1st|Constants::UnitsPerMeter'
status=0
# shellcheck disable=SC2086
if grep -nE "$pattern" $files; then
    echo "check_game_conventions: a listed file names a Morrowind convention (use Misc::gameConventions())"
    status=1
fi
if grep -nE 'Constants::UnitsPerMeter' apps/openmw/mwmechanics/character.cpp; then
    echo "check_game_conventions: character.cpp names the Morrowind length unit"
    status=1
fi
[ $status -eq 0 ] && echo "check_game_conventions: ok"
exit $status
