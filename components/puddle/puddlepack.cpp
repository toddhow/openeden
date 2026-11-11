#include "puddlepack.hpp"

#include <algorithm>
#include <charconv>

#include <edenarchive/hash.hpp>
#include <edenarchive/writer.hpp>

namespace Puddle
{
    namespace
    {
        Error fail(const char* field, std::uint64_t offset, std::string message)
        {
            return Error{ field, offset, std::move(message) };
        }

        Error fromArchive(const EdenArchive::Error& error, const char* what)
        {
            return fail(what, error.mOffset, error.describe());
        }

        // The manifest: every value an integer, in this order. Changing the order or the keys is a layout change.
        struct Field
        {
            const char* mKey;
            std::int64_t mValue;
        };

        std::vector<Field> fieldsOf(const Manifest& m)
        {
            const BakeSettings& s = m.mSettings;
            return {
                { "layoutVersion", m.mLayoutVersion },
                { "bakerVersion", m.mBakerVersion },
                { "spacingUnits", s.mSpacingUnits },
                { "capacityCapSteps", s.mCapacityCapSteps },
                { "minPondTexels", s.mMinPondTexels },
                { "catchmentRadiusUnits", s.mCatchmentRadiusUnits },
                { "halo", s.mHalo },
                { "statics", s.mStatics ? 1 : 0 },
                { "waterLevelUnits", s.mWaterLevelUnits },
                { "cellSizeUnits", s.mCellSizeUnits },
                { "cellVertices", s.mCellVertices },
            };
        }

        void hashBytes(EdenArchive::Hasher& hasher, std::uint64_t value)
        {
            std::uint8_t bytes[8];
            for (int i = 0; i < 8; ++i)
                bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
            hasher.update(bytes, 8);
        }
    }

    std::string writeManifestJson(const Manifest& manifest)
    {
        std::string out = "{";
        bool first = true;
        for (const Field& field : fieldsOf(manifest))
        {
            if (!first)
                out += ',';
            first = false;
            out += '"';
            out += field.mKey;
            out += "\":";
            out += std::to_string(field.mValue);
        }
        out += '}';
        return out;
    }

    Error parseManifestJson(std::string_view text, Manifest& manifest)
    {
        Manifest parsed;
        const std::vector<Field> layout = fieldsOf(parsed);
        std::vector<std::int64_t> values;
        std::size_t pos = 0;
        auto expect = [&](std::string_view literal) {
            if (text.substr(pos, literal.size()) != literal)
                return false;
            pos += literal.size();
            return true;
        };
        if (!expect("{"))
            return fail("manifest", pos, "expected '{'");
        for (std::size_t i = 0; i < layout.size(); ++i)
        {
            if (i != 0 && !expect(","))
                return fail("manifest", pos, "expected ','");
            const std::string key = std::string("\"") + layout[i].mKey + "\":";
            if (!expect(key))
                return fail("manifest", pos, std::string("expected the key ") + layout[i].mKey);
            std::int64_t value = 0;
            const char* begin = text.data() + pos;
            const char* end = text.data() + text.size();
            const auto [stop, ec] = std::from_chars(begin, end, value);
            if (ec != std::errc())
                return fail("manifest", pos, "expected an integer");
            // Canonical form: no '+', no leading zero, no "-0".
            const std::string canonical = std::to_string(value);
            if (static_cast<std::size_t>(stop - begin) != canonical.size())
                return fail("manifest", pos, "the integer is not in canonical form");
            pos = static_cast<std::size_t>(stop - text.data());
            values.push_back(value);
        }
        if (!expect("}") || pos != text.size())
            return fail("manifest", pos, "expected the end of the manifest");
        BakeSettings& s = parsed.mSettings;
        auto u32 = [&](std::size_t i, std::uint32_t& out) {
            if (values[i] < 0 || values[i] > 0xFFFFFFFFll)
                return false;
            out = static_cast<std::uint32_t>(values[i]);
            return true;
        };
        bool ok = u32(0, parsed.mLayoutVersion) && u32(1, parsed.mBakerVersion) && u32(2, s.mSpacingUnits)
            && u32(3, s.mCapacityCapSteps) && u32(4, s.mMinPondTexels) && u32(5, s.mCatchmentRadiusUnits)
            && u32(6, s.mHalo) && u32(9, s.mCellSizeUnits) && u32(10, s.mCellVertices);
        if (!ok || (values[7] != 0 && values[7] != 1) || values[8] < -0x7FFFFFFFll || values[8] > 0x7FFFFFFFll)
            return fail("manifest", 0, "a value is out of range");
        s.mStatics = values[7] == 1;
        s.mWaterLevelUnits = static_cast<std::int32_t>(values[8]);
        manifest = parsed;
        return {};
    }

    Digest contentDigest(const Manifest& manifest, const std::vector<std::pair<std::uint64_t, Digest>>& cellInputs)
    {
        std::vector<std::pair<std::uint64_t, Digest>> sorted = cellInputs;
        std::sort(sorted.begin(), sorted.end());
        EdenArchive::Hasher hasher;
        const std::string json = writeManifestJson(manifest);
        hasher.update(json.data(), json.size());
        hashBytes(hasher, sorted.size());
        for (const auto& [key, digest] : sorted)
        {
            hashBytes(hasher, key);
            hasher.update(digest.data(), digest.size());
        }
        return hasher.finalize();
    }

    namespace
    {
        Error buildEntries(const Manifest& manifest, const std::vector<PackedCell>& cells, const PackOptions& options,
            std::vector<EdenArchive::InputEntry>& entries)
        {
            std::vector<std::uint64_t> keys;
            keys.reserve(cells.size());
            for (const PackedCell& cell : cells)
            {
                std::uint64_t key = 0;
                if (!packCellKey(cell.mCellX, cell.mCellY, key))
                    return fail("cell", 0, "the cell coordinates do not pack into a key");
                if (cell.mMap.mCellX != cell.mCellX || cell.mMap.mCellY != cell.mCellY)
                    return fail("cell", 0, "the map's cell differs from the entry's cell");
                keys.push_back(key);
                EdenArchive::InputEntry entry;
                entry.mKey = key;
                entry.mType = entryTypePuddleCell;
                if (Error error = writeCell(cell.mMap, entry.mData))
                    return error;
                entry.mHintFlags = EdenArchive::entryFlagHintMemSize;
                entry.mHintMemorySize = static_cast<std::uint32_t>(entry.mData.size());
                entry.mLayoutParamA = cell.mMap.mSide;
                entry.mCompression = EdenArchive::Compression::Auto;
                entry.mProvenance = EdenArchive::ProvenanceInput{ std::string(bakerToolId), cell.mMap.mInputDigest,
                    std::vector<std::uint8_t>(cell.mMap.mParameterDigest.begin(), cell.mMap.mParameterDigest.end()) };
                entries.push_back(std::move(entry));
            }
            std::sort(keys.begin(), keys.end());
            if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
                return fail("cell", 0, "two cells have the same coordinates");

            EdenArchive::InputEntry manifestEntry;
            manifestEntry.mPath = std::string(manifestPath);
            manifestEntry.mType = 0;
            const std::string json = writeManifestJson(manifest);
            manifestEntry.mData.assign(json.begin(), json.end());
            manifestEntry.mProvenance
                = EdenArchive::ProvenanceInput{ std::string(bakerToolId), options.mSourceDigest, {} };
            entries.push_back(std::move(manifestEntry));
            return {};
        }

        EdenArchive::WriteOptions writeOptionsFor(const PackOptions& options)
        {
            EdenArchive::WriteOptions write;
            write.mDerived = true;
            write.mGeneration = options.mGeneration;
            write.mSourceDigest = options.mSourceDigest;
            write.mThreads = options.mThreads;
            write.mStoreOnly = options.mStoreOnly;
            return write;
        }
    }

    Error writePuddlePackFromFiles(const Manifest& manifest, const std::vector<CellFile>& cells,
        const PackOptions& options, const std::filesystem::path& path)
    {
        std::vector<EdenArchive::InputEntry> entries;
        std::vector<std::uint64_t> keys;
        for (const CellFile& cell : cells)
        {
            std::uint64_t key = 0;
            if (!packCellKey(cell.mCellX, cell.mCellY, key))
                return fail("cell", 0, "the cell coordinates do not pack into a key");
            keys.push_back(key);
            EdenArchive::InputEntry entry;
            entry.mKey = key;
            entry.mType = entryTypePuddleCell;
            entry.mFile = cell.mPath;
            entry.mHintFlags = EdenArchive::entryFlagHintMemSize;
            entry.mHintMemorySize = static_cast<std::uint32_t>(std::min<std::uint64_t>(cell.mSize, 0xFFFFFFFFu));
            entry.mLayoutParamA = cell.mSide;
            entry.mCompression = EdenArchive::Compression::Auto;
            entry.mProvenance = EdenArchive::ProvenanceInput{ std::string(bakerToolId), cell.mInputDigest,
                std::vector<std::uint8_t>(cell.mParameterDigest.begin(), cell.mParameterDigest.end()) };
            entries.push_back(std::move(entry));
        }
        std::sort(keys.begin(), keys.end());
        if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
            return fail("cell", 0, "two cells have the same coordinates");
        EdenArchive::InputEntry manifestEntry;
        manifestEntry.mPath = std::string(manifestPath);
        manifestEntry.mType = 0;
        const std::string json = writeManifestJson(manifest);
        manifestEntry.mData.assign(json.begin(), json.end());
        manifestEntry.mProvenance = EdenArchive::ProvenanceInput{ std::string(bakerToolId), options.mSourceDigest, {} };
        entries.push_back(std::move(manifestEntry));
        const EdenArchive::Error error = EdenArchive::writePack(entries, path, writeOptionsFor(options));
        if (error)
            return fromArchive(error, "pack");
        return {};
    }

    Error buildPuddlePack(const Manifest& manifest, const std::vector<PackedCell>& cells, const PackOptions& options,
        std::vector<std::uint8_t>& out)
    {
        out.clear();
        std::vector<EdenArchive::InputEntry> entries;
        if (Error error = buildEntries(manifest, cells, options, entries))
            return error;
        const EdenArchive::Error error = EdenArchive::writePackToMemory(entries, out, writeOptionsFor(options));
        if (error)
        {
            out.clear();
            return fromArchive(error, "pack");
        }
        return {};
    }

    Error writePuddlePack(const Manifest& manifest, const std::vector<PackedCell>& cells, const PackOptions& options,
        const std::filesystem::path& path)
    {
        std::vector<EdenArchive::InputEntry> entries;
        if (Error error = buildEntries(manifest, cells, options, entries))
            return error;
        const EdenArchive::Error error = EdenArchive::writePack(entries, path, writeOptionsFor(options));
        if (error)
            return fromArchive(error, "pack");
        return {};
    }

    struct PuddlePack::Impl
    {
        std::unique_ptr<EdenArchive::Archive> mArchive;
        Manifest mManifest;
        std::vector<std::pair<std::int32_t, std::int32_t>> mCells;
    };

    PuddlePack::PuddlePack(std::unique_ptr<Impl> impl)
        : mImpl(std::move(impl))
    {
    }

    PuddlePack::~PuddlePack() = default;

    std::unique_ptr<PuddlePack> PuddlePack::open(const std::filesystem::path& path, Error& error)
    {
        EdenArchive::Error archiveError;
        std::shared_ptr<const EdenArchive::Source> source = EdenArchive::openFileSource(path, archiveError);
        if (!source)
        {
            error = fromArchive(archiveError, "pack");
            return nullptr;
        }
        return open(std::move(source), error);
    }

    std::unique_ptr<PuddlePack> PuddlePack::open(std::shared_ptr<const EdenArchive::Source> source, Error& error)
    {
        error = {};
        EdenArchive::Error archiveError;
        auto impl = std::make_unique<Impl>();
        impl->mArchive = EdenArchive::Archive::open(std::move(source), EdenArchive::OpenOptions{}, archiveError);
        if (!impl->mArchive)
        {
            error = fromArchive(archiveError, "pack");
            return nullptr;
        }
        const EdenArchive::Archive& archive = *impl->mArchive;
        if ((archive.info().mPackFlags & EdenArchive::packFlagDerived) == 0)
        {
            error = fail("packFlags", 0, "the pack is not a derived pack");
            return nullptr;
        }
        const std::optional<std::size_t> manifestIndex = archive.findPath(manifestPath);
        if (!manifestIndex)
        {
            error = fail("manifest", 0, "the pack has no puddles/manifest.json entry");
            return nullptr;
        }
        std::vector<std::uint8_t> manifestBytes;
        EdenArchive::DecodeBudget budget{ std::uint64_t{ 1 } << 20 };
        archiveError = archive.readEntry(*manifestIndex, manifestBytes, &budget);
        if (archiveError)
        {
            error = fromArchive(archiveError, "manifest");
            return nullptr;
        }
        error = parseManifestJson(
            std::string_view(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size()),
            impl->mManifest);
        if (error)
            return nullptr;
        if (impl->mManifest.mLayoutVersion != layoutVersion)
        {
            error = fail("layoutVersion", 0, "the pack was made with another puddle layout version");
            return nullptr;
        }
        std::vector<std::uint64_t> keys;
        for (std::size_t i = 0; i < archive.entryCount(); ++i)
        {
            const EdenArchive::EntryInfo entry = archive.entry(i);
            if (!entry.mHasKey || (entry.mKey >> 56) != tagPuddleCell)
                continue;
            keys.push_back(entry.mKey);
        }
        std::sort(keys.begin(), keys.end());
        for (const std::uint64_t key : keys)
        {
            std::int32_t x = 0;
            std::int32_t y = 0;
            if (!unpackCellKey(key, x, y))
            {
                error = fail("cellKey", 0, "a cell entry has a key that is not a valid cell key");
                return nullptr;
            }
            impl->mCells.emplace_back(x, y);
        }
        return std::unique_ptr<PuddlePack>(new PuddlePack(std::move(impl)));
    }

    const Manifest& PuddlePack::manifest() const noexcept
    {
        return mImpl->mManifest;
    }

    std::uint64_t PuddlePack::generation() const noexcept
    {
        return mImpl->mArchive->info().mGeneration;
    }

    const Digest& PuddlePack::sourceDigest() const noexcept
    {
        return mImpl->mArchive->info().mSourceDigest;
    }

    const std::vector<std::pair<std::int32_t, std::int32_t>>& PuddlePack::cells() const noexcept
    {
        return mImpl->mCells;
    }

    bool PuddlePack::contains(std::int32_t cellX, std::int32_t cellY) const noexcept
    {
        std::uint64_t key = 0;
        return packCellKey(cellX, cellY, key) && mImpl->mArchive->findKey(key).has_value();
    }

    std::optional<Digest> PuddlePack::inputDigest(std::int32_t cellX, std::int32_t cellY) const
    {
        std::uint64_t key = 0;
        if (!packCellKey(cellX, cellY, key))
            return std::nullopt;
        const std::optional<std::size_t> index = mImpl->mArchive->findKey(key);
        if (!index)
            return std::nullopt;
        const std::optional<EdenArchive::ProvenanceInfo> provenance = mImpl->mArchive->provenance(*index);
        if (!provenance)
            return std::nullopt;
        return provenance->mSourceDigest;
    }

    Error PuddlePack::readCell(std::int32_t cellX, std::int32_t cellY, CellMap& cell) const
    {
        cell = CellMap{};
        std::uint64_t key = 0;
        if (!packCellKey(cellX, cellY, key))
            return fail("cell", 0, "the cell coordinates do not pack into a key");
        const std::optional<std::size_t> index = mImpl->mArchive->findKey(key);
        if (!index)
            return fail("cell", 0, "the pack has no such cell");
        std::vector<std::uint8_t> bytes;
        EdenArchive::DecodeBudget budget{ std::uint64_t{ 64 } << 20 };
        const EdenArchive::Error archiveError = mImpl->mArchive->readEntry(*index, bytes, &budget);
        if (archiveError)
            return fromArchive(archiveError, "cell");
        CellMap parsed;
        if (Error error = parseCell(bytes, parsed))
            return error;
        if (parsed.mCellX != cellX || parsed.mCellY != cellY)
            return fail("cell", 12, "the payload is for another cell than its entry");
        const std::uint32_t expectedSide = mImpl->mManifest.mSettings.texelsPerCell();
        if (parsed.mSide != expectedSide
            || parsed.mSpacing != static_cast<float>(mImpl->mManifest.mSettings.mSpacingUnits))
            return fail("side", 20, "the cell does not match the manifest's spacing");
        cell = std::move(parsed);
        return {};
    }
}
