# Eden Archive reference library (task 0052). Apache-2.0, see LICENSE. It depends on nothing of the engine: only the
# C++ standard library, BLAKE3 and zstd (fetched and pinned by extern/CMakeLists.txt, docs/dependencies.md).

if (NOT OPENEDEN_ZSTD_SOURCE_DIR OR NOT OPENEDEN_BLAKE3_SOURCE_DIR)
    message(FATAL_ERROR "edenarchive: extern/CMakeLists.txt did not fetch zstd and BLAKE3")
endif()

# Third-party code is built without the project's warning flags and without anything that needs an assembler.
function(edenarchive_quiet target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W0)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

# zstd 1.5.7: the library sources only, single-threaded (the specification wants nbWorkers 0), no legacy formats, no
# assembler. XXH_NAMESPACE keeps its copy of xxHash from clashing with any other xxHash in the program.
set(ZSTD_LIB "${OPENEDEN_ZSTD_SOURCE_DIR}/lib")
file(GLOB ZSTD_SOURCES "${ZSTD_LIB}/common/*.c" "${ZSTD_LIB}/compress/*.c" "${ZSTD_LIB}/decompress/*.c")
add_library(edenarchive_zstd STATIC ${ZSTD_SOURCES})
edenarchive_quiet(edenarchive_zstd)
target_include_directories(edenarchive_zstd SYSTEM PUBLIC "${ZSTD_LIB}")
target_compile_definitions(edenarchive_zstd
    PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0 XXH_NAMESPACE=ZSTD_
    # OpenMW's Windows dependency set puts a zstd.h on the global include path; this names the pinned one.
    PUBLIC "EDENARCHIVE_ZSTD_H=\"${ZSTD_LIB}/zstd.h\"")

# BLAKE3 1.8.2: the C implementation. On x86-64 the SIMD code is the intrinsics version (no assembler needed), each
# file with the instruction-set flag its compiler needs; the dispatcher picks one at run time.
set(BLAKE3_C "${OPENEDEN_BLAKE3_SOURCE_DIR}/c")
set(BLAKE3_SOURCES "${BLAKE3_C}/blake3.c" "${BLAKE3_C}/blake3_dispatch.c" "${BLAKE3_C}/blake3_portable.c")
set(BLAKE3_DEFINITIONS "")
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
    list(APPEND BLAKE3_SOURCES
        "${BLAKE3_C}/blake3_sse2.c" "${BLAKE3_C}/blake3_sse41.c" "${BLAKE3_C}/blake3_avx2.c" "${BLAKE3_C}/blake3_avx512.c")
    if (NOT MSVC)
        set_source_files_properties("${BLAKE3_C}/blake3_sse2.c" PROPERTIES COMPILE_OPTIONS "-msse2")
        set_source_files_properties("${BLAKE3_C}/blake3_sse41.c" PROPERTIES COMPILE_OPTIONS "-msse4.1")
        set_source_files_properties("${BLAKE3_C}/blake3_avx2.c" PROPERTIES COMPILE_OPTIONS "-mavx2")
        set_source_files_properties("${BLAKE3_C}/blake3_avx512.c" PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx512vl")
    endif()
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|AARCH64|arm64|ARM64)$")
    list(APPEND BLAKE3_SOURCES "${BLAKE3_C}/blake3_neon.c")
    list(APPEND BLAKE3_DEFINITIONS BLAKE3_USE_NEON=1)
else()
    list(APPEND BLAKE3_DEFINITIONS BLAKE3_USE_NEON=0 BLAKE3_NO_SSE2 BLAKE3_NO_SSE41 BLAKE3_NO_AVX2 BLAKE3_NO_AVX512)
endif()
add_library(edenarchive_blake3 STATIC ${BLAKE3_SOURCES})
edenarchive_quiet(edenarchive_blake3)
target_include_directories(edenarchive_blake3 SYSTEM PUBLIC "${BLAKE3_C}")
target_compile_definitions(edenarchive_blake3 PRIVATE ${BLAKE3_DEFINITIONS})

# The library itself.
add_library(edenarchive STATIC
    src/archive.cpp
    src/error.cpp
    src/extract.cpp
    src/fuzz.cpp
    src/hash.cpp
    src/manifest.cpp
    src/source.cpp
    src/utf8.cpp
    src/validator.cpp
    src/writer.cpp
)
target_include_directories(edenarchive PUBLIC include PRIVATE src)
# The pinned zstd.h first, before any zstd.h that the build environment puts on the include path (OpenMW Windows deps).
target_include_directories(edenarchive SYSTEM BEFORE PRIVATE "${ZSTD_LIB}")
target_compile_features(edenarchive PUBLIC cxx_std_20)
target_link_libraries(edenarchive PUBLIC edenarchive_blake3 edenarchive_zstd)
find_package(Threads REQUIRED)
target_link_libraries(edenarchive PRIVATE Threads::Threads)

# Unit tests (an own executable, so that the permissive tree builds and tests without the engine), the independent
# test writer they share with the conformance corpus generator, and the generator itself (corpus/README.md).
if (BUILD_COMPONENTS_TESTS)
    file(GLOB EDENARCHIVE_SUPPORT_SOURCES CONFIGURE_DEPENDS testsupport/*.cpp)
    file(GLOB EDENARCHIVE_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    if (EDENARCHIVE_SUPPORT_SOURCES)
        add_library(edenarchive-testsupport STATIC ${EDENARCHIVE_SUPPORT_SOURCES})
        target_include_directories(edenarchive-testsupport PUBLIC testsupport)
        target_link_libraries(edenarchive-testsupport PUBLIC edenarchive)
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/corpus/corpusgen.cpp")
            add_executable(edenarchive-corpusgen corpus/corpusgen.cpp)
            target_link_libraries(edenarchive-corpusgen PRIVATE edenarchive-testsupport)
        endif()
    endif()
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench/benchread.cpp")
        add_executable(edenarchive-bench bench/benchread.cpp)
        target_link_libraries(edenarchive-bench PRIVATE edenarchive)
    endif()
    if (EDENARCHIVE_TEST_SOURCES)
        add_executable(edenarchive-tests ${EDENARCHIVE_TEST_SOURCES})
        target_link_libraries(edenarchive-tests PRIVATE edenarchive GTest::GTest Threads::Threads)
        if (TARGET edenarchive-testsupport)
            target_link_libraries(edenarchive-tests PRIVATE edenarchive-testsupport)
        endif()
        target_include_directories(edenarchive-tests PRIVATE src)
        target_compile_definitions(edenarchive-tests PRIVATE
            EDENARCHIVE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")
        if (WIN32)
            target_sources(edenarchive-tests PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
        endif()
    endif()
endif()

# A libFuzzer target, only on request and only where the compiler has the fuzzer runtime (Clang; GCC has none).
# The same input function also runs as a plain unit test (tests/), so CI exercises it without libFuzzer.
option(EDENARCHIVE_BUILD_FUZZER "Build the libFuzzer target of the Eden Archive reader" OFF)
if (EDENARCHIVE_BUILD_FUZZER AND NOT MSVC)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
    check_cxx_source_compiles("#include <cstdint>
#include <cstddef>
extern \"C\" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }
" EDENARCHIVE_HAVE_LIBFUZZER)
    unset(CMAKE_REQUIRED_FLAGS)
    if (EDENARCHIVE_HAVE_LIBFUZZER)
        add_executable(edenarchive-fuzz fuzz/fuzzreader.cpp)
        target_link_libraries(edenarchive-fuzz PRIVATE edenarchive)
        target_compile_options(edenarchive-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(edenarchive-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
    else()
        message(WARNING "edenarchive: this compiler has no -fsanitize=fuzzer; the libFuzzer target is not built")
    endif()
endif()
