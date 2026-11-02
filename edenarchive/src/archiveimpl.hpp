// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// The private state of an open Archive. Shared by archive.cpp and validator.cpp. Internal.
#ifndef EDENARCHIVE_SRC_ARCHIVEIMPL_HPP
#define EDENARCHIVE_SRC_ARCHIVEIMPL_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include <edenarchive/archive.hpp>

namespace EdenArchive
{
    /// Owned by Archive (through std::unique_ptr). Filled once by Archive::open and never changed afterwards, so any
    /// number of threads may read it.
    struct Archive::Impl
    {
        std::shared_ptr<const Source> mSource;
        PackInfo mInfo;
        /// The whole metadata region, read once at open (at most the caller's cap, which is at most 2^32).
        std::vector<std::uint8_t> mMeta;
        /// Per entry: 1 if every chunk but the last has exactly the pack's chunk size (so the chunk holding a given
        /// offset is found by division), else 0.
        std::vector<std::uint8_t> mUniform;
        /// For an entry whose chunks are not all the pack's chunk size: the content offset at which each of its
        /// chunks starts, so that a read finds its chunk by binary search. mStartsOf[entry] is the index of the first
        /// of them in mStarts (unused for uniform entries).
        std::vector<std::uint32_t> mStartsOf;
        std::vector<std::uint64_t> mStarts;

        /// Pointer to the metadata byte at absolute file offset `offset` (which must be inside the region).
        const std::uint8_t* at(std::uint64_t offset) const noexcept
        {
            return mMeta.data() + (offset - mInfo.mMetadataOffset);
        }
        const std::uint8_t* entryRecord(std::size_t index) const noexcept
        {
            return at(mInfo.mEntryTableOffset + index * mInfo.mEntryRecordSize);
        }
        std::uint64_t entryRecordOffset(std::size_t index) const noexcept
        {
            return mInfo.mEntryTableOffset + index * mInfo.mEntryRecordSize;
        }
    };
}

#endif
