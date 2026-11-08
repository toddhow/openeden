# extensions/compat_skyrimse/CMakeLists.txt (task 0086)

set(COMPAT_SKYRIMSE_SOURCES
    src/skyrimse_compat_extension.cpp
    src/scene_options.cpp
)

set(COMPAT_SKYRIMSE_HEADERS
    include/compat_skyrimse/skyrimse_compat_extension.hpp
    include/compat_skyrimse/scene_options.hpp
)

openeden_add_extension(compat_skyrimse
    SOURCES
        ${COMPAT_SKYRIMSE_SOURCES}
        ${COMPAT_SKYRIMSE_HEADERS}
    MANIFEST
        extension.json
)

target_include_directories(compat_skyrimse
    PUBLIC
        include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/components
        ${CMAKE_SOURCE_DIR}/sdk/include
)

target_link_libraries(compat_skyrimse
    PRIVATE
        components
)
