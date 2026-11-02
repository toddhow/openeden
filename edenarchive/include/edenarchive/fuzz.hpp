// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_FUZZ_HPP
#define EDENARCHIVE_FUZZ_HPP

#include <cstddef>
#include <cstdint>

namespace EdenArchive
{
    /// The one body shared by the libFuzzer target and by the deterministic mutation test (task 0052, stage f).
    /// Treats `data` as a pack held in memory: opens it with tight caps, and if it opens, looks every entry up,
    /// reads every entry in whole and by ranges against a small decoded-byte budget, and runs the validator.
    /// Must never crash, hang, throw or allocate without bound, whatever the bytes are. Returns true if the bytes
    /// opened as a pack.
    bool fuzzOneInput(const std::uint8_t* data, std::size_t size) noexcept;
}

#endif
