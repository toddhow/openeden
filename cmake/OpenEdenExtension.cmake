# cmake/OpenEdenExtension.cmake
# OpenEden Extension Build Macro
#
# Usage:
#   openeden_add_extension(<id>
#       SOURCES <source1.cpp> ...
#       [MANIFEST <extension.json>]
#       [DATA <folder>]
#       [GROUP <folder>]
#       [NO_INSTALL]
#   )
#
# GROUP names the folder that holds the pack: "extensions" (the default) or "game_packs" (openeden_add_game_pack(), task
# 0084).
#
# The result is the layout of extension doc 5.1, in the build folder and in an install, where the game finds it:
#   extensions/<id>/extension.json, the library, data/, LICENSE


function(openeden_add_extension EXTENSION_ID)
    cmake_parse_arguments(ARG "NO_INSTALL" "MANIFEST;DATA;GROUP" "SOURCES" ${ARGN})
    if(NOT ARG_GROUP)
        set(ARG_GROUP "extensions")
    endif()

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "openeden_add_extension: No SOURCES provided for extension '${EXTENSION_ID}'")
    endif()

    add_library(${EXTENSION_ID} SHARED ${ARG_SOURCES})

    target_compile_features(${EXTENSION_ID} PUBLIC cxx_std_20)

    # Export symbol definitions
    target_compile_definitions(${EXTENSION_ID} PRIVATE OPENEDEN_BUILDING_EXTENSION)

    # Link SDK if target exists
    if(TARGET openeden_sdk)
        target_link_libraries(${EXTENSION_ID} PRIVATE openeden_sdk)
    endif()

    # Default visibility hidden
    # The manifest names the library, so its file name must not change with the build type. Each extension has a folder
    # of its own rather than the build folder's root, whatever the configuration.
    set(EXTENSION_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${ARG_GROUP}/${EXTENSION_ID}")
    set_target_properties(${EXTENSION_ID} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        OUTPUT_NAME "${EXTENSION_ID}"
        DEBUG_POSTFIX ""
        RELEASE_POSTFIX ""
        RELWITHDEBINFO_POSTFIX ""
        MINSIZEREL_POSTFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${EXTENSION_OUTPUT_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY "${EXTENSION_OUTPUT_DIRECTORY}"
    )
    foreach(EXTENSION_CONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${EXTENSION_CONFIG} EXTENSION_CONFIG)
        set_target_properties(${EXTENSION_ID} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${EXTENSION_CONFIG} "${EXTENSION_OUTPUT_DIRECTORY}"
            LIBRARY_OUTPUT_DIRECTORY_${EXTENSION_CONFIG} "${EXTENSION_OUTPUT_DIRECTORY}"
        )
    endforeach()

    if(WIN32)
        set_target_properties(${EXTENSION_ID} PROPERTIES PREFIX "")
    else()
        set_target_properties(${EXTENSION_ID} PROPERTIES PREFIX "lib")
    endif()

    # If manifest supplied, copy to target directory post-build
    if(ARG_MANIFEST)
        if(NOT IS_ABSOLUTE "${ARG_MANIFEST}")
            set(ARG_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_MANIFEST}")
        endif()

        if(EXISTS "${ARG_MANIFEST}")
            add_custom_command(TARGET ${EXTENSION_ID} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ARG_MANIFEST}"
                    "$<TARGET_FILE_DIR:${EXTENSION_ID}>/extension.json"
                COMMENT "Deploying extension manifest for '${EXTENSION_ID}'"
            )
        else()
            message(FATAL_ERROR "openeden_add_extension: Manifest file not found: '${ARG_MANIFEST}'")
        endif()
    endif()

    # Data sits next to the library in the build folder too, so that a build runs as an install does.
    if(ARG_DATA)
        if(NOT IS_ABSOLUTE "${ARG_DATA}")
            set(ARG_DATA "${CMAKE_CURRENT_SOURCE_DIR}/${ARG_DATA}")
        endif()
        add_custom_command(TARGET ${EXTENSION_ID} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${ARG_DATA}" "$<TARGET_FILE_DIR:${EXTENSION_ID}>/data"
            COMMENT "Deploying extension data for '${EXTENSION_ID}'"
        )
    endif()

    if(NOT ARG_NO_INSTALL)
        set(EXTENSION_INSTALL_DIRECTORY "${ARG_GROUP}/${EXTENSION_ID}")
        install(TARGETS ${EXTENSION_ID}
            RUNTIME DESTINATION "${EXTENSION_INSTALL_DIRECTORY}"
            LIBRARY DESTINATION "${EXTENSION_INSTALL_DIRECTORY}"
        )
        if(ARG_MANIFEST)
            install(FILES "${ARG_MANIFEST}" DESTINATION "${EXTENSION_INSTALL_DIRECTORY}" RENAME extension.json)
        endif()
        if(ARG_DATA)
            install(DIRECTORY "${ARG_DATA}/" DESTINATION "${EXTENSION_INSTALL_DIRECTORY}/data")
        endif()
        # First-party extensions are GPL-3.0-or-later, like the engine (extension doc 9)
        install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION "${EXTENSION_INSTALL_DIRECTORY}")
    endif()
endfunction()
