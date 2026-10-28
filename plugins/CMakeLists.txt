include(${CMAKE_SOURCE_DIR}/cmake/OpenEdenPlugin.cmake)

if(NOT TARGET openeden_sdk)
    add_library(openeden_sdk INTERFACE)
    target_include_directories(openeden_sdk INTERFACE "${CMAKE_SOURCE_DIR}/sdk/include")
endif()

add_subdirectory(compat_morrowind)
