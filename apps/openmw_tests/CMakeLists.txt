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
    mwrender/testfootik.cpp
    mwrender/testrain.cpp
    mwrender/testdistantobjectcells.cpp
    mwrender/testocclusionbox.cpp
    mwrender/testwaterripples.cpp
    mwrender/testgroundcoverinstances.cpp
    mwrender/testgroundcoverchunks.cpp
    mwrender/testswitches.cpp
    mwrender/testextentsync.cpp
    mwrender/testmapdepthrange.cpp
    mwrender/testpartcontrollers.cpp
    mwrender/testpartclocks.cpp
    mwrender/testactorautoplay.cpp
    mwrender/testnpcparts.cpp
    mwrender/testequipmentupdate.cpp
    mwrender/testitemlights.cpp
    mwrender/testnpcrebuild.cpp
    mwrender/testpartreaderrors.cpp
    mwrender/testquiverupdate.cpp
    mwrender/testthumbnailstate.cpp
    mwrender/testcreatureglow.cpp
    mwrender/testcompiledupdates.cpp
    mwrender/testattachammo.cpp
    mwrender/testplancommit.cpp
    mwrender/testmainthreadupdate.cpp
    mwrender/testpartfailures.cpp
    mwrender/testpartslots.cpp
    mwrender/testmergedbodyregions.cpp
    mwrender/testmergedbodydefines.cpp
    mwrender/testlodlayerdigest.cpp

    mwscript/testscripts.cpp
    mwscript/testragdollcommands.cpp

    # Task 0035 stage b: the RAGDOLL layer, HostedWorldConfig::gravityFactor and MWPhysics::RagdollHost.
    mwphysics/testjoltlayers.cpp
    mwphysics/testgravityfactor.cpp
    mwphysics/testragdollhost.cpp
    mwphysics/testragdollscale.cpp
    mwphysics/testragdollsleep.cpp

    extension/testextensionfailures.cpp
    extension/testextensionhost.cpp
    extension/testextensionworld.cpp
)

# Task 0063 (AdvancedBodies B1): the legacy adapter's tests need the game's store, bodyparts.cpp and bodylegacy.cpp
# (all in openmw-lib once bodylegacy.cpp is added to apps/openmw/CMakeLists.txt). The shared fixtures are compiled here too.
list(APPEND UNITTEST_SRC_FILES
    ${CMAKE_SOURCE_DIR}/apps/components_tests/body/bodyfixtures.cpp
    ${CMAKE_SOURCE_DIR}/apps/components_tests/body/testbodylegacy.cpp
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
    eden_body
)

target_compile_definitions(openmw-tests
    PRIVATE OPENEDEN_BODY_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/apps/components_tests/body/data")

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

# The extension host's tests (task 0015) load real shared libraries: one source, built once per test ID, SDK only.
# A provides the contract openeden.test.a, B requires it, C stands alone, KF requires the Morrowind pack's converter.
include(${CMAKE_SOURCE_DIR}/cmake/OpenEdenExtension.cmake)

foreach(OPENEDEN_TEST_EXTENSION_LETTER a b c kf)
    set(OPENEDEN_TEST_EXTENSION_TARGET openeden_test_extension_${OPENEDEN_TEST_EXTENSION_LETTER})
    set(OPENEDEN_TEST_EXTENSION_OUTPUT "${CMAKE_BINARY_DIR}/test_extensions/$<CONFIG>")
    openeden_add_extension(${OPENEDEN_TEST_EXTENSION_TARGET} SOURCES extension/testextension/testextension.cpp NO_INSTALL)
    # Not in the output root: the Windows install rule ships every DLL there.
    set_target_properties(${OPENEDEN_TEST_EXTENSION_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OPENEDEN_TEST_EXTENSION_OUTPUT}"
        LIBRARY_OUTPUT_DIRECTORY "${OPENEDEN_TEST_EXTENSION_OUTPUT}")
    foreach(OPENEDEN_TEST_EXTENSION_CONFIG ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${OPENEDEN_TEST_EXTENSION_CONFIG} OPENEDEN_TEST_EXTENSION_CONFIG)
        set_target_properties(${OPENEDEN_TEST_EXTENSION_TARGET} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${OPENEDEN_TEST_EXTENSION_CONFIG} "${OPENEDEN_TEST_EXTENSION_OUTPUT}"
            LIBRARY_OUTPUT_DIRECTORY_${OPENEDEN_TEST_EXTENSION_CONFIG} "${OPENEDEN_TEST_EXTENSION_OUTPUT}")
    endforeach()
    target_compile_definitions(${OPENEDEN_TEST_EXTENSION_TARGET}
        PRIVATE OPENEDEN_TEST_EXTENSION_ID="${OPENEDEN_TEST_EXTENSION_TARGET}")
    add_dependencies(openmw-tests ${OPENEDEN_TEST_EXTENSION_TARGET})

    string(TOUPPER ${OPENEDEN_TEST_EXTENSION_LETTER} OPENEDEN_TEST_EXTENSION_LETTER_UPPER)
    target_compile_definitions(openmw-tests
        PRIVATE OPENEDEN_TEST_EXTENSION_${OPENEDEN_TEST_EXTENSION_LETTER_UPPER}_PATH="$<TARGET_FILE:${OPENEDEN_TEST_EXTENSION_TARGET}>")
endforeach()

target_compile_definitions(openeden_test_extension_a PRIVATE OPENEDEN_TEST_EXTENSION_PROVIDES_A)
target_compile_definitions(openeden_test_extension_b PRIVATE OPENEDEN_TEST_EXTENSION_REQUIRES_A)
target_compile_definitions(openeden_test_extension_kf PRIVATE OPENEDEN_TEST_EXTENSION_USES_KF)

# The real Morrowind pack, for the test that calls its converter through the registry (task 0018).
add_dependencies(openmw-tests compat_morrowind)
target_compile_definitions(openmw-tests
    PRIVATE OPENEDEN_TEST_COMPAT_MORROWIND_PATH="$<TARGET_FILE:compat_morrowind>"
            OPENEDEN_TEST_COMPAT_MORROWIND_MANIFEST="${CMAKE_SOURCE_DIR}/extensions/compat_morrowind/extension.json")

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
