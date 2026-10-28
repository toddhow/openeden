# Third-party dependencies, fetched at pinned versions.
#
# Each dependency is pinned to an exact commit with a SHA-256 of the source
# archive, so a build can never silently pick up different code. Licenses are
# recorded in docs/dependencies.md; update it whenever this file changes.

include(FetchContent)

# --- Jolt Physics -----------------------------------------------------------
# v5.6.0, commit e77f175595e64cb44218cc9d9d56fc365ad0e36a (2026-07-11). MIT.
# Only physics/jolt may link this target (see physics/jolt/CLAUDE.md).
#
# Options below are defined in Jolt's Build/CMakeLists.txt at that commit.
# They must be set before FetchContent_MakeAvailable so Jolt sees them.

# Same simulation results on Windows and Linux, so golden replay recordings
# can be shared between CI platforms. Costs some speed; revisit with the
# performance budget.
if(OPAS_BUILD_PHYSICS)
  set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)
  # Match GoogleTest and our code, which use the DLL MSVC runtime (/MD).
  set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
  # Jolt's own comment warns LTO must then be enabled for the whole project too.
  set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
  # We do not own Jolt's warnings; newer compilers must not break our build.
  set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
  set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
  # GPU / compute backends are not needed for rigid-body ragdolls.
  set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
  set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
  set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
  set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    JoltPhysics
    URL https://github.com/jrouwe/JoltPhysics/archive/e77f175595e64cb44218cc9d9d56fc365ad0e36a.tar.gz
    URL_HASH SHA256=1f32328fb763135de10a244568d6ccb2ed9b1e6593fafe6dc6db5b2719d330bd
    SOURCE_SUBDIR Build
    SYSTEM
  )
endif()

# --- nlohmann/json ----------------------------------------------------------
# v3.12.0, commit 55f93686c01528224f448c19128836e7df245f72 (2025-04-11). MIT.
# Used privately by kinematic/markers for JSON files. Diagnostic positions
# (an option in its CMakeLists.txt at that commit) record each value's byte
# offset, so loaders can report line numbers. The define is an INTERFACE
# definition of the target, so every user sees the same layout.
# A host project that already provides nlohmann_json keeps its own; it must
# then build it with JSON_Diagnostic_Positions ON, or loader errors lose their
# line numbers (see kinematic/markers/CLAUDE.md).
if(NOT TARGET nlohmann_json::nlohmann_json)
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  set(JSON_Diagnostic_Positions ON CACHE BOOL "" FORCE)

  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/55f93686c01528224f448c19128836e7df245f72.tar.gz
    URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef
    SYSTEM
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- GoogleTest -------------------------------------------------------------
# v1.17.0, commit 52eb8108c5bdec04579160ae17225d66034bd723 (2025-04-30). BSD-3-Clause.
if(OPAS_BUILD_TESTS)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.tar.gz
    URL_HASH SHA256=745c55415660044610f7fcd3af7a6420d5de16a7dbb9ebfe2e131275676232be
    SYSTEM
  )
  FetchContent_MakeAvailable(googletest)
endif()

if(OPAS_BUILD_PHYSICS)
  FetchContent_MakeAvailable(JoltPhysics)
endif()

# --- Debug viewer: raylib, Dear ImGui, rlImGui (M6, docs/tasks/0016) ---------
# Only sandbox/viewer's app links these (tools/CheckViewerIncludes.cmake).
# raylib 6.0, commit dbc56a87da87d973a9c5baa4e7438a9d20121d28 (2026-04-23). zlib/libpng licence.
# Dear ImGui v1.92.9b, commit f1cc2ae15e53a861a874c3034aae6798fde194ab (2026-07-31). MIT.
# rlImGui tag Raylib_6_0, commit 3bc5731c4216bb8caa67fbea24aa85ce80d57ccb (2026-05-01). zlib.
# On Linux, raylib's bundled GLFW needs X11/Wayland development packages; the
# list is taken from raylib's own CI (.github/workflows/build_cmake.yml at
# that commit) and installed by our CI job that builds the viewer.
if(OPAS_BUILD_VIEWER)
  # Options defined in raylib's CMakeOptions.txt at that commit.
  # Plain set(): raylib needs CMake 3.25, so policy CMP0077 is NEW and its
  # option() keeps these; a cache FORCE would overwrite a parent project's value.
  set(BUILD_EXAMPLES OFF)
  set(BUILD_SHARED_LIBS OFF)

  FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/dbc56a87da87d973a9c5baa4e7438a9d20121d28.tar.gz
    URL_HASH SHA256=81b06ce7c19cf3b634b0271c23c361ba6ad8bf45fb8b036abbfeb4260ec1e126
    SYSTEM
  )
  # Dear ImGui and rlImGui ship no CMake build of their own (rlImGui uses
  # premake), so they are only fetched here and built as opas_imgui below.
  FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/f1cc2ae15e53a861a874c3034aae6798fde194ab.tar.gz
    URL_HASH SHA256=f30be7f3e2136615b1848a9afb72f6dfd8a60820ddf1c1a85997bf3b722e7cfc
  )
  FetchContent_Declare(
    rlimgui
    URL https://github.com/raylib-extras/rlImGui/archive/3bc5731c4216bb8caa67fbea24aa85ce80d57ccb.tar.gz
    URL_HASH SHA256=2198c4eb4c0a2b8efc2204da046ef014a2c7b736e1483039badee6d19a7ed35a
  )
  # MakeAvailable only downloads a project that has no CMakeLists.txt at its
  # root, which is the case for both ImGui and rlImGui.
  FetchContent_MakeAvailable(raylib imgui rlimgui)

  # raylib compiles its own copy of cgltf (src/rmodels.c: CGLTF_IMPLEMENTATION
  # under "#if SUPPORT_FILEFORMAT_GLTF"). opas_view links anim/gltf's cgltf
  # too, and two copies of the same C functions in one program let the linker
  # pick one silently: its layout differed from ours and the preview crashed
  # (task 0020, C3e). The viewer never loads models through raylib, so that
  # one flag is defined 0 on the raylib target; src/config.h guards each flag
  # with #ifndef, so every other default stays. (Not CUSTOMIZE_BUILD: its
  # ParseConfigHeader.cmake turns every config.h flag, even those defined 0,
  # into an ON option, which switched on SUPPORT_CUSTOM_FRAME_CONTROL and left
  # the window without frames or input.)
  target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_GLTF=0)
  foreach(flag IN ITEMS CUSTOMIZE_BUILD SUPPORT_CUSTOM_FRAME_CONTROL SUPPORT_BUSY_WAIT_LOOP)
    if(${flag})
      message(FATAL_ERROR "raylib option ${flag} is on (a stale cache from an earlier build?); "
                          "it changes raylib's frame loop. Reconfigure with -U ${flag}.")
    endif()
  endforeach()

  # Third-party code: built without our warnings-as-errors, includes marked
  # SYSTEM so their headers never trip our warnings either.
  add_library(opas_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
  )
  target_include_directories(opas_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
  target_link_libraries(opas_imgui PUBLIC raylib)
endif()

# --- cgltf (task 0020, stage C3b) -------------------------------------------
# v1.15, commit 360db1a95480fe102ae9c69b27c5d101167ff5ba (2025-02-09). MIT.
# A single-header glTF 2.0 reader. Only anim/gltf links it, privately, through
# the opas_cgltf target defined in anim/gltf/CMakeLists.txt. The archive has no
# CMakeLists.txt at its root, so MakeAvailable only downloads it.
FetchContent_Declare(
  cgltf
  URL https://github.com/jkuhlmann/cgltf/archive/360db1a95480fe102ae9c69b27c5d101167ff5ba.tar.gz
  URL_HASH SHA256=445d135cf793232ae6a585ca1404e4ff28d4f4dbca070689034fe780370ac84a
)
FetchContent_MakeAvailable(cgltf)
