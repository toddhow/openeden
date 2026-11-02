// Eden Archive test support (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// Byte-level changes to a finished pack, with the recomputations that Annex C of the specification describes.
#include <algorithm>

#include "eapackmodel.hpp"

namespace EdenTest
{
    void patch(Bytes& pack, std::size_t offset, std::uint64_t value, unsigned width)
    {
        putLe(pack, offset, value, width);
    }

    namespace
    {
        void rebuildPathIndexInBytes(Bytes& pack)
        {
            const std::uint64_t entryCount = std::min<std::uint64_t>(getLe(pack, Fmt::hEntryCount, 8), 1u << 20);
            const std::uint64_t entryTable = getLe(pack, Fmt::hEntryTableOffset, 8);
            const std::uint64_t stride = getLe(pack, Fmt::hEntryRecordSize, 4);
            const std::uint64_t stringOffset = getLe(pack, Fmt::hStringTableOffset, 8);
            const std::uint64_t stringSize = getLe(pack, Fmt::hStringTableSize, 8);
            const std::uint64_t indexOffset = getLe(pack, Fmt::hPathIndexOffset, 8);
            const std::uint64_t pathCount = getLe(pack, Fmt::hPathEntryCount, 8);

            std::vector<IndexRec> records;
            for (std::uint64_t i = 0; i < entryCount; ++i)
            {
                const std::uint64_t at = entryTable + i * stride;
                if ((getLe(pack, at + Fmt::eFlags, 4) & Fmt::entryHasPath) == 0)
                    continue;
                const std::uint64_t pathOffset = getLe(pack, at + Fmt::ePathOffset, 8);
                const std::uint64_t pathLength = getLe(pack, at + Fmt::ePathLength, 4);
                std::string path;
                if (pathOffset <= stringSize && pathLength <= stringSize - pathOffset
                    && stringOffset + pathOffset + pathLength <= pack.size())
                {
                    const auto first = pack.begin() + static_cast<std::ptrdiff_t>(stringOffset + pathOffset);
                    path.assign(first, first + static_cast<std::ptrdiff_t>(pathLength));
                }
                records.push_back({ pathHashOf(path), i });
            }
            std::sort(records.begin(), records.end(), [](const IndexRec& a, const IndexRec& b) {
                return a.mFirst != b.mFirst ? a.mFirst < b.mFirst : a.mEntryIndex < b.mEntryIndex;
            });
            for (std::size_t i = 0; i < records.size() && i < pathCount; ++i)
            {
                putLe(pack, indexOffset + 16 * i, records[i].mFirst, 8);
                putLe(pack, indexOffset + 16 * i + 8, records[i].mEntryIndex, 8);
            }
        }
    }

    void recompute(Bytes& pack, const Recompute& what)
    {
        if (pack.size() < Fmt::headerSize)
            return;
        if (what.mPathIndex)
            rebuildPathIndexInBytes(pack);

        const std::size_t footerPos
            = pack.size() >= Fmt::footerSize + Fmt::headerSize ? pack.size() - Fmt::footerSize : pack.size();
        if (what.mMetadataDigest)
        {
            // The range the header names, clamped to the file before the footer, as a reader would read it.
            const std::uint64_t offset = std::min<std::uint64_t>(getLe(pack, Fmt::hMetadataOffset, 8), footerPos);
            const std::uint64_t size = getLe(pack, Fmt::hMetadataSize, 8);
            const std::uint64_t end = size > footerPos - offset ? footerPos : offset + size;
            const Digest digest = blake3Of(pack.data() + offset, static_cast<std::size_t>(end - offset));
            std::copy(digest.begin(), digest.end(), pack.begin() + Fmt::hMetadataDigest);
        }
        if (what.mHeaderChecksum)
        {
            const Digest digest = blake3Of(pack.data(), Fmt::hHeaderChecksum);
            std::copy(digest.begin(), digest.end(), pack.begin() + Fmt::hHeaderChecksum);
        }
        if (what.mFooter && footerPos != pack.size())
        {
            const auto copy = [&](std::size_t from, std::size_t to, std::size_t length) {
                std::copy(pack.begin() + static_cast<std::ptrdiff_t>(from),
                    pack.begin() + static_cast<std::ptrdiff_t>(from + length),
                    pack.begin() + static_cast<std::ptrdiff_t>(footerPos + to));
            };
            copy(Fmt::hVersionMajor, Fmt::fVersionMajor, 4); // major and minor
            copy(Fmt::hMetadataOffset, Fmt::fMetadataOffset, 8);
            copy(Fmt::hMetadataSize, Fmt::fMetadataSize, 8);
            copy(Fmt::hMetadataDigest, Fmt::fMetadataDigest, 32);
            copy(Fmt::hHeaderChecksum, Fmt::fHeaderChecksum, 32);
        }
    }
}
