include_directories(SYSTEM ${GTEST_INCLUDE_DIRS})
include_directories(SYSTEM ${GMOCK_INCLUDE_DIRS})

file(GLOB UNITTEST_SRC_FILES
    main.cpp

    esm/testfixedstring.cpp
    esm/testrefid.cpp
    esm/variant.cpp

    lua/testasync.cpp
    lua/testconfiguration.cpp
    lua/testconfigrequires.cpp
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
    misc/testloadtrace.cpp
    misc/testbuildqueue.cpp
    misc/testmathutil.cpp
    misc/testquietwindow.cpp
    misc/testgameconventions.cpp
    misc/testresourcehelpers.cpp
    misc/teststringops.cpp

    crashcatcher/testfreezeresponse.cpp

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
    files/testdatapack.cpp

    toutf8/toutf8.cpp

    esm4/includes.cpp

    esm3/readerscache.cpp
    esm3/testsaveload.cpp
    esm3/testsavedgamepack.cpp
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
    vsgadapters/testmaterialmaps.cpp
    vsgadapters/testrmaosmaps.cpp
    vsgadapters/testinternaltexture.cpp
    vsgadapters/testtangents.cpp
    vsgadapters/testparticleemission.cpp
    vsgadapters/testparticleemitterindex.cpp
    vsgadapters/testparticlelink.cpp
    vsgadapters/testsoftparticlesize.cpp

    sceneutil/testdetourdebugdraw.cpp

    terrain/testbatchindex.cpp
    terrain/testbordersharing.cpp
    terrain/testchunkcomplement.cpp
    terrain/testchunkfade.cpp
    terrain/testchunkfadeinstance.cpp
    terrain/testheightranges.cpp
    terrain/testchunkinstancedecode.cpp
    terrain/testcoverage.cpp
    terrain/testindexstitch.cpp
    terrain/testlayermaps.cpp
    terrain/testlayershare.cpp
    terrain/testlayerrmaos.cpp
    terrain/testterrainfade.cpp

    vfs/testpathutil.cpp
    vfs/testvfscharacterisation.cpp
    vfs/testvfsnormalization.cpp
    vfs/testvfsoverrides.cpp
    vfs/testvfsrange.cpp
    vfs/testedenarchive.cpp
    vfs/testvfsthreads.cpp
    vfs/testvfsarchivekinds.cpp
    vfs/testvfsfactorythreads.cpp
    vfs/testvfsentryname.cpp
    vfs/testvfsservicetraversal.cpp
    vfs/testvfspackplacement.cpp

    archivekinds/testarchivekinds.cpp

    bsa/testbsafile.cpp
    bsa/testcompressedbsafile.cpp
    compiler/testmorrowindprogramgolden.cpp
    compiler/testparseroptions.cpp
    compiler/testlogicaloperators.cpp
    compiler/testformvalues.cpp
    compiler/testskippedarguments.cpp
    compiler/testdottednames.cpp
    compiler/testcallresultreference.cpp
    interpreter/testrunlimits.cpp

    # Task 0092: the ObScript runtime of the common compatibility pack (generated scripts, an invented world).
    script_obscript/testobscriptcontract.cpp
    script_obscript/testobscriptrun.cpp
    script_obscript/testobscriptcompile.cpp
    script_obscript/testobscriptbudget.cpp
    script_obscript/testobscriptsave.cpp
    script_obscript/testobscriptlogic.cpp
    script_obscript/testobscriptforms.cpp
    script_obscript/testobscriptderive.cpp
    script_obscript/testobscriptdeclare.cpp
    script_obscript/testobscriptlenient.cpp
    script_obscript/testobscriptgamepack.cpp
    script_obscript/testobscripttablecheck.cpp
    script_obscript/testobscriptrealdata.cpp
    script_obscript/testobscriptreview.cpp
    script_obscript/testobscriptcallref.cpp
    script_obscript/testobscriptrestoredetails.cpp

    # Task 0093: the Papyrus runtime of the common compatibility pack (generated .pex files from the test assembler).
    papyrus/testpapyruscontract.cpp
    papyrus/testpapyrusassembler.cpp
    papyrus/testpapyrusreader.cpp
    papyrus/testpapyrusfuzz.cpp
    papyrus/testpapyrusload.cpp
    papyrus/testpapyrusrealdata.cpp
    papyrus/testpapyrusvmcore.cpp
    papyrus/testpapyrusvmnatives.cpp
    papyrus/testpapyrusvmdata.cpp
    papyrus/testpapyrusvmfuzz.cpp
    papyrus/testpapyrusrealrun.cpp
    papyrus/testpapyrusreview.cpp
    papyrus/testpapyrusreview2.cpp
    papyrus/testpapyrussched.cpp
    papyrus/testpapyrussave.cpp
    papyrus/testpapyrusnatives.cpp
    papyrus/testrestoreresultabi.cpp
    papyrus/testpapyrusgamepack.cpp

    nif/node.hpp
    nif/testphysics.cpp
    nif/nifbuilder.hpp
    nif/testmorrowindunchanged.cpp
    nif/testreadoptions.cpp
    nif/testdefaultoptions.cpp

    contentsource/pluginbuilder.hpp
    contentsource/testplugin.cpp
    contentsource/testprobefamily.cpp
    contentsource/testdecoderregistry.cpp

    modorganizer/tempfolder.hpp
    modorganizer/testmodel.cpp
    modorganizer/testscanner.cpp
    modorganizer/testlayout.cpp
    modorganizer/testconfigfile.cpp
    modorganizer/testconflicts.cpp
    modorganizer/testmasters.cpp
    modorganizer/testrepack.cpp
    worldlod/testbakestatus.cpp

    nifedit/nifeditbuilder.hpp
    nifedit/nifeditsupport.hpp
    nifedit/testnifindex.cpp
    nifedit/testnifpatch.cpp
    nifedit/testnifdocument.cpp
    nifedit/testnifeditreal.cpp

    nifscene/ssebuilder.hpp
    nifscene/shaderbuilder.hpp
    nifscene/testscenebuild.cpp
    nifscene/testscenematerial.cpp
    nifscene/testscenemorrowindskin.cpp
    nifscene/testscenereal.cpp
    nifscene/testscenerealmaterials.cpp
    nifscene/testvertexdecode.cpp
    nifscene/testscenereview.cpp

    extension/test_version.cpp
    extension/test_resolution.cpp
    extension/test_lifecycle.cpp
    extension/test_empty_host.cpp
    extension/test_manifest_editor_entry.cpp
    extension/test_manifest_editor_library.cpp
    extension/test_manifest_io.cpp
    extension/test_dynamic_library.cpp
    extension/test_extension_loader.cpp
    extension/test_event_bus.cpp
    extension/test_entity_manager.cpp
    extension/test_settings_service.cpp
    extension/test_vfs_service.cpp
    extension/test_logging_service.cpp
    extension/test_profiler_service.cpp
    extension/test_scratch_memory.cpp
    extension/test_debug_draw.cpp
    extension/test_physics_service.cpp
    extension/test_game_pack_manifest.cpp
    extension/test_game_pack_lifecycle.cpp
    extension/test_game_pack_save.cpp
    extension/test_crafting_contract.cpp
    inertialmotion/test_locomotion_state.cpp
    inertialmotion/testleansmoother.cpp
    inertialmotion/testmovementrequest.cpp

    mwrender/test_creativemotion_runtime.cpp
    mwrender/test_creativemotion_style.cpp
    mwrender/test_creativemotion_intent.cpp
    mwrender/test_creativemotion_style_driver.cpp
    mwrender/test_game_conventions_sites.cpp
    ${CMAKE_SOURCE_DIR}/apps/openmw/mwrender/creativemotionstyle.cpp
    ${CMAKE_SOURCE_DIR}/apps/openmw/mwrender/creativemotiondriver.cpp

    vsg/testshadercompile.cpp
    vsg/testslangshaders.cpp
    vsg/testshaderdefines.cpp
    vsg/testshaderpermutation.cpp
    vsg/testslanginterfacerecords.cpp
    vsg/testslangpermutations.cpp
    vsg/testspirvinterface.cpp
    vsg/testpassstages.cpp
    vsg/testomwfxvertex.cpp
    vsg/testslangruntime.cpp
    vsg/testpostpasses.cpp

    render/testtonemapconstants.cpp
    render/testaerialmath.cpp
    render/testwatermath.cpp
    render/testgputiming.cpp
    render/testhdrmath.cpp
    render/testtonemap.cpp
    render/testvelocity.cpp
    render/testtaamath.cpp
    render/testtaapass.cpp
    render/testaotemporalmath.cpp
    render/testaotemporalpass.cpp
    render/testpointshadowmath.cpp
    render/testpointshadowselect.cpp
    render/testpointshadowcache.cpp
    render/testpointshadowgpu.cpp
    render/testvram.cpp
    render/testvramscaler.cpp
    render/testvramtexturemips.cpp
    render/testvrampoolrelease.cpp
    render/testpointshadowshader.cpp
    render/testpointshadowmemory.cpp
    render/testgbuffermath.cpp
    render/testdepthmath.cpp
    render/testmaterialmath.cpp
    render/testmaterialoverrides.cpp
    render/testeyeskin.cpp
    render/testtableelevation.cpp
    render/testproceduralsun.cpp
    body/testmergedruntime.cpp
    render/testcirrusmath.cpp
    render/testrmaos.cpp
    render/testprobegpu.cpp
    render/testprobebake.cpp
    render/testao.cpp
    render/testaopass.cpp
    render/testaoterrain.cpp
    render/testvolumemath.cpp
    render/testvolumegpu.cpp
    render/testvolumecamera.cpp
    render/testatmosphere.cpp
    render/testsss.cpp
    render/testwetnessmath.cpp
    render/testcloudmath.cpp
    render/testcloudvolumemath.cpp
    render/testbluenoise.cpp
    render/testcloudbicubic.cpp
    render/testcloudcurved.cpp
    render/testcloudvolumegpu.cpp
    render/testcovermath.cpp
    render/testlightningmath.cpp
    render/testsunglow.cpp
    render/testprecipitationmath.cpp
    render/testparallaxdata.cpp
    render/testparallaxmath.cpp
    render/testparallaxray.cpp
    render/testssspass.cpp
    render/testprobes.cpp
    render/testreflectionprobes.cpp
    render/testskyvis.cpp
    render/testskyvisbake.cpp
    render/testskyvisgpu.cpp
    render/testskyenv.cpp
    render/testskyvisdebug.cpp
    view/testcollectlights.cpp
    view/testshadowview.cpp
    render/testdevicereport.cpp
    render/testpipelinecache.cpp
    render/testlodmeasure.cpp
    render/testscreenshotnames.cpp
    render/testframecapture.cpp
    lod/testtilekey.cpp
    lod/testtileselection.cpp
    lod/testterraintile.cpp
    lod/testtilepack.cpp
    lod/testtilebake.cpp
    lod/testtexturedecode.cpp
    lod/testalbedobake.cpp
    lod/testobjectcluster.cpp
    lod/testclusterbuild.cpp
    lod/testclusterreview.cpp
    lod/testclusterfade.cpp
    lod/testtileswap.cpp
    lod/testterrainfade.cpp
    lod/testterrainfadecomplement.cpp
    lod/testclusterplan.cpp
    lod/testclusterfoliage.cpp
    lod/testnifflatten.cpp
    lod/testclustereffects.cpp
    lod/testclusterpack.cpp
    lod/testtiletree.cpp
    lod/testtilelivefallback.cpp
    lod/testtileoffview.cpp
    lod/testtilebakelevels.cpp
    lod/testviewlimits.cpp
    lod/testfartier.cpp
    lod/testresidency.cpp
    bodynif/testskinnedshape.cpp
    bodynif/testseamanalysis.cpp
    bodynif/testanalysisreport.cpp
    vsg/testscenedata.cpp

    vsgutil/testdistancelod.cpp
    vsgutil/testarraytexture.cpp

    pipeline/testcoveragemodes.cpp
    pipeline/testgbufferblend.cpp
    pipeline/testgbufferstates.cpp
    pipeline/testoverlaydepthbias.cpp
    pipeline/testterraindefines.cpp

    mwanimation/testclonematerials.cpp
    mwanimation/testskinprevious.cpp
    mwanimation/testlightcontroller.cpp
    mwanimation/testpointshadowcarrier.cpp
    vsgutil/testimagepixels.cpp
    vsgutil/testdeferredbin.cpp
    vsgutil/testarraystate.cpp
    vsgutil/testhitnormal.cpp
    vsgutil/testhitnormalmesh.cpp
    vsgutil/testdeletionqueue.cpp
    vsgutil/testdeletionqueuethreads.cpp
    vsgutil/testremovesrgb.cpp
    vsgutil/testtexturecap.cpp
    vsgutil/testtextureresize.cpp
    vsgutil/testtexturemips.cpp
    vsgutil/testtexturearrays.cpp
    vsgutil/testtexturecapread.cpp
    vsgutil/testtexturecaparray.cpp
    vsgutil/testarraylayers.cpp
    vsgutil/testarrayreencode.cpp
    vsgutil/testbc1encode.cpp
    vsgutil/testbc4.cpp
    vsgutil/testbc4array.cpp
    vsgutil/testbc7decode.cpp
    vsgutil/testblockconvert.cpp
    vsgutil/testrgbaresize.cpp

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
    extension/test_compat_morrowind.cpp
    extension/test_compat_morrowind_scene.cpp
    ${CMAKE_SOURCE_DIR}/extensions/compat_morrowind/src/scene_options.cpp
    ${CMAKE_SOURCE_DIR}/extensions/compat_morrowind/src/kf_animation.cpp
    ${CMAKE_SOURCE_DIR}/extensions/compat_morrowind/src/kf_converter.cpp
    ${CMAKE_SOURCE_DIR}/extensions/compat_morrowind/src/morrowind_compat_extension.cpp
)

# Task 0086: the Skyrim SE compatibility pack and game pack skeleton (generated plugins, archives and NIFs only; the
# install-in-place test is skipped unless OPENEDEN_SKYRIMSE_DATA names a Data folder). The packs' own entry points are
# left out because a test executable has one openeden_extension_entry already.
list(APPEND UNITTEST_SRC_FILES
    skyrimse/bsabuilder.hpp
    skyrimse/testbsahardening.cpp
    skyrimse/testcompatskyrimse.cpp
    skyrimse/testskyrimsepack.cpp
    skyrimse/testskyrimseplugins.cpp
    skyrimse/testskyrimsearchives.cpp
    skyrimse/testskyrimserealdata.cpp
    ${CMAKE_SOURCE_DIR}/extensions/compat_skyrimse/src/scene_options.cpp
    ${CMAKE_SOURCE_DIR}/extensions/compat_skyrimse/src/skyrimse_compat_extension.cpp
    ${CMAKE_SOURCE_DIR}/game_packs/skyrimse/src/pack_core.cpp
    ${CMAKE_SOURCE_DIR}/game_packs/skyrimse/src/skyrimse_game_pack.cpp
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/extensions/compat_skyrimse/src/skyrimse_compat_extension.cpp
    ${CMAKE_SOURCE_DIR}/game_packs/skyrimse/src/skyrimse_game_pack.cpp
    PROPERTIES COMPILE_DEFINITIONS OPENEDEN_SKYRIMSE_NO_ENTRY)

# Task 0035 stage a: CMPhysics::HostedJoltWorld (CreativeMotion's PhysicsWorld over the host's Jolt system).
# testhostedjoltallocation.cpp replaces the global operator new to count allocations (see the comment in that file).
list(APPEND UNITTEST_SRC_FILES
    cmphysics/testhostedjoltworld.cpp
    cmphysics/testhostedjoltallocation.cpp
    cmphysics/testhostedjoltragdoll.cpp
    cmphysics/testhostedjoltscale.cpp
)

# Task 0063 (AdvancedBodies B1): the body contract (JSON, validator, packed form, tool) and its conformance suite.
# The legacy adapter's tests (body/testbodylegacy.cpp) need the game's store and are built in openmw-tests.
list(APPEND UNITTEST_SRC_FILES
    body/bodyfixtures.cpp
    body/testbodyjson.cpp
    body/testbodyvalidate.cpp
    body/testbodypack.cpp
    body/testbodytool.cpp
    body/testbodyconformance.cpp
    body/testbodymerge.cpp
    body/testbodymesh.cpp
    body/testskeletonmap.cpp
    body/testskeletonsuggest.cpp
    body/testskeletonmapping.cpp
    body/testskeletonmapreview.cpp
    body/testmergedruntime.cpp
    body/testmergedbodyreview.cpp
    body/testmergedregions.cpp
)

# Task 0071 (facial speech animation): opas.facecurves, importers and the sampler (eden_face, no dependencies).
list(APPEND UNITTEST_SRC_FILES
    face/testfacecurves.cpp
    face/testfacesample.cpp
)

# Task 0067 stage 0: the maths and the parser of the CreativeMotion transition measurement.
list(APPEND UNITTEST_SRC_FILES
    animtrace/testtracechanges.cpp
    animtrace/testtracemetrics.cpp
    animtrace/testtraceparse.cpp
    animtrace/testtracetool.cpp
)

# Task 0070 stage a: the measurement of extended skeletons (skeleton and skin weight reading, the aggregates) and the
# two synthetic checks of the engine (a skin bone the skeleton lacks, the bone groups of unknown bones).
list(APPEND UNITTEST_SRC_FILES
    bodynif/testskeletonread.cpp
    body/testskeletonstats.cpp
    skelstat/testmissingskinbone.cpp
    skelstat/testbonegroups.cpp
)

source_group(apps\\components-tests FILES ${UNITTEST_SRC_FILES})

openmw_add_executable(components-tests ${UNITTEST_SRC_FILES})

target_include_directories(components-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/extensions/compat_morrowind/include
    ${CMAKE_SOURCE_DIR}/extensions/compat_skyrimse/include
    ${CMAKE_SOURCE_DIR}/game_packs/skyrimse/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/anim/gltf/include
    ${CMAKE_SOURCE_DIR}/apps/openmw
    ${CMAKE_SOURCE_DIR}/components/creativemotion/anim/runtime/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/anim/style/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/anim/clip/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/core/rig/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/core/math/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/core/format/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/core/motor/include
    ${CMAKE_SOURCE_DIR}/components/creativemotion/physics/interface/include
)

target_link_libraries(components-tests
    GTest::GTest
    GMock::GMock
    components
    edenarchive-testsupport
    opas_anim_gltf
    opas_anim_runtime
    opas_anim_style
    opas_anim_clip
    opas_rig
    opas_math
    opas_motor
    eden_body
    eden_face
    eden_animtrace
    # the compile table of Morrowind's script commands (task 0098 move 4), for the golden compile recording
    game_morrowind_script_table
)

# Task 0063: where the hand-written body.json fixtures live.
target_compile_definitions(components-tests PRIVATE
    OPENEDEN_BODY_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/apps/components_tests/body/data")

target_link_libraries(components-tests ${VSG_LIBRARIES})
target_include_directories(components-tests PRIVATE ${VSG_INCLUDE_DIRS})
# The build is always VSG's now. Committed tests still test this name under #if, so it stays defined.
target_compile_definitions(components-tests PRIVATE OPENEDEN_BUILD_VSG=1)
# Task 0023: the Slang sun shader test compares the built SPIR-V with the GLSL it replaces.
target_compile_definitions(components-tests PRIVATE
    OPENEDEN_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/files/shaders"
    OPENEDEN_SHADER_BUILD_DIR="${OPENMW_RESOURCES_ROOT}/resources/shaders"
    OPENEDEN_SLANG_ENABLED=$<BOOL:${OPENEDEN_SLANG_SHADERS_BUILT}>)
if (OPENEDEN_SLANG_SHADERS_BUILT)
    add_dependencies(components-tests openeden_slang_shaders)
endif()
# Task 0072, stage h: the tests of the Slang library in the game need it, and the .slang sources, in the build's resources.
if (OPENEDEN_SLANGC AND OPENEDEN_SLANG_SHADERS_BUILT)
    target_compile_definitions(components-tests PRIVATE
        OPENEDEN_SLANG_LIBRARY_DIR="${OPENMW_RESOURCES_ROOT}/resources/slang"
        OPENEDEN_SLANG_RUNTIME_TESTS=1)
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

