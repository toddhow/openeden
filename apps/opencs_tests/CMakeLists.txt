file(GLOB OPENCS_TESTS_SRC_FILES
    main.cpp
    model/extension/testeditorextensionhost.cpp
    model/extension/testeditorextensionhostphysics.cpp
    model/extension/testeditormodulesources.cpp
    model/module/advancedbodies/testabfill.cpp
    model/module/advancedbodies/testabhostile.cpp
    model/module/advancedbodies/testabmodule.cpp
    model/module/advancedbodies/testabview.cpp
    model/module/creativemotion/testcmdocuments.cpp
    model/module/creativemotion/testcmgizmos.cpp
    model/module/creativemotion/testcmmodule.cpp
    model/module/testheader_module.cpp
    model/module/testheader_registrar.cpp
    model/module/testheader_types.cpp
    model/module/testmodulehost.cpp
    model/module/testmoduleregistry.cpp
    model/module/testoverlaytriangles.cpp
    model/module/testsamplemodule.cpp
    model/mesh/testanimationkeys.cpp
    model/mesh/testframemath.cpp
    model/mesh/testimagesize.cpp
    model/mesh/testmeshgeometry.cpp
    model/mesh/testmeshoverlay.cpp
    model/mesh/testmeshtree.cpp
    model/mesh/testmeshviewstate.cpp
    model/mesh/testtexturereport.cpp
    model/lod/testtilemodel.cpp
    model/lod/testtilemesh.cpp
    model/lod/testworldgrid.cpp
    model/physics/testdebuglinedrawer.cpp
    model/physics/testjoltruntime.cpp
    model/physics/teststudioworld.cpp
    model/physics/teststudioworldcm.cpp
    model/prefs/testconfigmigration.cpp
    model/prefs/testcommandline.cpp
    model/prefs/testquietwindow.cpp
    model/prefs/testlayoutstore.cpp
    model/world/testinfocollection.cpp
    model/world/testuniversalid.cpp
    view/render/testcamerapose.cpp
    view/render/testmarkerpicking.cpp
    view/render/testpicking.cpp
    view/render/testcameraposereview.cpp
    view/render/testinstancemath.cpp
    view/render/testselectionmarker.cpp
    view/render/testterraintools.cpp
    view/render/testpathgridgeometry.cpp
)

source_group(apps\\eden-studio-tests FILES ${OPENCS_TESTS_SRC_FILES})

# The sample editor module's source is also linked into the tests, for the static loading tests (task 0031).
list(APPEND OPENCS_TESTS_SRC_FILES ${CMAKE_SOURCE_DIR}/sdk/examples/sample_editor_module/sample_editor_module.cpp)

openmw_add_executable(eden-studio-tests ${OPENCS_TESTS_SRC_FILES})
add_dependencies(eden-studio-tests sample_editor_module creativemotion_editor advancedbodies_editor)
target_sources(eden-studio-tests PRIVATE $<TARGET_OBJECTS:creativemotion_editor_objects>)
target_link_libraries(eden-studio-tests PRIVATE opas::rig)
target_compile_definitions(eden-studio-tests PRIVATE
    "CREATIVEMOTION_EDITOR_PATH=\"$<TARGET_FILE:creativemotion_editor>\"")
# AdvancedBodies' editor module (task 0069 stage f): its sources without the entry point, and the built library.
target_sources(eden-studio-tests PRIVATE $<TARGET_OBJECTS:advancedbodies_editor_objects>)
target_link_libraries(eden-studio-tests PRIVATE eden_body nlohmann_json::nlohmann_json)
target_compile_definitions(eden-studio-tests PRIVATE
    "ADVANCEDBODIES_EDITOR_PATH=\"$<TARGET_FILE:advancedbodies_editor>\"")
target_compile_definitions(eden-studio-tests PRIVATE
    "SAMPLE_EDITOR_MODULE_PATH=\"$<TARGET_FILE:sample_editor_module>\"")

target_include_directories(eden-studio-tests SYSTEM PRIVATE ${GTEST_INCLUDE_DIRS})
target_include_directories(eden-studio-tests SYSTEM PRIVATE ${GMOCK_INCLUDE_DIRS})

target_link_libraries(eden-studio-tests PRIVATE
    csmodel
    GTest::GTest
    GMock::GMock
)

if (WIN32)
    target_sources(eden-studio-tests PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(eden-studio-tests PRIVATE ${CMAKE_THREAD_LIBS_INIT})
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(eden-studio-tests PRIVATE --coverage)
    target_link_libraries(eden-studio-tests PRIVATE gcov)
endif()

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(eden-studio-tests REUSE_FROM components)
endif()
