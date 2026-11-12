# game_packs/skyrimse/CMakeLists.txt (task 0086): the Skyrim SE game pack skeleton

openeden_add_game_pack(skyrimse
    SOURCES
        src/skyrimse_game_pack.cpp
        src/pack_core.cpp
        src/records.cpp
        src/actors.cpp
        src/land.cpp
        src/worldspace.cpp
        src/world_view.cpp
        src/neutral_world.cpp
        include/game_skyrimse/pack_core.hpp
        include/game_skyrimse/records.hpp
        include/game_skyrimse/actors.hpp
        include/game_skyrimse/land.hpp
        include/game_skyrimse/worldspace.hpp
        include/game_skyrimse/world_view.hpp
        include/game_skyrimse/neutral_world.hpp
        include/game_skyrimse/skyrimse_game_pack.hpp
    MANIFEST
        extension.json
)

target_include_directories(skyrimse
    PUBLIC
        include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/components
        ${CMAKE_SOURCE_DIR}/sdk/include
)

target_link_libraries(skyrimse
    PRIVATE
        components
        ZLIB::ZLIB # pack_core.cpp includes <zlib.h>
        LZ4::LZ4 # and, through components/bsa/compressedbsafile.hpp, LZ4
)

# The world store without the plug-in glue, for Eden Studio's read-only cell viewer (task 0086, world model option 1).
# Linked by csmodel; the pack library above compiles the same sources for the game.
add_library(game_skyrimse_world STATIC
    src/pack_core.cpp
    src/records.cpp
    src/actors.cpp
    src/land.cpp
    src/worldspace.cpp
    src/world_view.cpp
    src/neutral_world.cpp
)
set_target_properties(game_skyrimse_world PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(game_skyrimse_world
    PUBLIC
        include
        ${CMAKE_SOURCE_DIR}/sdk/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/components
)
target_link_libraries(game_skyrimse_world
    PUBLIC
        components
    PRIVATE
        ZLIB::ZLIB
        LZ4::LZ4
)
