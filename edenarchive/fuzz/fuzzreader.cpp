// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// libFuzzer entry point: clang++ -fsanitize=fuzzer,address,undefined (see edenarchive/CMakeLists.txt).
#include <cstddef>
#include <cstdint>

#include <edenarchive/fuzz.hpp>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    EdenArchive::fuzzOneInput(data, size);
    return 0;
}
