# The body package validator as a program (task 0063): edenbodyvalidate [--json] <package>
openmw_add_executable(edenbodyvalidate main.cpp)
target_link_libraries(edenbodyvalidate PRIVATE eden_body)
if (WIN32)
    target_sources(edenbodyvalidate PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
    install(TARGETS edenbodyvalidate RUNTIME DESTINATION ".")
endif()
