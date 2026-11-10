# game_packs/skyrimse/CMakeLists.txt (task 0086): the Skyrim SE game pack skeleton

openeden_add_game_pack(skyrimse
    SOURCES
        src/skyrimse_game_pack.cpp
        src/pack_core.cpp
        src/records.cpp
        include/game_skyrimse/pack_core.hpp
        include/game_skyrimse/records.hpp
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
