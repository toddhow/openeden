include(${CMAKE_SOURCE_DIR}/cmake/OpenEdenPlugin.cmake)

if(NOT TARGET openeden_sdk)
    add_library(openeden_sdk INTERFACE)
    target_include_directories(openeden_sdk INTERFACE "${CMAKE_SOURCE_DIR}/sdk/include")
endif()

# Plugins are shared libraries that link the static components library (the pack reads Nif records), so on Linux
# components must be position independent.
set_property(TARGET components PROPERTY POSITION_INDEPENDENT_CODE ON)

add_subdirectory(compat_morrowind)
add_subdirectory(example)
