// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#ifndef EDENARCHIVE_SOURCE_HPP
#define EDENARCHIVE_SOURCE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "error.hpp"

namespace EdenArchive
{
    /// Where a pack's bytes come from. Every read is positional (no shared file position, no memory mapping).
    /// Implementations MUST be safe to call from several threads at once.
    class Source
    {
    public:
        virtual ~Source() = default;
        virtual std::uint64_t size() const noexcept = 0;
        /// Copies exactly `size` bytes from `offset`. Returns false (and the destination is unspecified) when the
        /// range is not entirely inside the source or the system gave fewer bytes. Never throws.
        virtual bool read(std::uint64_t offset, void* destination, std::size_t size) const noexcept = 0;
    };

    /// Opens a file once, read-only, and reads it with positional reads (pread on POSIX, ReadFile with an offset on
    /// Windows) from that one handle. Returns nullptr and sets `error` (class Io) on failure.
    std::shared_ptr<const Source> openFileSource(const std::filesystem::path& path, Error& error);

    /// A source over bytes held in memory (the bytes are copied).
    std::shared_ptr<const Source> makeMemorySource(const std::vector<std::uint8_t>& bytes);
    std::shared_ptr<const Source> makeMemorySource(std::vector<std::uint8_t>&& bytes);
}

#endif
