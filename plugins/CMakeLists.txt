include(${CMAKE_SOURCE_DIR}/cmake/OpenEdenPlugin.cmake)

if(NOT TARGET openeden_sdk)
    add_library(openeden_sdk INTERFACE)
    target_include_directories(openeden_sdk INTERFACE "${CMAKE_SOURCE_DIR}/sdk/include")
endif()

# The Morrowind pack's .kf converter is built on NifOsg, which the VSG renderer replaced; it returns in task 0011.
if(OPENEDEN_VSG_PENDING_FEATURES)
    add_subdirectory(compat_morrowind)
endif()
