include_directories(SYSTEM ${GTEST_INCLUDE_DIRS})
include_directories(SYSTEM ${GMOCK_INCLUDE_DIRS})

file(GLOB UNITTEST_SRC_FILES
    main.cpp

    esm/testfixedstring.cpp
    esm/testrefid.cpp
    esm/variant.cpp

    lua/testasync.cpp
    lua/testconfiguration.cpp
    lua/testinputactions.cpp
    lua/testl10n.cpp
    lua/testlua.cpp
    lua/testscriptscontainer.cpp
    lua/testserialization.cpp
    lua/teststorage.cpp
    lua/testuicontent.cpp
    lua/testutilmath.cpp
    lua/testutilpackage.cpp
    lua/testyaml.cpp

    misc/compression.cpp
    misc/progressreporter.cpp
    misc/testconvert.cpp
    misc/teststats.cpp
    misc/testendianness.cpp
    misc/testmathutil.cpp
    misc/testresourcehelpers.cpp
    misc/teststringops.cpp

    nifloader/testjoltnifloader.cpp

    detournavigator/navigator.cpp
    detournavigator/settingsutils.cpp
    detournavigator/recastmeshbuilder.cpp
    detournavigator/gettilespositions.cpp
    detournavigator/recastmeshobject.cpp
    detournavigator/navmeshtilescache.cpp
    detournavigator/tilecachedrecastmeshmanager.cpp
    detournavigator/navmeshdb.cpp
    detournavigator/asyncnavmeshupdater.cpp

    serialization/binaryreader.cpp
    serialization/binarywriter.cpp
    serialization/sizeaccumulator.cpp
    serialization/integration.cpp

    settings/parser.cpp
    settings/shadermanager.cpp
    settings/testvalues.cpp

    sqlite3/db.cpp
    sqlite3/request.cpp
    sqlite3/statement.cpp
    sqlite3/transaction.cpp

    esmloader/load.cpp
    esmloader/esmdata.cpp
    esmloader/record.cpp

    files/conversiontests.cpp
    files/hash.cpp

    toutf8/toutf8.cpp

    esm4/includes.cpp

    esm3/readerscache.cpp
    esm3/testsaveload.cpp
    esm3/testesmwriter.cpp
    esm3/testinfoorder.cpp
    esm3/testcstringids.cpp

    esmterrain/testgridsampling.cpp

    esm3terrain/testalteredheights.cpp
    esm3terrain/testprojectlayers.cpp

    resource/testobjectcache.cpp
    resource/testresourcesystem.cpp
    resource/testphysicsshapemanager.cpp
    resource/colladadocuments.hpp
    resource/testassimpscene.cpp

    vsgadapters/testdaeanimation.cpp
    vsgadapters/testnifreader.cpp
    vsgadapters/testautomaps.cpp
    vsgadapters/testinternaltexture.cpp
    vsgadapters/testtangents.cpp
    vsgadapters/testparticleemission.cpp
    vsgadapters/testsoftparticlesize.cpp

    sceneutil/testdetourdebugdraw.cpp

    terrain/testlayermaps.cpp

    vfs/testpathutil.cpp

    bsa/testbsafile.cpp
    bsa/testcompressedbsafile.cpp

    nif/node.hpp
    nif/testphysics.cpp

    plugin/test_version.cpp
    plugin/test_resolution.cpp
    plugin/test_lifecycle.cpp
    plugin/test_empty_host.cpp
    plugin/test_manifest_io.cpp
    plugin/test_dynamic_library.cpp
    plugin/test_plugin_loader.cpp
    plugin/test_event_bus.cpp
    plugin/test_entity_manager.cpp
    plugin/test_settings_service.cpp
    plugin/test_save_service.cpp
    plugin/test_collision_layers.cpp
    plugin/test_vfs_service.cpp
    plugin/test_logging_service.cpp
    plugin/test_profiler_service.cpp
    plugin/test_scratch_memory.cpp
    plugin/test_debug_draw.cpp
    plugin/test_scripting_service.cpp
    plugin/test_editor_service.cpp
    plugin/test_physics_service.cpp
    inertialmotion/test_locomotion_state.cpp

    mwrender/test_creativemotion_runtime.cpp
    ${CMAKE_SOURCE_DIR}/apps/openmw/mwrender/creativemotiondriver.cpp

    vsg/testshadercompile.cpp
    vsg/testscenedata.cpp

    vsgutil/testdistancelod.cpp
    vsgutil/testarraytexture.cpp

    pipeline/testcoveragemodes.cpp

    mwanimation/testclonematerials.cpp
    vsgutil/testimagepixels.cpp
    vsgutil/testarraystate.cpp
    vsgutil/testhitnormal.cpp
    vsgutil/testdeletionqueue.cpp

    fx/lexer.cpp
    fx/technique.cpp
    fx/testtechniquedata.cpp
    math/testvectorconventions.cpp
    math/testquatconventions.cpp
    math/testmatrixconventions.cpp
    math/testboundsconventions.cpp
    math/testvsgmath.cpp
    math/testdeletedproducts.cpp
    math/testvsgmathreview.cpp
    fx/testvulkanpass.cpp
    fx/testuniformsettings.cpp
)

# The Morrowind compatibility pack's converter, renderer-free since task 0011.
list(APPEND UNITTEST_SRC_FILES
    plugin/test_compat_morrowind.cpp
    ${CMAKE_SOURCE_DIR}/plugins/compat_morrowind/src/kf_animation.cpp
    ${CMAKE_SOURCE_DIR}/plugins/compat_morrowind/src/kf_converter.cpp
    ${CMAKE_SOURCE_DIR}/plugins/compat_morrowind/src/morrowind_compat_plugin.cpp
)

source_group(apps\\components-tests FILES ${UNITTEST_SRC_FILES})

openmw_add_executable(components-tests ${UNITTEST_SRC_FILES})

target_include_directories(components-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/compat_morrowind/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/anim/gltf/include
    ${CMAKE_SOURCE_DIR}/apps/openmw
    ${CMAKE_SOURCE_DIR}/components/creativemotion/anim/runtime/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/anim/clip/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/core/rig/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/core/math/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/core/format/include
)

target_link_libraries(components-tests
    GTest::GTest
    GMock::GMock
    components
    opas_anim_gltf
    opas_anim_runtime
    opas_anim_clip
    opas_rig
    opas_math
)

if (OPENEDEN_BUILD_VSG)
    target_link_libraries(components-tests ${VSG_LIBRARIES})
    target_include_directories(components-tests PRIVATE ${VSG_INCLUDE_DIRS})
    target_compile_definitions(components-tests PRIVATE OPENEDEN_BUILD_VSG=1)
endif()

if (WIN32)
    target_sources(components-tests PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

# Fix for not visible pthreads functions for linker with glibc 2.15
if (UNIX AND NOT APPLE)
    target_link_libraries(components-tests ${CMAKE_THREAD_LIBS_INIT})
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(components-tests PRIVATE --coverage)
    target_link_libraries(components-tests gcov)
endif()

file(DOWNLOAD
    https://gitlab.com/OpenMW/example-suite/-/raw/8966dab24692555eec720c854fb0f73d108070cd/data/template.omwgame
    ${CMAKE_CURRENT_BINARY_DIR}/data/template.omwgame
    EXPECTED_HASH SHA512=6e38642bcf013c5f496a9cb0bf3ec7c9553b6e86b836e7844824c5a05f556c9391167214469b6318401684b702d7569896bf743c85aee4198612b3315ba778d6
)

target_compile_definitions(components-tests
    PRIVATE OPENMW_DATA_DIR=u8"${CMAKE_CURRENT_BINARY_DIR}/data"
            OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}")

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(components-tests PRIVATE
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

