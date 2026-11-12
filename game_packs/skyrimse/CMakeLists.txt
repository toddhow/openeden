# game_packs/skyrimse/CMakeLists.txt (task 0086): the Skyrim SE game pack skeleton

openeden_add_game_pack(skyrimse
    SOURCES
        src/skyrimse_game_pack.cpp
        src/pack_core.cpp
        src/records.cpp
        src/actors.cpp
        src/dialogue.cpp
        src/quest.cpp
        src/dialogue_runtime.cpp
        src/voice.cpp
        src/appearance.cpp
        src/land.cpp
        src/worldspace.cpp
        src/weather.cpp
        src/weather_source.cpp
        src/terrain_source.cpp
        src/world_view.cpp
        src/neutral_world.cpp
        src/survey.cpp
        src/survey_scripts.cpp
        src/survey_nif.cpp
        src/survey_names.cpp
        src/survey_names_xedit.cpp
        src/conditions.cpp
        include/game_skyrimse/survey.hpp
        include/game_skyrimse/conditions.hpp
        include/game_skyrimse/survey_scripts.hpp
        include/game_skyrimse/survey_nif.hpp
        include/game_skyrimse/pack_core.hpp
        include/game_skyrimse/records.hpp
        include/game_skyrimse/actors.hpp
        include/game_skyrimse/dialogue.hpp
        include/game_skyrimse/quest.hpp
        include/game_skyrimse/dialogue_runtime.hpp
        include/game_skyrimse/voice.hpp
        include/game_skyrimse/appearance.hpp
        include/game_skyrimse/land.hpp
        include/game_skyrimse/worldspace.hpp
        include/game_skyrimse/weather.hpp
        include/game_skyrimse/weather_source.hpp
        include/game_skyrimse/terrain_source.hpp
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
    src/dialogue.cpp
    src/quest.cpp
    src/dialogue_runtime.cpp
    src/voice.cpp
    src/appearance.cpp
    src/land.cpp
    src/worldspace.cpp
    src/weather.cpp
    src/weather_source.cpp
    src/terrain_source.cpp
    src/world_view.cpp
    src/neutral_world.cpp
    src/survey.cpp
    src/survey_scripts.cpp
    src/survey_nif.cpp
    src/survey_names.cpp
    src/survey_names_xedit.cpp
    src/conditions.cpp
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
