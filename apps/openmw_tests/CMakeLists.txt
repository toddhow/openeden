include_directories(SYSTEM ${GTEST_INCLUDE_DIRS})
include_directories(SYSTEM ${GMOCK_INCLUDE_DIRS})

file(GLOB UNITTEST_SRC_FILES
    main.cpp

    options.cpp
    test_vsg_v1_invariants.cpp

    mwworld/teststore.cpp
    mwworld/testduration.cpp
    mwworld/testtimestamp.cpp
    mwworld/testptr.cpp
    mwworld/testweather.cpp

    mwdialogue/testkeywordsearch.cpp

    mwgui/tooltips.cpp
    mwgui/weightedsearch.cpp

    mwrender/testbonepose.cpp
    mwrender/testrain.cpp
    mwrender/testwaterripples.cpp
    mwrender/testgroundcoverinstances.cpp
    mwrender/testgroundcoverchunks.cpp
    mwrender/testswitches.cpp

    mwscript/testscripts.cpp

    plugin/testpluginhost.cpp
)

if (MSVC)
    # these TUs take a lot of memory for codegen
    # in CI, they often go over the 8GB memory limit if another cl.exe is running at the same time
    # this option (make sure you're not looking at the docs for the identically-named linker option) stops four codegen threads being spun off
    # it hurts build time ~33% to enable this unnecessarily, though, so target it to the hungriest boys
    set_source_files_properties(
        mwworld/teststore.cpp
        PROPERTIES
        COMPILE_OPTIONS /cgthreads1
    )
endif()

source_group(apps\\openmw-tests FILES ${UNITTEST_SRC_FILES})

openmw_add_executable(openmw-tests ${UNITTEST_SRC_FILES})

target_link_libraries(openmw-tests
    GTest::GTest
    GMock::GMock
    openmw-lib
)

# Fix for not visible pthreads functions for linker with glibc 2.15
if (UNIX AND NOT APPLE)
    target_link_libraries(openmw-tests ${CMAKE_THREAD_LIBS_INIT})
endif()

if (WIN32)
    target_sources(openmw-tests PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-tests PRIVATE --coverage)
    target_link_libraries(openmw-tests gcov)
endif()

target_compile_definitions(openmw-tests
    PRIVATE OPENMW_DATA_DIR=u8"${CMAKE_CURRENT_BINARY_DIR}/data"
            OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}")

# The plugin host's tests (task 0015) load real shared libraries: one source, built once per test ID, SDK only.
# A provides the contract openeden.test.a, B requires it, C stands alone.
include(${CMAKE_SOURCE_DIR}/cmake/OpenEdenPlugin.cmake)

foreach(OPENEDEN_TEST_PLUGIN_LETTER a b c)
    set(OPENEDEN_TEST_PLUGIN_TARGET openeden_test_plugin_${OPENEDEN_TEST_PLUGIN_LETTER})
    set(OPENEDEN_TEST_PLUGIN_OUTPUT "${CMAKE_BINARY_DIR}/test_plugins/$<CONFIG>")
    openeden_add_plugin(${OPENEDEN_TEST_PLUGIN_TARGET} SOURCES plugin/testplugin/testplugin.cpp)
    # Not in the output root: the Windows install rule ships every DLL there.
    set_target_properties(${OPENEDEN_TEST_PLUGIN_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OPENEDEN_TEST_PLUGIN_OUTPUT}"
        LIBRARY_OUTPUT_DIRECTORY "${OPENEDEN_TEST_PLUGIN_OUTPUT}")
    foreach(OPENEDEN_TEST_PLUGIN_CONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${OPENEDEN_TEST_PLUGIN_CONFIG} OPENEDEN_TEST_PLUGIN_CONFIG)
        set_target_properties(${OPENEDEN_TEST_PLUGIN_TARGET} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${OPENEDEN_TEST_PLUGIN_CONFIG} "${OPENEDEN_TEST_PLUGIN_OUTPUT}"
            LIBRARY_OUTPUT_DIRECTORY_${OPENEDEN_TEST_PLUGIN_CONFIG} "${OPENEDEN_TEST_PLUGIN_OUTPUT}")
    endforeach()
    target_compile_definitions(${OPENEDEN_TEST_PLUGIN_TARGET}
        PRIVATE OPENEDEN_TEST_PLUGIN_ID="${OPENEDEN_TEST_PLUGIN_TARGET}")
    add_dependencies(openmw-tests ${OPENEDEN_TEST_PLUGIN_TARGET})

    string(TOUPPER ${OPENEDEN_TEST_PLUGIN_LETTER} OPENEDEN_TEST_PLUGIN_LETTER_UPPER)
    target_compile_definitions(openmw-tests
        PRIVATE OPENEDEN_TEST_PLUGIN_${OPENEDEN_TEST_PLUGIN_LETTER_UPPER}_PATH="$<TARGET_FILE:${OPENEDEN_TEST_PLUGIN_TARGET}>")
endforeach()

target_compile_definitions(openeden_test_plugin_a PRIVATE OPENEDEN_TEST_PLUGIN_PROVIDES_A)
target_compile_definitions(openeden_test_plugin_b PRIVATE OPENEDEN_TEST_PLUGIN_REQUIRES_A)

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw-tests PRIVATE
        <boost/program_options/options_description.hpp>

        <gtest/gtest.h>

        <sol/sol.hpp>

        <algorithm>
        <filesystem>
        <fstream>
        <functional>
        <memory>
        <string>
        <vector>
    )
endif()
