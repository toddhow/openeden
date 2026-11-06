# extensions/compat_morrowind/CMakeLists.txt

set(COMPAT_MORROWIND_SOURCES
    src/morrowind_compat_extension.cpp
    src/kf_converter.cpp
    src/kf_animation.cpp
    src/scene_options.cpp
)

set(COMPAT_MORROWIND_HEADERS
    include/compat_morrowind/morrowind_compat_extension.hpp
    include/compat_morrowind/kf_converter.hpp
    include/compat_morrowind/kf_animation.hpp
    include/compat_morrowind/scene_options.hpp
)

openeden_add_extension(compat_morrowind
    SOURCES
        ${COMPAT_MORROWIND_SOURCES}
        ${COMPAT_MORROWIND_HEADERS}
    MANIFEST
        extension.json
)

target_include_directories(compat_morrowind
    PUBLIC
        include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/components
        ${CMAKE_SOURCE_DIR}/sdk/include
)

target_link_libraries(compat_morrowind
    PRIVATE
        components
)
