# The transition measurement as a program (task 0067, stage 0): creativemotioncheck [--json] <openmw.log>
openmw_add_executable(creativemotioncheck main.cpp)
target_link_libraries(creativemotioncheck PRIVATE eden_animtrace)
if (WIN32)
    target_sources(creativemotioncheck PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
    install(TARGETS creativemotioncheck RUNTIME DESTINATION ".")
endif()
