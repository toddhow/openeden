file(GLOB OPENCS_TESTS_SRC_FILES
    main.cpp
    model/doc/testundorouting.cpp
    model/extension/testeditorextensionhost.cpp
    model/extension/testeditorextensionhostphysics.cpp
    model/extension/testeditormodulesources.cpp
    model/module/advancedbodies/testabfill.cpp
    model/module/advancedbodies/testabhostile.cpp
    model/module/advancedbodies/testabmodule.cpp
    model/module/advancedbodies/testabuserdata.cpp
    model/module/advancedbodies/testabview.cpp
    model/module/creativemotion/testcmdocuments.cpp
    model/module/creativemotion/testcmgizmos.cpp
    model/module/creativemotion/testcmmodule.cpp
    model/module/creativemotion/testcmselection.cpp
    model/module/creativemotion/testcmhandles.cpp
    model/module/creativemotion/testcmsandbox.cpp
    model/module/testheader_module.cpp
    model/module/testheader_registrar.cpp
    model/module/testapi12.cpp
    model/module/testapi13.cpp
    model/module/testeventbus.cpp
    model/module/testoverlaypick.cpp
    model/module/testoverlaylabels.cpp
    model/module/testmodulesettings.cpp
    model/module/testtaskrunner.cpp
    model/module/testmanagersettingsstore.cpp
    model/module/testdragsession.cpp
    model/module/testmoduleevents.cpp
    model/module/testoldapimodules.cpp
    model/module/testheader_types.cpp
    model/module/testmodulehost.cpp
    model/module/testmoduleowner.cpp
    model/module/testmoduleregistry.cpp
    model/module/testoverlaytriangles.cpp
    model/module/testsamplemodule.cpp
    model/module/testsamplemodule12.cpp
    model/module/testsamplemodule13.cpp
    model/module/testapi14.cpp
    model/module/testtimelinemodel.cpp
    model/module/face/testfacekeys.cpp
    model/module/face/testmodelsfolder.cpp
    model/module/face/testaudiopcm.cpp
    model/module/face/testfacedocument.cpp
    model/module/face/testfacemodule.cpp
    model/mesh/testanimationkeys.cpp
    model/mesh/testframemath.cpp
    model/mesh/testimagesize.cpp
    model/mesh/testmeshgeometry.cpp
    model/mesh/testmeshoverlay.cpp
    model/mesh/testmeshtree.cpp
    model/mesh/testmeshviewstate.cpp
    model/mesh/testcellscene.cpp
    model/mesh/testcellsceneneutral.cpp
    model/mesh/testcellsceneneutralreal.cpp
    model/mesh/testscenemodel.cpp
    model/mesh/testtexturereport.cpp
    model/mesh/testtexturecolour.cpp
    model/lod/testtilemodel.cpp
    model/lod/testtilemesh.cpp
    model/lod/testworldgrid.cpp
    model/physics/testdebuglinedrawer.cpp
    model/physics/testjoltruntime.cpp
    model/physics/teststudioworld.cpp
    model/physics/teststudioworldcm.cpp
    model/physics/testpushscene.cpp
    model/physics/testsandboxservice.cpp
    model/physics/testownedsandbox.cpp
    model/physics/teststudioworldhooks.cpp
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
# The face editor module (task 0147): its sources without the entry point, and the built library.
add_dependencies(eden-studio-tests face_editor)
target_sources(eden-studio-tests PRIVATE $<TARGET_OBJECTS:face_editor_objects>)
target_link_libraries(eden-studio-tests PRIVATE eden_face ${FFmpeg_LIBRARIES})
target_compile_definitions(eden-studio-tests PRIVATE
    "FACE_EDITOR_PATH=\"$<TARGET_FILE:face_editor>\"")
target_compile_definitions(eden-studio-tests PRIVATE
    "SAMPLE_EDITOR_MODULE_PATH=\"$<TARGET_FILE:sample_editor_module>\"")

# Modules built against the FROZEN headers of API 1.0, 1.1 and 1.2 (task 0091, 0065): each is a library of its own, so that two
# definitions of openeden::editor::Gizmo never meet in one executable. The tests load them by path.
foreach(old_api 1_0 1_1 1_2 1_3)
    set(old_target sample_editor_module_api${old_api})
    add_library(${old_target} SHARED ${CMAKE_CURRENT_SOURCE_DIR}/oldapi/samplenotemoduleold.cpp)
    target_compile_features(${old_target} PRIVATE cxx_std_20)
    target_include_directories(${old_target} BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/oldapi/api${old_api})
    target_link_libraries(${old_target} PRIVATE nlohmann_json::nlohmann_json)
    set_target_properties(${old_target} PROPERTIES
        OUTPUT_NAME "${old_target}"
        DEBUG_POSTFIX "" RELEASE_POSTFIX "" RELWITHDEBINFO_POSTFIX "" MINSIZEREL_POSTFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/editor-modules"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/editor-modules")
    add_dependencies(eden-studio-tests ${old_target})
    string(TOUPPER ${old_api} old_api_upper)
    target_compile_definitions(eden-studio-tests PRIVATE
        "OLD_API_${old_api_upper}_MODULE_PATH=\"$<TARGET_FILE:${old_target}>\"")
endforeach()

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
