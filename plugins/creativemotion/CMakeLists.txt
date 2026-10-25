set(CREATIVEMOTION_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../CreativeMotion" CACHE PATH "Path to CreativeMotion repository")

if(EXISTS "${CREATIVEMOTION_SOURCE_DIR}/CMakeLists.txt")
    set(OPAS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(OPAS_BUILD_PHYSICS OFF CACHE BOOL "" FORCE)
    set(OPAS_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CREATIVEMOTION_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/creativemotion_ext" EXCLUDE_FROM_ALL)

    add_library(creativemotion_plugin_static STATIC
        host_physics_world.hpp
        host_physics_world.cpp
        creativemotion_plugin.hpp
        creativemotion_plugin.cpp
    )

    target_include_directories(creativemotion_plugin_static PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(creativemotion_plugin_static PUBLIC
        openeden_sdk
        opas::math
        opas::rig
        opas::motor
        opas::sensors
        opas::behaviors
        opas::arbiter
        opas::reconcile
        opas::format
        opas::physics_interface
    )

    target_compile_features(creativemotion_plugin_static PUBLIC cxx_std_20)

    add_library(creativemotion SHARED
        creativemotion_plugin.cpp
        host_physics_world.cpp
    )

    target_include_directories(creativemotion PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_definitions(creativemotion PRIVATE OPENEDEN_SHARED_PLUGIN)

    target_link_libraries(creativemotion PRIVATE
        openeden_sdk
        opas::math
        opas::rig
        opas::motor
        opas::sensors
        opas::behaviors
        opas::arbiter
        opas::reconcile
        opas::format
        opas::physics_interface
    )

    target_compile_features(creativemotion PRIVATE cxx_std_20)

    set_target_properties(creativemotion PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/creativemotion"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/creativemotion"
    )

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        "${CMAKE_BINARY_DIR}/plugins/creativemotion/plugin.json"
        COPYONLY
    )
endif()
