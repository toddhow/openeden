// Eden Archive reference library. Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
#include <edenarchive/fuzz.hpp>

#include <algorithm>
#include <vector>

#include <edenarchive/archive.hpp>
#include <edenarchive/validator.hpp>

namespace EdenArchive
{
    bool fuzzOneInput(const std::uint8_t* data, std::size_t size) noexcept
    {
        // Tight caps: a fuzz input is small, and every limit below keeps one run fast and its memory small.
        constexpr std::uint64_t metadataCap = std::uint64_t{ 16 } << 20;
        constexpr std::uint64_t decodeBudget = std::uint64_t{ 64 } << 20;
        constexpr std::size_t maxEntriesTouched = 2000;
        try
        {
            std::vector<std::uint8_t> bytes(data, data + size);
            const std::shared_ptr<const Source> source = makeMemorySource(std::move(bytes));
            OpenOptions options;
            options.mMaxMetadataSize = metadataCap;
            Error error;
            const std::unique_ptr<Archive> archive = Archive::open(source, options, error);
            if (!archive)
                return false;

            DecodeBudget budget;
            budget.mRemaining = decodeBudget;
            const std::size_t count = std::min<std::size_t>(archive->entryCount(), maxEntriesTouched);
            std::vector<std::uint8_t> content;
            for (std::size_t i = 0; i < count; ++i)
            {
                const EntryInfo entry = archive->entry(i);
                if (entry.mHasPath)
                    (void)archive->findPath(entry.mPath);
                if (entry.mHasKey)
                    (void)archive->findKey(entry.mKey);
                (void)archive->provenance(i);
                if (entry.mSize <= decodeBudget)
                    (void)archive->readEntry(i, content, &budget);
                // A few ranges, including the first and last byte and one that spans a chunk boundary.
                if (entry.mSize > 0)
                {
                    std::uint8_t one = 0;
                    (void)archive->readRange(i, 0, 1, &one, &budget);
                    (void)archive->readRange(i, entry.mSize - 1, 1, &one, &budget);
                    const std::uint64_t middle = entry.mSize / 2;
                    const std::size_t span = static_cast<std::size_t>(std::min<std::uint64_t>(entry.mSize - middle, 4096));
                    content.resize(span);
                    (void)archive->readRange(i, middle, span, content.data(), &budget);
                }
            }
            ValidateOptions validateOptions;
            validateOptions.mDecodeBudget = decodeBudget;
            validateOptions.mMaxFindings = 10;
            (void)validate(source, validateOptions);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}
