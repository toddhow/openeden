#!/bin/bash -ex

# OpenMW's naming convention (lower case letters and digits only) applies to inherited code and to code that lives
# among it. These trees are OpenEden's own and follow CreativeMotion's standards (snake_case file names), or are
# a vendored copy that is never edited in place: components/creativemotion.
git ls-files -- ':(exclude)extern/' \
        ':(exclude)components/extension' ':(exclude)components/creativemotion' ':(exclude)sdk' ':(exclude)extensions' \
        ':(exclude)editor_modules' ':(exclude)game_packs' ':(exclude)apps/components_tests/extension' ':(exclude)apps/opencs_tests/model/module' \
        ':(exclude)apps/components_tests/inertialmotion/test_locomotion_state.cpp' \
        ':(exclude)apps/components_tests/mwrender/test_creativemotion_runtime.cpp' \
        ':(exclude)apps/components_tests/mwrender/test_creativemotion_style.cpp' \
        ':(exclude)apps/components_tests/mwrender/test_creativemotion_style_driver.cpp' \
        ':(exclude)apps/components_tests/mwrender/test_creativemotion_intent.cpp' \
        ':(exclude)apps/components_tests/mwrender/test_game_conventions_sites.cpp' \
        ':(exclude)apps/openmw_tests/test_vsg_v1_invariants.cpp' \
        '*.cpp' '*.hpp' '*.h' |
    grep -vP '/[a-z0-9]+\.(cpp|hpp|h)$' &&
    ( echo 'File names do not follow the naming convention, see https://wiki.openmw.org/index.php?title=Naming_Conventions#Files'; exit -1 )
exit 0
