# Top-level build for the Open Procedural Animation System.
# "opas" / OPAS_ is a placeholder prefix until the project name is decided
# (see "Open questions" in CLAUDE.md).

# 3.25 is the first version where FetchContent_Declare accepts SYSTEM, which
# keeps third-party headers from tripping our warnings-as-errors.
cmake_minimum_required(VERSION 3.25)

project(opas VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# When CreativeMotion is included as a subproject inside OpenEden to build the
# creativemotion shared plugin library, internal opas_* module targets must be
# compiled as static archives even if the host project sets BUILD_SHARED_LIBS.
if(NOT PROJECT_IS_TOP_LEVEL)
  set(BUILD_SHARED_LIBS OFF)
endif()

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. Use a preset, e.g. `cmake --preset debug`.")
endif()

# When CreativeMotion is pulled into another project (the CreativeAlignment
# plugin uses it as a submodule for the kinematic libraries), tests and the
# Jolt physics layer default off so the host does not build them.
option(OPAS_BUILD_TESTS "Build the test runner" ${PROJECT_IS_TOP_LEVEL})
option(OPAS_BUILD_PHYSICS "Build physics/ (fetches Jolt)" ${PROJECT_IS_TOP_LEVEL})
# The debug viewer (M6) fetches raylib and Dear ImGui and needs a desktop
# OpenGL stack; CI builds it in one job per platform (docs/tasks/0016). On by
# default only for a top-level Windows build: on Linux it needs X11/Wayland
# development packages (docs/dependencies.md), so opt in with
# -DOPAS_BUILD_VIEWER=ON there.
set(opas_viewer_default OFF)
if(PROJECT_IS_TOP_LEVEL AND WIN32)
  set(opas_viewer_default ON)
endif()
option(OPAS_BUILD_VIEWER "Build the debug viewer, sandbox/viewer (fetches raylib, Dear ImGui)" ${opas_viewer_default})
if(OPAS_BUILD_VIEWER AND NOT OPAS_BUILD_PHYSICS)
  message(FATAL_ERROR "OPAS_BUILD_VIEWER needs OPAS_BUILD_PHYSICS (the viewer runs scenarios).")
endif()
if(OPAS_BUILD_TESTS AND NOT OPAS_BUILD_PHYSICS)
  message(FATAL_ERROR "OPAS_BUILD_TESTS needs OPAS_BUILD_PHYSICS (the test runner covers physics too).")
endif()
option(OPAS_SANITIZE "Build everything with AddressSanitizer (and UBSan where the compiler supports it)" OFF)
# Performance budget tests (task 0018, docs/budgets.md): opas_perf_tests is
# always built, but registered with ctest only when this is ON, so ordinary
# runs and CI never time anything. Use a Release build.
option(OPAS_PERF_TESTS "Register the performance budget tests (tests/perf) with ctest" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(OpasWarnings)
include(OpasSanitizers)

# Sanitizer flags must be set before dependencies are added so that Jolt and
# GoogleTest are instrumented the same way as our code.
opas_enable_sanitizers_globally()

include(OpasDependencies)

add_subdirectory(core)
# Always added: the interface is needed by core/motor; Jolt only with OPAS_BUILD_PHYSICS.
add_subdirectory(physics)
add_subdirectory(kinematic)
add_subdirectory(anim)
if(OPAS_BUILD_PHYSICS)
  add_subdirectory(sandbox)
endif()
if(PROJECT_IS_TOP_LEVEL)
  add_subdirectory(tools/validate_markers)
endif()

if(OPAS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)

  # Architecture check: Jolt headers only under physics/jolt/.
  set(jolt_check "${CMAKE_CURRENT_SOURCE_DIR}/tools/CheckJoltIncludes.cmake")
  add_test(NAME JoltIncludeCheck.Repository
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR} -P ${jolt_check})

  # Self-tests: the check must pass when Jolt is included only in the backend,
  # and fail when anything else includes it.
  set(fixture "${CMAKE_CURRENT_BINARY_DIR}/jolt_include_fixture")
  file(WRITE "${fixture}/allowed/physics/jolt/Backend.cpp" "#include <Jolt/Jolt.h>\n")
  file(WRITE "${fixture}/allowed/core/Clean.cpp" "#include <vector>\n")
  file(WRITE "${fixture}/allowed/build/Generated.cpp" "#include <Jolt/Jolt.h>\n")
  add_test(NAME JoltIncludeCheck.AllowsBackend
    COMMAND ${CMAKE_COMMAND} -DROOT=${fixture}/allowed -P ${jolt_check})
  # One fixture per way a leak could be written, each must be caught on its own.
  set(leaks
    "Spaced|core/Leak.hpp|  #  include \"Jolt/Physics/Body/Body.h\""
    "NoSpace|core/Leak.cpp|#include<Jolt/Jolt.h>"
    "Cxx|sandbox/Leak.cxx|#include <Jolt/Jolt.h>"
    "UpperCaseExtension|core/Leak.HPP|#include <Jolt/Jolt.h>"
    "ModuleInterface|core/Leak.ixx|#include <Jolt/Jolt.h>")
  foreach(leak IN LISTS leaks)
    string(REPLACE "|" ";" parts "${leak}")
    list(GET parts 0 name)
    list(GET parts 1 file)
    list(GET parts 2 line)
    file(WRITE "${fixture}/leak${name}/physics/jolt/Backend.cpp" "#include <Jolt/Jolt.h>\n")
    file(WRITE "${fixture}/leak${name}/${file}" "${line}\n")
    add_test(NAME JoltIncludeCheck.Rejects${name}
      COMMAND ${CMAKE_COMMAND} -DROOT=${fixture}/leak${name} -P ${jolt_check})
    set_tests_properties(JoltIncludeCheck.Rejects${name} PROPERTIES WILL_FAIL TRUE)
  endforeach()
endif()

if(OPAS_BUILD_TESTS)
  # Architecture check (M6): raylib and ImGui headers only under
  # sandbox/viewer/src/app/, with self-tests like the Jolt check's.
  set(viewer_check "${CMAKE_CURRENT_SOURCE_DIR}/tools/CheckViewerIncludes.cmake")
  add_test(NAME ViewerIncludeCheck.Repository
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR} -P ${viewer_check})
  set(vfixture "${CMAKE_CURRENT_BINARY_DIR}/viewer_include_fixture")
  file(WRITE "${vfixture}/allowed/sandbox/viewer/src/app/Main.cpp"
    "#include <raylib.h>\n#include \"imgui.h\"\n#include <rlImGui.h>\n#include <rlgl.h>\n")
  file(WRITE "${vfixture}/allowed/core/Clean.cpp" "#include <vector>\n")
  add_test(NAME ViewerIncludeCheck.AllowsTheApp
    COMMAND ${CMAKE_COMMAND} -DROOT=${vfixture}/allowed -P ${viewer_check})
  set(vleaks
    "RaylibInCore|core/Leak.cpp|#include <raylib.h>"
    "ImguiInModel|sandbox/viewer/src/Scene.cpp|  #  include \"imgui.h\""
    "ImguiInternal|tests/Leak.hpp|#include<imgui_internal.h>"
    "Rlgl|core/debug/Leak.hpp|#include <rlgl.h>"
    "RlImGui|sandbox/runner/Leak.cxx|#include <rlImGui.h>"
    "PathPrefix|core/Leak.cpp|#include \"imgui/imgui.h\""
    "UpperCase|core/Leak.cpp|#include <Raylib.h>"
    "Raygui|sandbox/runner/Leak.hpp|#include <raygui.h>"
    "ImguiConfig|tests/Leak.cpp|#include \"../imconfig.h\"")
  foreach(leak IN LISTS vleaks)
    string(REPLACE "|" ";" parts "${leak}")
    list(GET parts 0 name)
    list(GET parts 1 file)
    list(GET parts 2 line)
    file(WRITE "${vfixture}/leak${name}/${file}" "${line}\n")
    add_test(NAME ViewerIncludeCheck.Rejects${name}
      COMMAND ${CMAKE_COMMAND} -DROOT=${vfixture}/leak${name} -P ${viewer_check})
    set_tests_properties(ViewerIncludeCheck.Rejects${name} PROPERTIES WILL_FAIL TRUE)
  endforeach()
endif()

if(OPAS_BUILD_TESTS)
  # Architecture check (task 0020, C3b): cgltf headers only under
  # anim/gltf/src/, with self-tests like the viewer check's.
  set(gltf_check "${CMAKE_CURRENT_SOURCE_DIR}/tools/CheckGltfIncludes.cmake")
  add_test(NAME GltfIncludeCheck.Repository
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_CURRENT_SOURCE_DIR} -P ${gltf_check})
  set(gfixture "${CMAKE_CURRENT_BINARY_DIR}/gltf_include_fixture")
  file(WRITE "${gfixture}/allowed/anim/gltf/src/Reader.cpp" "#include <cgltf.h>\n")
  file(WRITE "${gfixture}/allowed/anim/clip/Clean.cpp" "#include <vector>\n")
  add_test(NAME GltfIncludeCheck.AllowsTheReader
    COMMAND ${CMAKE_COMMAND} -DROOT=${gfixture}/allowed -P ${gltf_check})
  set(gleaks
    "InPublicHeader|anim/gltf/include/opas/anim/Leak.hpp|#include <cgltf.h>"
    "InTests|tests/Leak.cpp|  #  include \"cgltf.h\""
    "Writer|core/Leak.cpp|#include<cgltf_write.h>"
    "PathPrefix|sandbox/Leak.cpp|#include \"cgltf/cgltf.h\""
    "UpperCase|anim/clip/Leak.cpp|#include <CGLTF.h>")
  foreach(leak IN LISTS gleaks)
    string(REPLACE "|" ";" parts "${leak}")
    list(GET parts 0 name)
    list(GET parts 1 file)
    list(GET parts 2 line)
    file(WRITE "${gfixture}/leak${name}/${file}" "${line}\n")
    add_test(NAME GltfIncludeCheck.Rejects${name}
      COMMAND ${CMAKE_COMMAND} -DROOT=${gfixture}/leak${name} -P ${gltf_check})
    set_tests_properties(GltfIncludeCheck.Rejects${name} PROPERTIES WILL_FAIL TRUE)
  endforeach()
endif()
