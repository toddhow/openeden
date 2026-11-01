file(GLOB OPENCS_TESTS_SRC_FILES
    main.cpp
    model/extension/testeditorextensionhost.cpp
    model/prefs/testconfigmigration.cpp
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

openmw_add_executable(eden-studio-tests ${OPENCS_TESTS_SRC_FILES})

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
