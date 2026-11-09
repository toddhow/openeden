# game_packs/morrowind/CMakeLists.txt (task 0085): the Morrowind game pack. A STATIC pack: its rules are the game's own
# classes (they use the world model, the managers and the records), so it is linked into the executable and the tests
# next to openmw-lib, which it links PUBLIC. openmw-lib never names this target; the executable installs the pack's rules
# (MWWorld::installCraftingRules) and the windows reach them through the contract (sdk/include/openeden/game/crafting.hpp).
#
# Move 2 put the crafting rules here: alchemy, enchanting, recharge and repair, moved from apps/openmw/mwmechanics.

openeden_add_game_pack(game_morrowind STATIC
    SOURCES
        src/alchemy.cpp
        src/enchanting.cpp
        src/recharge.cpp
        src/repair.cpp
        src/crafting_rules.cpp
        include/game_morrowind/alchemy.hpp
        include/game_morrowind/enchanting.hpp
        include/game_morrowind/recharge.hpp
        include/game_morrowind/repair.hpp
        include/game_morrowind/crafting_rules.hpp
)

# The repository root is for the pack's own sources, which include <apps/openmw/...> by path; it is not exported, so a
# target that only links this pack does not get the whole repository on its include path.
target_include_directories(game_morrowind
    PUBLIC
        include
        ${CMAKE_SOURCE_DIR}/sdk/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(game_morrowind PUBLIC openmw-lib)
