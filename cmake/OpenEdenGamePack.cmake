# cmake/OpenEdenGamePack.cmake
# OpenEden game pack build macro (task 0084, decision 0013)
#
# Usage:
#   openeden_add_game_pack(<id>
#       SOURCES <source1.cpp> ...
#       [STATIC]                 # linked into the game through the same Registrar, no manifest, no library to load
#       [MANIFEST <extension.json>]   # dynamic packs: manifest format 2, "kind": "game"
#       [DATA <folder>]
#       [NO_INSTALL]
#   )
#
# A dynamic pack is an ordinary shared library built against the SDK only, laid out as game_packs/<id>/extension.json,
# the library, data/, LICENSE (docs/design/game-packs.md section 4). A static pack is a static library target <id> that
# the game executable links; it registers itself through the pack's own factory function (task 0085).

include(${CMAKE_CURRENT_LIST_DIR}/OpenEdenExtension.cmake)

function(openeden_add_game_pack GAME_PACK_ID)
    cmake_parse_arguments(ARG "STATIC;NO_INSTALL" "MANIFEST;DATA" "SOURCES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "openeden_add_game_pack: No SOURCES provided for game pack '${GAME_PACK_ID}'")
    endif()

    if(ARG_STATIC)
        add_library(${GAME_PACK_ID} STATIC ${ARG_SOURCES})
        target_compile_features(${GAME_PACK_ID} PUBLIC cxx_std_20)
        if(TARGET openeden_sdk)
            target_link_libraries(${GAME_PACK_ID} PUBLIC openeden_sdk)
        endif()
        set_target_properties(${GAME_PACK_ID} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        return()
    endif()

    set(FORWARD SOURCES ${ARG_SOURCES} GROUP game_packs)
    if(ARG_MANIFEST)
        list(APPEND FORWARD MANIFEST ${ARG_MANIFEST})
    endif()
    if(ARG_DATA)
        list(APPEND FORWARD DATA ${ARG_DATA})
    endif()
    if(ARG_NO_INSTALL)
        list(APPEND FORWARD NO_INSTALL)
    endif()
    openeden_add_extension(${GAME_PACK_ID} ${FORWARD})
endfunction()
