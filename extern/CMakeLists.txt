# SPDX-License-Identifier: GPL-3.0-or-later

set_directory_properties(PROPERTIES EXCLUDE_FROM_ALL on)
set(CMAKE_CXX_CLANG_TIDY "")

if(NOT OPENMW_USE_SYSTEM_JOLT)
    if(MSVC)
        # Keep Jolt runtime selection aligned with OpenMW (/MD on CI/MSVC).
        # Jolt defaults this to ON, which causes /MT vs /MD linker mismatches.
        set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    endif()

    set(JPH_DEBUG_RENDERER ON CACHE BOOL "")
    set(JPH_PROFILE_ENABLED OFF CACHE BOOL "")
    add_definitions(-DJPH_DEBUG_RENDERER=1)

    # Turn RTTI on for Jolt
    set(CPP_RTTI_ENABLED ON)

    # When turning this option on, the library will be compiled using doubles for positions. This allows for much bigger worlds.
    set(DOUBLE_PRECISION ON)

    # When turning this option on, the library will be compiled in such a way to attempt to keep the simulation deterministic across platforms
    set(CROSS_PLATFORM_DETERMINISTIC OFF)

    # When turning this option on, the library will be compiled with interprocedural optimizations enabled, also known as link-time optimizations or link-time code generation.
    # Note that if you turn this on you need to use SET_INTERPROCEDURAL_OPTIMIZATION() or set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON) to enable LTO specificly for your own project as well.
    # If you don't do this you may get an error: /usr/bin/ld: libJolt.a: error adding symbols: file format not recognized
    set(INTERPROCEDURAL_OPTIMIZATION OFF)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

    # When turning this on, in Debug and Release mode, the library will emit extra code to ensure that the 4th component of a 3-vector is kept the same as the 3rd component 
    # and will enable floating point exceptions during simulation to detect divisions by zero. 
    set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)

    # openeden: Jolt is a shared library, one copy for the game and the extensions that link it (extensions/compat_morrowind).
    # It used to follow BUILD_SHARED_LIBS from the cache, which the VSG section below forces on: so the first configure
    # of a new build folder built a static Jolt without -fPIC, which a plugin cannot link on Linux, and every later
    # configure a shared one. Every existing build folder had the shared one; this makes the first configure agree.
    set(BUILD_SHARED_LIBS ON)

    include(FetchContent)
    FetchContent_Declare(joltphysics
        URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.5.0.zip
        URL_HASH SHA512=52d6013a9bd38f31e73a308b8a5355df8a59d53d28a7823699d090f8e632ac0af58f1f7ecce342927cbe269b0aeae0656ea57f876a8e8f489024211bb52196a4
        SOURCE_DIR fetched/joltphysics
        SOURCE_SUBDIR Build
    )
    FetchContent_MakeAvailable(joltphysics)

    set(JOLT_INCLUDE_DIRS ${joltphysics_SOURCE_DIR} PARENT_SCOPE)
    set(JOLT_LIBRARIES Jolt PARENT_SCOPE)
    unset(BUILD_SHARED_LIBS)
endif()

if(NOT OPENMW_USE_SYSTEM_MYGUI)

    set(MYGUI_RENDERSYSTEM 4 CACHE STRING "")
    set(MYGUI_DISABLE_PLUGINS TRUE CACHE BOOL "")
    set(MYGUI_BUILD_DEMOS OFF CACHE BOOL "")
    set(MYGUI_BUILD_PLUGINS OFF CACHE BOOL "")
    set(MYGUI_BUILD_TOOLS OFF CACHE BOOL "")
    set(MYGUI_DONT_USE_OBSOLETE ON CACHE BOOL "")

    if(MYGUI_STATIC)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    else()
        set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    endif()

    include(FetchContent)
    FetchContent_Declare(mygui
        URL https://github.com/MyGUI/mygui/archive/refs/tags/MyGUI3.4.3.zip
        URL_HASH SHA512=c804ef665e786076582367f171082cd2181fdbd214300ddcca4a4245c5a0f45e62e72778ee2d96ec46b393e22477dd729f9bb3006e6eecbf536674e34a057721
        SOURCE_DIR fetched/mygui
    )
    FetchContent_MakeAvailable(mygui)

    set(MyGUI_INCLUDE_DIRS ${mygui_SOURCE_DIR}/MyGUIEngine/include PARENT_SCOPE)
    set(MyGUI_LIBRARIES MyGUIEngine PARENT_SCOPE)
endif()

if(NOT OPENMW_USE_SYSTEM_RECASTNAVIGATION)
    if(RECASTNAVIGATION_STATIC)
        set(BUILD_SHARED_LIBS OFF)
    else()
        set(BUILD_SHARED_LIBS ON)
    endif()

    set(RECASTNAVIGATION_DEMO OFF CACHE BOOL "")
    set(RECASTNAVIGATION_TESTS OFF CACHE BOOL "")
    set(RECASTNAVIGATION_EXAMPLES OFF CACHE BOOL "")

    include(FetchContent)
    FetchContent_Declare(recastnavigation
        URL https://github.com/OpenMW/recastnavigation/archive/03259f3287ff8330f0d66fcd98d022edddffaa97.zip
        URL_HASH SHA512=63db2ac90904764d7f1466d3c95994ec51d777cddd870b70f8ef674c09ed166b3d16ffc4ac1d18d0eab865fda4bf51b6ea17049025b66a338d3cadd0f48dc712
        SOURCE_DIR fetched/recastnavigation
    )
    FetchContent_MakeAvailable(recastnavigation)
endif()

if (NOT OPENMW_USE_SYSTEM_SQLITE3)
    include(FetchContent)
    FetchContent_Declare(sqlite3
        URL https://www.sqlite.org/2023/sqlite-amalgamation-3410100.zip
        URL_HASH SHA512=7de291709e88fffc693cf24ac675950cfc35c1bf7631cfea95167105720a05cf37fb943c57c5c985db2eeaa57b31894b3c0df98a7bd2939b5746fc5a24b5ae87
        SOURCE_DIR fetched/sqlite3
    )
    FetchContent_MakeAvailable(sqlite3)

    add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3 INTERFACE ${sqlite3_SOURCE_DIR}/)
    if (UNIX)
        target_link_libraries(sqlite3 ${CMAKE_DL_LIBS})
    endif()
    add_library(SQLite::SQLite3 ALIAS sqlite3)

    set(SQLite3_INCLUDE_DIR ${sqlite3_SOURCE_DIR}/ PARENT_SCOPE)
    set(SQLite3_LIBRARY sqlite3 PARENT_SCOPE)
endif()

add_subdirectory(smhasher)

if (BUILD_BENCHMARKS AND NOT OPENMW_USE_SYSTEM_BENCHMARK)

    set(BENCHMARK_ENABLE_TESTING OFF)
    set(BENCHMARK_ENABLE_INSTALL OFF)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
    set(BENCHMARK_ENABLE_WERROR OFF)

    include(FetchContent)
    FetchContent_Declare(benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
        URL_HASH SHA512=d73587ad9c49338749e1d117a6f8c7ff9c603a91a2ffa91a7355c7df7dea82710b9a810d34ddfef20973ecdc77092ec10fb2b4e4cc8d2e7810cbed79617b3828
        SOURCE_DIR fetched/benchmark
    )
    FetchContent_MakeAvailable(benchmark)
endif()

if (NOT OPENMW_USE_SYSTEM_YAML_CPP)
    if(YAML_CPP_STATIC)
        set(YAML_BUILD_SHARED_LIBS OFF)
    else()
        set(YAML_BUILD_SHARED_LIBS ON)
    endif()

    include(FetchContent)
    # One commit after the 0.8.0 tag for CMake 4.x compatibility
    FetchContent_Declare(yaml-cpp
        URL https://github.com/jbeder/yaml-cpp/archive/c2680200486572baf8221ba052ef50b58ecd816e.zip
        URL_HASH SHA512=9eb328e37ff547e7f681d64e3a8f268a8a5645cb1c258106d1d9f959eec498dfc7c466690c56f210ad7d5fb95b806a30c87ccbb019cccf5c3c6135cfc3c13fe1
        SOURCE_DIR fetched/yaml-cpp
    )
    FetchContent_MakeAvailable(yaml-cpp)

    if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
        target_precompile_headers(yaml-cpp PRIVATE <algorithm>)
    endif()
endif()

if (NOT OPENMW_USE_SYSTEM_ICU)
    set(ICU_ENV "ICU_DATA_FILTER_FILE=${CMAKE_CURRENT_SOURCE_DIR}/icufilters.json")
    if (ANDROID)
        # Note: Must be a build directory, not an install root, since the configure script
        # looks for a configuration file which does not get installed.
        set(OPENMW_ICU_HOST_BUILD_DIR "" CACHE STRING "A pre-built ICU build directory for the host system if cross-compiling")
        if (OPENMW_ICU_HOST_BUILD_DIR STREQUAL "")
            message(FATAL_ERROR "If cross-compiling on android you must set the \
            OPENMW_ICU_HOST_BUILD_DIR to the path of a pre-compiled build of \
            ICU 70.1 for the system doing the build, as ICU needs to be able \
            to run its own executables as part of the build process.")
        endif()
        # We need a host version of ICU so that the tools can be run when building the data library.
        set(NDK_STANDARD_ROOT ${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64)
        string(REPLACE "android-" "" ANDROIDVER ${ANDROID_PLATFORM})
        # Wants a triple such as aarch64-linux-android, excluding a trailing
        # -clang etc.
        string(REGEX MATCH "^[^-]\+-[^-]+-[^-]+" ICU_TOOLCHAIN_NAME ${ANDROID_TOOLCHAIN_NAME})
        set(ICU_ENV
            ${ICU_ENV}
            "CC=${CMAKE_C_COMPILER_LAUNCHER} ${NDK_STANDARD_ROOT}/bin/${ICU_TOOLCHAIN_NAME}${ANDROIDVER}-clang"
            "CXX=${CMAKE_CXX_COMPILER_LAUNCHER} ${NDK_STANDARD_ROOT}/bin/${ICU_TOOLCHAIN_NAME}${ANDROIDVER}-clang"
            "RANLIB=${NDK_STANDARD_ROOT}/bin/${ICU_TOOLCHAIN_NAME}-ranlib"
            "AR=${NDK_STANDARD_ROOT}/bin/${ICU_TOOLCHAIN_NAME}-ar"
            "CPPFLAGS=${ANDROID_COMPILER_FLAGS}"
            "LDFLAGS=${ANDROID_LINKER_FLAGS} -lc -lstdc++"
            )
        set(ICU_ADDITIONAL_OPTS --disable-tools --host=${ICU_TOOLCHAIN_NAME}${ANDROIDVER} --with-cross-build=${OPENMW_ICU_HOST_BUILD_DIR})
    endif()
    include(ExternalProject)
    ExternalProject_Add(icu
        URL https://github.com/unicode-org/icu/archive/refs/tags/release-70-1.zip
        URL_HASH MD5=49d5e2e5bab93ae1a4b56e916150544c
        SOURCE_DIR fetched/icu
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env ${ICU_ENV}
            <SOURCE_DIR>/icu4c/source/configure --enable-static --disable-shared
            --disable-tests --disable-samples --disable-icuio --disable-extras ${ICU_ADDITIONAL_OPTS}
        BUILD_COMMAND make
        INSTALL_COMMAND ""
    )
    ExternalProject_Get_Property(icu SOURCE_DIR BINARY_DIR)
    set(ICU_INCLUDE_DIRS
        ${SOURCE_DIR}/icu4c/source/common
        ${SOURCE_DIR}/icu4c/source/i18n
        PARENT_SCOPE
    )
    foreach(ICULIB data uc i18n)
        add_library(ICU::${ICULIB} STATIC IMPORTED GLOBAL)
        set_target_properties(ICU::${ICULIB} PROPERTIES IMPORTED_LOCATION
            ${BINARY_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}icu${ICULIB}${CMAKE_STATIC_LIBRARY_SUFFIX})
        add_dependencies(ICU::${ICULIB} icu)
    endforeach()
    set(ICU_LIBRARIES ICU::i18n ICU::uc ICU::data PARENT_SCOPE)
endif()

if (NOT TARGET nlohmann_json::nlohmann_json)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    set(JSON_Diagnostic_Positions ON CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/archive/55f93686c01528224f448c19128836e7df245f72.tar.gz
        URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef
        SOURCE_DIR fetched/nlohmann_json
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if ((BUILD_COMPONENTS_TESTS OR BUILD_OPENCS_TESTS OR BUILD_OPENMW_TESTS) AND NOT OPENMW_USE_SYSTEM_GOOGLETEST)

    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
        URL_HASH SHA512=1479ea2f3172c622c0ca305f5b2bc45a42941221ec0ac7865e6d6d020ec4d008d952fc64e01a4c5138d7bed4148cf75596f25bb9e9044a98bbbf5662053ea11c
        SOURCE_DIR fetched/googletest
    )
    if (MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(googletest)

    add_library(GTest::GTest ALIAS gtest)
    add_library(GMock::GMock ALIAS gmock)
endif()

# VulkanSceneGraph and its dependencies, fetched at pinned versions (docs/dependencies.md).
include(FetchContent)

# 1. Vulkan Headers
FetchContent_Declare(vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.3.296.0.zip
    URL_HASH SHA512=ab0d7a03e76c7a3e80ee5c05456e5e076e51864ac837c97c9795b3ca2d11cdc6ceea963ffb637a8dafbd5a53720555b9f697ef0adff7dcad6a34478a767e9631
    SOURCE_DIR fetched/vulkan_headers
)
FetchContent_MakeAvailable(vulkan_headers)

set(Vulkan_INCLUDE_DIR "${vulkan_headers_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(Vulkan_INCLUDE_DIRS "${vulkan_headers_SOURCE_DIR}/include" CACHE PATH "" FORCE)

if(WIN32)
    set(VULKAN_LIB "${CMAKE_CURRENT_BINARY_DIR}/vulkan-1.lib")
    set(VULKAN_DEF "${CMAKE_CURRENT_SOURCE_DIR}/vulkan/vulkan-1.def")
    if(NOT EXISTS "${VULKAN_LIB}")
        execute_process(
            COMMAND lib /def:${VULKAN_DEF} /machine:x64 /out:${VULKAN_LIB}
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
    set(Vulkan_LIBRARY "${VULKAN_LIB}" CACHE FILEPATH "" FORCE)
    set(Vulkan_LIBRARIES "${VULKAN_LIB}" CACHE FILEPATH "" FORCE)
endif()

# 2. glslang (static)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glslang
    URL https://github.com/KhronosGroup/glslang/archive/refs/tags/14.3.0.zip
    URL_HASH SHA512=e5dd6405a837dadc19287d04f3ab25fe33c199deec308bcbc9a1a7d8a9f4ee004ce75264130f31f7c9bbb4dcecd74c95df35925f706826ca2a85ff02f6a2e7c1
    SOURCE_DIR fetched/glslang
)
FetchContent_MakeAvailable(glslang)

# Provide glslang/SPIRV headers at include/glslang/SPIRV
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/glslang")
file(COPY "${glslang_SOURCE_DIR}/SPIRV" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/include/glslang")
target_include_directories(SPIRV INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")
target_include_directories(glslang INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")

# Add glslang targets to vsgTargets export set so VSG export succeeds
install(TARGETS glslang MachineIndependent GenericCodeGen SPIRV glslang-default-resource-limits
    EXPORT vsgTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# glslang's MachineIndependent links OSDependent on every platform, so it must be in the export set on Linux too.
if(TARGET OSDependent)
    install(TARGETS OSDependent
        EXPORT vsgTargets
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
    )
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cmake/glslang")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake/glslang/glslangConfig.cmake" "set(glslang_FOUND TRUE)\n")
set(glslang_DIR "${CMAKE_CURRENT_BINARY_DIR}/cmake/glslang" CACHE PATH "" FORCE)

if(NOT TARGET glslang::glslang AND TARGET glslang)
    add_library(glslang::glslang ALIAS glslang)
endif()
if(NOT TARGET glslang::SPIRV AND TARGET SPIRV)
    add_library(glslang::SPIRV ALIAS SPIRV)
endif()
if(NOT TARGET glslang::glslang-default-resource-limits AND TARGET glslang-default-resource-limits)
    add_library(glslang::glslang-default-resource-limits ALIAS glslang-default-resource-limits)
endif()

# 3. VSG
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(VSG_SUPPORTS_ShaderCompiler ON CACHE BOOL "" FORCE)
set(VSG_SUPPORTS_ShaderOptimizer OFF CACHE BOOL "" FORCE)

FetchContent_Declare(vsg
    URL https://github.com/vsg-dev/VulkanSceneGraph/archive/refs/tags/v1.1.16.zip
    URL_HASH SHA512=0b23ace8d665f9b94d5b8058cbf9072b659dcbfe10168348584bf5e72adfb2e16bdf75e12bc47cf83d6f1afc72603fa97ebc9b90ba8084873628a2bd48234730
    SOURCE_DIR fetched/vsg
    # openeden-vsg: one small patch gives vsg::Device an optional VkPipelineCache that its graphics and compute pipeline
    # creation passes to the driver (upstream passes VK_NULL_HANDLE, so the driver recompiled every pipeline each run).
    # Runs only when the source is first fetched; code that uses the member checks for it, so an older fetch still builds.
    PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/patches/vsg_pipeline_cache.cmake
    # VSG's headers are system headers for OpenEden's code (Linux builds with -Werror).
    SYSTEM
)
FetchContent_MakeAvailable(vsg)

add_dependencies(vsg glslang SPIRV glslang-default-resource-limits)

# The DLL output directory is set below, after vsgXchange is fetched.

# 4. assimp (static, COLLADA importer only)
# openeden-vsg: OpenMW 0.51 loaded .dae models through OSG's COLLADA plugin. The VSG build reads them with assimp:
# components links it for the collision shapes of non-NIF models (resource/physicsshapemanager.cpp). The example
# suite used by the integration tests is all .dae.
set(OPENEDEN_ASSIMP_VERSION 6.0.5)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL_PDB OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
# Use the zlib the engine already links; assimp's bundled copy defines the same symbols.
set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)

# BUILD_SHARED_LIBS is forced on above for VSG; assimp is a static library.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF)
# The root CMakeLists.txt compiles with /utf-8 through add_compile_options, which assimp cannot see, so assimp adds
# /source-charset:utf-8 and MSVC rejects the pair (D8016). Showing /utf-8 in CMAKE_CXX_FLAGS stops it adding its own.
set(OPENEDEN_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
endif()
FetchContent_Declare(assimp
    URL https://github.com/assimp/assimp/archive/refs/tags/v${OPENEDEN_ASSIMP_VERSION}.tar.gz
    URL_HASH SHA512=57326194bf3a8e2ae793c739878231067fdd6d031531e910d4d20fc8a673eacf48f75e11bd21ca2e6421682f78585d445bffb647bdcb52e01b2cd4d3ff0e2c62
    SOURCE_DIR fetched/assimp
)
FetchContent_MakeAvailable(assimp)
set(CMAKE_CXX_FLAGS "${OPENEDEN_SAVED_CXX_FLAGS}")
unset(BUILD_SHARED_LIBS)
unset(CMAKE_POLICY_DEFAULT_CMP0077)

if(NOT TARGET assimp::assimp)
    add_library(assimp::assimp ALIAS assimp)
endif()
# assimp compiles its bundled pugixml into the library; components reads COLLADA animation clips with it.
set(OPENEDEN_PUGIXML_INCLUDE_DIR ${assimp_SOURCE_DIR}/contrib/pugixml/src PARENT_SCOPE)

# vsgXchange's own assimp reader stays off: it builds VSG's own shader pipelines, which vsgopenmw does not use.
set(vsgXchange_assimp OFF CACHE BOOL "" FORCE)

# 5. vsgXchange
FetchContent_Declare(vsgXchange
    URL https://github.com/vsg-dev/vsgXchange/archive/refs/tags/v1.1.14.zip
    URL_HASH SHA512=57d6f626e54843fa813b85a64d1e24e9b09fb5fabc3fba08d66104d41e04cbdbfaf9a5aff3b30c4ded75c09dedc49146f9eb719c1a8ee713c807031fc436389b
    SOURCE_DIR fetched/vsgXchange
    SYSTEM
)
FetchContent_MakeAvailable(vsgXchange)
add_dependencies(vsgXchange vsg)

# 6. vsgQt: the editor's Vulkan viewport (task 0014). Only the editor needs it, and only the editor pulls in Qt.
if (BUILD_OPENCS OR BUILD_OPENCS_TESTS)
    set(QT_PACKAGE_NAME Qt6 CACHE STRING "" FORCE)
    set(VSGQT_BUILD_EXAMPLES OFF CACHE BOOL "")
    FetchContent_Declare(vsgQt
        URL https://github.com/vsg-dev/vsgQt/archive/refs/tags/v0.5.0.tar.gz
        URL_HASH SHA512=1e22e2171b3a470e09b04bce5f27e1fa2ceda38a9ea62c2da558664d47cf289d918f33fc324292c4e237af2bd0eb6affe7ce357639c1151d7f93a9c7cedd6d37
        SOURCE_DIR fetched/vsgQt
        SYSTEM
    )
    FetchContent_MakeAvailable(vsgQt)
    add_dependencies(vsgQt vsg)
endif()

# openeden-vsg: extern/ is processed before the root CMakeLists.txt sets CMAKE_RUNTIME_OUTPUT_DIRECTORY, and VSG's
# own CMake writes its DLLs under _deps/vsg-build/lib, so openmw.exe ran with stale or missing VSG DLLs. On Windows
# the DLLs must sit next to the executables, as the root CMakeLists.txt arranges for OpenEden's own targets.
if (WIN32)
    set(OPENEDEN_VSG_DLL_TARGETS vsg vsgXchange)
    if (TARGET vsgQt)
        list(APPEND OPENEDEN_VSG_DLL_TARGETS vsgQt)
    endif()
    # Jolt's DLL too (shared, see the Jolt section): a new build folder otherwise left it in _deps/joltphysics-build.
    if (TARGET Jolt)
        list(APPEND OPENEDEN_VSG_DLL_TARGETS Jolt)
    endif()
    foreach(target ${OPENEDEN_VSG_DLL_TARGETS})
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${OpenMW_BINARY_DIR}")
        foreach(config ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER ${config} upperConfig)
            set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${upperConfig}
                "${OpenMW_BINARY_DIR}/${config}")
        endforeach()
    endforeach()
endif()

set(VSG_INCLUDE_DIRS ${vsg_SOURCE_DIR}/include ${vsg_BINARY_DIR}/include PARENT_SCOPE)
set(VSG_LIBRARIES vsg::vsg PARENT_SCOPE)
set(VSGXCHANGE_INCLUDE_DIRS ${vsgXchange_SOURCE_DIR}/include ${vsgXchange_BINARY_DIR}/include PARENT_SCOPE)
set(VSGXCHANGE_LIBRARIES vsgXchange::vsgXchange PARENT_SCOPE)

# openeden-vsg: the Windows dependency set (OpenMW's prebuilt vcpkg packages) has SDL2 built without the vcpkg
# "vulkan" feature, so SDL cannot create a Vulkan window. OpenEden keeps linking that SDL2 2.32.10 and ships a
# Vulkan-enabled build of the same release next to the executables instead. Its exported API is identical; only the
# Windows video driver gains Vulkan support. Sources and hash match vcpkg's sdl2 port for this version.
if (WIN32)
    option(OPENEDEN_SDL2_VULKAN_RUNTIME "Build SDL2 with Vulkan support and copy SDL2.dll next to openmw.exe" ON)
    if (OPENEDEN_SDL2_VULKAN_RUNTIME)
        include(ExternalProject)
        set(SDL2_VULKAN_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/sdl2-vulkan)
        ExternalProject_Add(sdl2_vulkan
            URL https://github.com/libsdl-org/SDL/archive/release-2.32.10.tar.gz
            URL_HASH SHA512=d5622d6bb7266f7942a7b8ad43e8a22524893bf0c2ea1af91204838d9b78d32768843f6faa248757427b8404b8c6443776d4afa6b672cd8571a4e0c03a829383
            PREFIX ${SDL2_VULKAN_PREFIX}
            CMAKE_ARGS
                -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                -DCMAKE_BUILD_TYPE=Release
                -DSDL_SHARED=ON
                -DSDL_STATIC=OFF
                -DSDL_TEST=OFF
                -DSDL_VULKAN=ON
            BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
            INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR> --config Release
            BUILD_BYPRODUCTS ${SDL2_VULKAN_PREFIX}/bin/SDL2.dll
        )
        set(OPENEDEN_SDL2_VULKAN_DLL ${SDL2_VULKAN_PREFIX}/bin/SDL2.dll PARENT_SCOPE)
    endif()
endif()

# Slang's compiler, slangc (task 0023, F1a; research note 0005; docs/dependencies.md).
# A build-time tool only: nothing of Slang is linked into or shipped with the game. The release archive is downloaded
# from its pinned URL and checked against its SHA-512, so this works on a CI runner with no machine-wide install.
# OPENEDEN_SLANGC is left empty where there is no matching archive (not x86-64 Windows or Linux) or the option is off,
# and then no .slang shader is compiled and [Rendering] slang shaders falls back to the GLSL.
option(OPENEDEN_BUILD_SLANG_SHADERS "Compile .slang shaders to SPIR-V at build time with a downloaded, pinned slangc" ON)
set(OPENEDEN_SLANGC "" CACHE INTERNAL "Path of the pinned slangc, or empty")
if(OPENEDEN_BUILD_SLANG_SHADERS)
    set(OPENEDEN_SLANG_VERSION "2026.19")
    set(OPENEDEN_SLANG_URL "")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|x64)$")
        set(OPENEDEN_SLANG_URL
            "https://github.com/shader-slang/slang/releases/download/v${OPENEDEN_SLANG_VERSION}/slang-${OPENEDEN_SLANG_VERSION}-windows-x86_64.zip")
        set(OPENEDEN_SLANG_HASH
            "cdd3329697a7307e0e46a03c45f279af9dd437cf842829f7dee579408959bd821982d9a486a4b01b865b19e67292c378ee962d340244c3062a3915b4f2cb967d")
        set(OPENEDEN_SLANG_EXE "bin/slangc.exe")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set(OPENEDEN_SLANG_URL
            "https://github.com/shader-slang/slang/releases/download/v${OPENEDEN_SLANG_VERSION}/slang-${OPENEDEN_SLANG_VERSION}-linux-x86_64-glibc-2.28.tar.gz")
        set(OPENEDEN_SLANG_HASH
            "68bbb47ec6157d28b9fd353ad700facdd73d5fb56d6b5ef4a88bf8038c8e0f23ef951e596800322a3031573480262419a51900140f94314b45ae21c6dba0b5a7")
        set(OPENEDEN_SLANG_EXE "bin/slangc")
    endif()

    if(OPENEDEN_SLANG_URL)
        FetchContent_Declare(slang
            URL "${OPENEDEN_SLANG_URL}"
            URL_HASH SHA512=${OPENEDEN_SLANG_HASH}
            SOURCE_DIR fetched/slang
        )
        FetchContent_MakeAvailable(slang)
        if(EXISTS "${slang_SOURCE_DIR}/${OPENEDEN_SLANG_EXE}")
            set(OPENEDEN_SLANGC "${slang_SOURCE_DIR}/${OPENEDEN_SLANG_EXE}" CACHE INTERNAL "Path of the pinned slangc, or empty")
        else()
            message(WARNING "Slang was downloaded but ${OPENEDEN_SLANG_EXE} is not in it; .slang shaders will not be built")
        endif()
    else()
        message(STATUS "Slang: no pinned archive for ${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR}; .slang shaders will not be built")
    endif()
endif()

# spirv-val, to check every compiled Slang shader (task 0023 follow-up; docs/dependencies.md). Slang's archive has no
# validator and Khronos publishes no binary, so it is built from pinned sources. Its directory is EXCLUDE_FROM_ALL:
# only the spirv-val target is built, and only because openeden_add_slang_shader() depends on it.
if(OPENEDEN_SLANGC)
    # SPIRV-Tools needs Python 3 for its generated tables. Where there is none (a Windows machine without Python),
    # use the pinned embeddable package, as a build tool only.
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(NOT Python3_EXECUTABLE AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        FetchContent_Declare(buildpython
            URL https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip
            URL_HASH SHA512=98375769d8181a1553d361afdcda1cef34fd59cced20c2c8dc7ef0de349ad917057b59b6a66572bd6126bc352ef784f31a08e937ad185f6bc8d6c9271ab29411
            SOURCE_DIR fetched/buildpython
        )
        FetchContent_MakeAvailable(buildpython)
        set(Python3_EXECUTABLE "${buildpython_SOURCE_DIR}/python.exe" CACHE FILEPATH "Python 3 for build tools" FORCE)
    endif()
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
    set(SPIRV_BUILD_FUZZER OFF CACHE BOOL "" FORCE)
    set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
    set(SPIRV_HEADERS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    set(SPIRV_HEADERS_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
    FetchContent_Declare(spirv_headers
        URL https://github.com/KhronosGroup/SPIRV-Headers/archive/refs/tags/vulkan-sdk-1.4.357.0.zip
        URL_HASH SHA512=651e1d08e95e56a8512045d79b89afb7fe3ccfb9ba862a2e084440c65a9267e45b218b1df95d03cd3b2da476a51bac46cf66067ec1c4b1cbbcb55748293c7251
        SOURCE_DIR fetched/spirv-headers
    )
    FetchContent_Declare(spirv_tools
        URL https://github.com/KhronosGroup/SPIRV-Tools/archive/refs/tags/vulkan-sdk-1.4.357.0.zip
        URL_HASH SHA512=98ddcf34189abf79a2d4ac5f554e6432c09de64c2d1c1652ab452813d51200ff07065fcdc7ce61b8a76fe6dc6fd6393878bd2bdc4a6104499afcc10f430f7e71
        SOURCE_DIR fetched/spirv-tools
    )
    FetchContent_MakeAvailable(spirv_headers spirv_tools)
    set_property(DIRECTORY "${spirv_headers_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
    set_property(DIRECTORY "${spirv_tools_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
endif()

# Task 0052: BLAKE3 and zstd for the Eden Archive reference library (edenarchive/). Only the sources are fetched here;
# edenarchive/CMakeLists.txt builds them as static libraries with its own flags, so that the pinned copy is the one
# that is used (OpenMW's Windows vcpkg set also contains a zstd, which this does not touch). Records, licences and
# hashes: docs/dependencies.md.
FetchContent_Declare(zstd_src
    URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.zip
    URL_HASH SHA512=d2958014b11ba0fb579b5fd85260caf66955f28e67d9c0ff7b77970ab2ce4d293a55e4ea9c7f00680fb84726777d7cb0e3c2ae2661d97daa8f57b8126d23a7d3
    SOURCE_DIR fetched/zstd
)
FetchContent_Declare(blake3_src
    URL https://github.com/BLAKE3-team/BLAKE3/archive/refs/tags/1.8.2.zip
    URL_HASH SHA512=90d8196cddf9fc0d6668a0759c67db1523bd0f11e17703c14cb1e629fba224d4f5829437e1121e4bc16f8a2411a6633ab1a1c4f08ee774b6aaaca4e70b879e4c
    SOURCE_DIR fetched/blake3
)
FetchContent_MakeAvailable(zstd_src blake3_src)
set(OPENEDEN_ZSTD_SOURCE_DIR "${zstd_src_SOURCE_DIR}" PARENT_SCOPE)
set(OPENEDEN_BLAKE3_SOURCE_DIR "${blake3_src_SOURCE_DIR}" PARENT_SCOPE)

# Task 0059: meshoptimizer v1.3 (MIT) for the object cluster baker. Fetched and compiled here with the project's
# third-party flags (not by its own CMake file). Record and licence: docs/dependencies.md.
FetchContent_Declare(meshoptimizer_src
    URL https://github.com/zeux/meshoptimizer/archive/refs/tags/v1.3.zip
    URL_HASH SHA512=bb037ba6c9ac1c616bc796afc55ca3d2955ec69189db0093171615903d6e632ed24b2cdb4d38087562046b0e4d43ee962df04dd95891932f53df7ffca1998b98
    SOURCE_DIR fetched/meshoptimizer
    # Its own CMake file is not used (the sources are compiled below), so add_subdirectory must find nothing.
    SOURCE_SUBDIR does-not-exist
)
FetchContent_MakeAvailable(meshoptimizer_src)
file(GLOB OPENEDEN_MESHOPT_SOURCES "${meshoptimizer_src_SOURCE_DIR}/src/*.cpp")
add_library(openeden_meshoptimizer STATIC ${OPENEDEN_MESHOPT_SOURCES})
if (MSVC)
    target_compile_options(openeden_meshoptimizer PRIVATE /W0)
else()
    target_compile_options(openeden_meshoptimizer PRIVATE -w)
endif()
set_target_properties(openeden_meshoptimizer PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(openeden_meshoptimizer SYSTEM PUBLIC "${meshoptimizer_src_SOURCE_DIR}/src")
target_compile_features(openeden_meshoptimizer PUBLIC cxx_std_11)

# Task 0104 (decision 0015, UI system U1): RmlUi 6.3 (MIT), the first backend of the new UI system. Fetched and compiled
# here with the project's third-party flags (not by its own CMake file: that one brings samples, install rules and a
# shared-library switch), as a static library with the default FreeType font engine. FreeType itself is not fetched: it is
# the system/vcpkg library MyGUI already needs. Record and licences: docs/dependencies.md. OFF removes the UI backend
# and `[GUI] rmlui` then only logs a warning.
option(OPENEDEN_BUILD_RMLUI "Build the RmlUi UI backend (decision 0015, task 0104)" ON)
if(OPENEDEN_BUILD_RMLUI)
    find_package(Freetype REQUIRED)
    if(TARGET Freetype::Freetype)
        # Linked from a target of this directory and used by components/: an imported target is visible in its own
        # directory only, unless it is global.
        set_target_properties(Freetype::Freetype PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
    FetchContent_Declare(rmlui_src
        URL https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz
        URL_HASH SHA512=b66d2a3621ca2690d6e826661203fc3a4babe16243ed01b2f884d149f5b7ee598241f1f743262c7a8fbf8ae98641d5c5526fc63d8a6283638b6dc021d23e103e
        SOURCE_DIR fetched/rmlui
        # Its own CMake file is not used (the sources are compiled below).
        SOURCE_SUBDIR does-not-exist
    )
    FetchContent_MakeAvailable(rmlui_src)
    file(GLOB_RECURSE OPENEDEN_RMLUI_SOURCES "${rmlui_src_SOURCE_DIR}/Source/Core/*.cpp")
    add_library(openeden_rmlui STATIC ${OPENEDEN_RMLUI_SOURCES})
    if (MSVC)
        target_compile_options(openeden_rmlui PRIVATE /W0 /utf-8)
    else()
        target_compile_options(openeden_rmlui PRIVATE -w)
    endif()
    set_target_properties(openeden_rmlui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(openeden_rmlui PRIVATE "${rmlui_src_SOURCE_DIR}/Source/Core")
    target_include_directories(openeden_rmlui SYSTEM PUBLIC "${rmlui_src_SOURCE_DIR}/Include")
    # RMLUI_STATIC_LIB turns the __declspec(dllimport/export) off in the public headers; the library is linked into
    # `components` only. RMLUI_FONT_ENGINE_FREETYPE selects the default font engine (FontEngineDefault/*.cpp).
    target_compile_definitions(openeden_rmlui PUBLIC RMLUI_STATIC_LIB)
    target_compile_definitions(openeden_rmlui PRIVATE RMLUI_FONT_ENGINE_FREETYPE "RMLUI_VERSION=\"6.3\"")
    target_compile_features(openeden_rmlui PUBLIC cxx_std_17)
    target_link_libraries(openeden_rmlui PRIVATE Freetype::Freetype)
endif()
