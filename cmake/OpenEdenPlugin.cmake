# cmake/OpenEdenPlugin.cmake
# OpenEden Plugin Build Macro
#
# Usage:
#   openeden_add_plugin(<id>
#       SOURCES <source1.cpp> ...
#       [MANIFEST <plugin.json>]
#       [DATA <folder>]
#       [NO_INSTALL]
#   )
#
# The result is the layout of plugin doc 5.1, in the build folder and in an install, where the game finds it:
#   plugins/<id>/plugin.json, the library, data/, LICENSE


function(openeden_add_plugin PLUGIN_ID)
    cmake_parse_arguments(ARG "NO_INSTALL" "MANIFEST;DATA" "SOURCES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "openeden_add_plugin: No SOURCES provided for plugin '${PLUGIN_ID}'")
    endif()

    add_library(${PLUGIN_ID} SHARED ${ARG_SOURCES})

    target_compile_features(${PLUGIN_ID} PUBLIC cxx_std_20)

    # Export symbol definitions
    target_compile_definitions(${PLUGIN_ID} PRIVATE OPENEDEN_BUILDING_PLUGIN)

    # Link SDK if target exists
    if(TARGET openeden_sdk)
        target_link_libraries(${PLUGIN_ID} PRIVATE openeden_sdk)
    endif()

    # Default visibility hidden
    # The manifest names the library, so its file name must not change with the build type. Each plugin has a folder
    # of its own rather than the build folder's root, whatever the configuration.
    set(PLUGIN_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${PLUGIN_ID}")
    set_target_properties(${PLUGIN_ID} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        OUTPUT_NAME "${PLUGIN_ID}"
        DEBUG_POSTFIX ""
        RELEASE_POSTFIX ""
        RELWITHDEBINFO_POSTFIX ""
        MINSIZEREL_POSTFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_OUTPUT_DIRECTORY}"
    )
    foreach(PLUGIN_CONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${PLUGIN_CONFIG} PLUGIN_CONFIG)
        set_target_properties(${PLUGIN_ID} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${PLUGIN_CONFIG} "${PLUGIN_OUTPUT_DIRECTORY}"
            LIBRARY_OUTPUT_DIRECTORY_${PLUGIN_CONFIG} "${PLUGIN_OUTPUT_DIRECTORY}"
        )
    endforeach()

    if(WIN32)
        set_target_properties(${PLUGIN_ID} PROPERTIES PREFIX "")
    else()
        set_target_properties(${PLUGIN_ID} PROPERTIES PREFIX "lib")
    endif()

    # If manifest supplied, copy to target directory post-build
    if(ARG_MANIFEST)
        if(NOT IS_ABSOLUTE "${ARG_MANIFEST}")
            set(ARG_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_MANIFEST}")
        endif()

        if(EXISTS "${ARG_MANIFEST}")
            add_custom_command(TARGET ${PLUGIN_ID} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ARG_MANIFEST}"
                    "$<TARGET_FILE_DIR:${PLUGIN_ID}>/plugin.json"
                COMMENT "Deploying plugin manifest for '${PLUGIN_ID}'"
            )
        else()
            message(FATAL_ERROR "openeden_add_plugin: Manifest file not found: '${ARG_MANIFEST}'")
        endif()
    endif()

    # Data sits next to the library in the build folder too, so that a build runs as an install does.
    if(ARG_DATA)
        if(NOT IS_ABSOLUTE "${ARG_DATA}")
            set(ARG_DATA "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_DATA}")
        endif()
        add_custom_command(TARGET ${PLUGIN_ID} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${ARG_DATA}" "$<TARGET_FILE_DIR:${PLUGIN_ID}>/data"
            COMMENT "Deploying plugin data for '${PLUGIN_ID}'"
        )
    endif()

    if(NOT ARG_NO_INSTALL)
        set(PLUGIN_INSTALL_DIRECTORY "plugins/${PLUGIN_ID}")
        install(TARGETS ${PLUGIN_ID}
            RUNTIME DESTINATION "${PLUGIN_INSTALL_DIRECTORY}"
            LIBRARY DESTINATION "${PLUGIN_INSTALL_DIRECTORY}"
        )
        if(ARG_MANIFEST)
            install(FILES "${ARG_MANIFEST}" DESTINATION "${PLUGIN_INSTALL_DIRECTORY}" RENAME plugin.json)
        endif()
        if(ARG_DATA)
            install(DIRECTORY "${ARG_DATA}/" DESTINATION "${PLUGIN_INSTALL_DIRECTORY}/data")
        endif()
        # First-party plugins are GPL-3.0-or-later, like the engine (plugin doc 9)
        install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION "${PLUGIN_INSTALL_DIRECTORY}")
    endif()
endfunction()
