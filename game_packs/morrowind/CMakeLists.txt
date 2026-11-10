# game_packs/morrowind/CMakeLists.txt (task 0085): the Morrowind game pack. A STATIC pack: its rules are the game's own
# classes (they use the world model, the managers and the records), so it is linked into the executable and the tests
# next to openmw-lib, which it links PUBLIC. openmw-lib never names this target; the executable installs the pack's rules
# (MWWorld::installCraftingRules) and the windows reach them through the contract (sdk/include/openeden/game/crafting.hpp).
#
# Move 3 put the journal and the dialogue manager (with the filter) here, behind the engine's MWBase interfaces; the
# executable installs them through MWDialogue::installFactories.
#
# Move 2 put the crafting rules here: alchemy, enchanting, recharge and repair, moved from apps/openmw/mwmechanics.

# Move 4 (task 0098) put Morrowind's script command set here: the handlers of the commands (the *extensions files, moved
# from apps/openmw/mwscript) and the compile table (script_table.cpp, with the opcode numbers in compiler_opcodes.hpp,
# moved from components/compiler). The table is its own small library because the editor and the components tests need
# it and nothing else of the pack: it depends on the components alone. The executable hands both halves to the engine
# through MWScript::installCommandSet (game_morrowind::installScriptCommands).
add_library(game_morrowind_script_table STATIC
    src/script_table.cpp
    include/game_morrowind/compiler_opcodes.hpp
)
target_include_directories(game_morrowind_script_table PUBLIC include)
target_link_libraries(game_morrowind_script_table PUBLIC components)

openeden_add_game_pack(game_morrowind STATIC
    SOURCES
        src/alchemy.cpp
        src/enchanting.cpp
        src/recharge.cpp
        src/repair.cpp
        src/crafting_rules.cpp
        src/game_rules.cpp
        src/pack.cpp
        src/dialogue_rules.cpp
        src/dialoguemanagerimp.cpp
        src/journalimp.cpp
        src/filter.cpp
        src/selectwrapper.cpp
        src/scripttest.cpp
        src/aiextensions.cpp
        src/animationextensions.cpp
        src/cellextensions.cpp
        src/containerextensions.cpp
        src/controlextensions.cpp
        src/dialogueextensions.cpp
        src/guiextensions.cpp
        src/miscextensions.cpp
        src/skyextensions.cpp
        src/soundextensions.cpp
        src/statsextensions.cpp
        src/transformationextensions.cpp
        src/userextensions.cpp
        src/script_commands.cpp
        include/game_morrowind/alchemy.hpp
        include/game_morrowind/enchanting.hpp
        include/game_morrowind/recharge.hpp
        include/game_morrowind/repair.hpp
        include/game_morrowind/crafting_rules.hpp
        include/game_morrowind/game_rules.hpp
        include/game_morrowind/pack.hpp
        include/game_morrowind/dialogue_rules.hpp
        include/game_morrowind/dialoguemanagerimp.hpp
        include/game_morrowind/journalimp.hpp
        include/game_morrowind/filter.hpp
        include/game_morrowind/selectwrapper.hpp
        include/game_morrowind/scripttest.hpp
        include/game_morrowind/aiextensions.hpp
        include/game_morrowind/animationextensions.hpp
        include/game_morrowind/cellextensions.hpp
        include/game_morrowind/containerextensions.hpp
        include/game_morrowind/controlextensions.hpp
        include/game_morrowind/dialogueextensions.hpp
        include/game_morrowind/guiextensions.hpp
        include/game_morrowind/miscextensions.hpp
        include/game_morrowind/skyextensions.hpp
        include/game_morrowind/soundextensions.hpp
        include/game_morrowind/statsextensions.hpp
        include/game_morrowind/transformationextensions.hpp
        include/game_morrowind/userextensions.hpp
        include/game_morrowind/consoleextensions.hpp
        include/game_morrowind/script_commands.hpp
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

target_link_libraries(game_morrowind PUBLIC openmw-lib game_morrowind_script_table)
