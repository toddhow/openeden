# cmake/OpenEdenPlugin.cmake
# OpenEden Plugin Build Macro
#
# Usage:
#   openeden_add_plugin(<id>
#       SOURCES <source1.cpp> ...
#       [MANIFEST <plugin.json>]
#   )

function(openeden_add_plugin PLUGIN_ID)
    cmake_parse_arguments(ARG "" "MANIFEST" "SOURCES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "openeden_add_plugin: No SOURCES provided for plugin '${PLUGIN_ID}'")
    endif()

    add_library(${PLUGIN_ID} SHARED ${ARG_SOURCES})

    target_compile_features(${PLUGIN_ID} PUBLIC cxx_std_20)

    # Export symbol definitions
    target_compile_definitions(${PLUGIN_ID} PRIVATE OPENEDEN_PLUGIN_EXPORT)

    # Link SDK if target exists
    if(TARGET openeden_sdk)
        target_link_libraries(${PLUGIN_ID} PRIVATE openeden_sdk)
    endif()

    # Default visibility hidden
    set_target_properties(${PLUGIN_ID} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        OUTPUT_NAME "${PLUGIN_ID}"
    )

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
endfunction()
