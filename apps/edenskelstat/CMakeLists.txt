# The read-only measurement of extended skeletons (task 0070, stage a):
#   edenskelstat --manifest <file> --profile <skeletonprofile.json> --out <folder> [--detail]
# A developer tool run on the user's own installed files; not installed.
openmw_add_executable(edenskelstat main.cpp)
target_link_libraries(edenskelstat PRIVATE components eden_body)
if (WIN32)
    target_sources(edenskelstat PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()
