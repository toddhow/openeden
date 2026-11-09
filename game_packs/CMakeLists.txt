# Game packs (decision 0013, task 0084): one game's rules, each a pack of kind "game".
include(${CMAKE_SOURCE_DIR}/cmake/OpenEdenGamePack.cmake)

if(NOT TARGET openeden_sdk)
    add_library(openeden_sdk INTERFACE)
    target_include_directories(openeden_sdk INTERFACE "${CMAKE_SOURCE_DIR}/sdk/include")
endif()

add_subdirectory(skyrimse)

# The Morrowind game pack is a static library that calls into the game (openmw-lib), so it exists only where the game does.
if(TARGET openmw-lib)
    add_subdirectory(morrowind)
endif()
