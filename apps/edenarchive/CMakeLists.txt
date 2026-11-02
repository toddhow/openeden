# Command-line tools of the Eden Archive (task 0052). They are OpenEden's (GPL) and use the engine's own extraction
# name rule (components/vfs/entryname.cpp, compiled in directly; it needs nothing else of the engine).
set(EDENARCHIVE_TOOL_SOURCES_COMMON
    ${CMAKE_SOURCE_DIR}/components/vfs/entryname.cpp
)

foreach (tool edenpack edenunpack edenlist edenvalidate)
    openmw_add_executable(${tool} ${tool}.cpp ${EDENARCHIVE_TOOL_SOURCES_COMMON})
    target_include_directories(${tool} PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(${tool} PRIVATE edenarchive)
    if (WIN32)
        target_sources(${tool} PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
        install(TARGETS ${tool} RUNTIME DESTINATION ".")
    endif()
endforeach()
