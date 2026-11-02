#include "clusterpack.hpp"

#include <algorithm>
#include <utility>

#include <edenarchive/error.hpp>
#include <edenarchive/source.hpp>
#include <edenarchive/writer.hpp>

namespace Lod
{
    namespace
    {
        LodError fail(const char* field, std::uint64_t offset, std::string message)
        {
            LodError error;
            error.mField = field;
            error.mOffset = offset;
            error.mMessage = std::move(message);
            return error;
        }

        LodError fromArchive(const EdenArchive::Error& error, const char* what)
        {
            return fail(what, error.mOffset, error.describe());
        }

        constexpr std::size_t maxManifestBytes = 4096;

        void appendNumbers(std::string& out, const std::vector<std::uint32_t>& values)
        {
            out += '[';
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                    out += ',';
                out += std::to_string(values[i]);
            }
            out += ']';
        }

        // The writer escapes nothing, so a name may hold any byte except a control character, a quote or a backslash
        // (UTF-8 text passes through unchanged).
        bool plainCharacter(char c)
        {
            return static_cast<unsigned char>(c) >= 0x20 && c != 0x7F && c != '"' && c != '\\';
        }

        // A cursor over the manifest text that accepts only the canonical shape.
        class Cursor
        {
        public:
            explicit Cursor(std::string_view text)
                : mText(text)
            {
            }

            bool literal(std::string_view expected)
            {
                if (mText.substr(mPos, expected.size()) != expected)
                    return false;
                mPos += expected.size();
                return true;
            }

            bool number(std::uint32_t& value)
            {
                const std::size_t start = mPos;
                std::uint64_t result = 0;
                while (mPos < mText.size() && mText[mPos] >= '0' && mText[mPos] <= '9')
                {
                    result = result * 10 + static_cast<std::uint64_t>(mText[mPos] - '0');
                    if (result > 0xFFFFFFFFu)
                        return false;
                    ++mPos;
                }
                // No sign, no leading zero (except 0 itself), at least one digit.
                if (mPos == start || (mText[start] == '0' && mPos - start > 1))
                    return false;
                value = static_cast<std::uint32_t>(result);
                return true;
            }

            bool numbers(std::vector<std::uint32_t>& values)
            {
                if (!literal("["))
                    return false;
                if (literal("]"))
                    return true;
                while (true)
                {
                    std::uint32_t value = 0;
                    if (!number(value) || values.size() >= 16)
                        return false;
                    values.push_back(value);
                    if (literal("]"))
                        return true;
                    if (!literal(","))
                        return false;
                }
            }

            // A string of plain characters (see plainCharacter).
            bool plainString(std::string& value)
            {
                if (!literal("\""))
                    return false;
                const std::size_t start = mPos;
                while (mPos < mText.size() && mText[mPos] != '"')
                {
                    if (!plainCharacter(mText[mPos]))
                        return false;
                    ++mPos;
                }
                value = std::string(mText.substr(start, mPos - start));
                return literal("\"");
            }

            std::size_t position() const { return mPos; }
            bool atEnd() const { return mPos == mText.size(); }

        private:
            std::string_view mText;
            std::size_t mPos = 0;
        };

        bool plainName(std::string_view name)
        {
            return name.size() <= 64 && std::all_of(name.begin(), name.end(), plainCharacter);
        }

        LodError validateManifest(const ClusterManifest& manifest)
        {
            if (manifest.mCellSize == 0)
                return fail("cellSize", 0, "the cell size must be above 0");
            if (!plainName(manifest.mWorldspace))
                return fail("worldspace", 0, "the worldspace name must be at most 64 plain characters");
            if (manifest.mLevels.empty() || manifest.mLevels.size() != manifest.mErrors.size())
                return fail("levels", 0, "levels and errors must be lists of the same, non-zero length");
            for (const std::uint32_t level : manifest.mLevels)
                if (level < minClusterLevel || level > maxClusterLevel)
                    return fail("levels", 0, "a level is outside 1 to 3");
            return {};
        }
    }

    std::string writeClusterManifestJson(const ClusterManifest& manifest)
    {
        std::string out = "{\"bakerVersion\":" + std::to_string(manifest.mBakerVersion)
            + ",\"cellSize\":" + std::to_string(manifest.mCellSize) + ",\"errors\":";
        appendNumbers(out, manifest.mErrors);
        out += ",\"layoutVersion\":" + std::to_string(manifest.mLayoutVersion) + ",\"levels\":";
        appendNumbers(out, manifest.mLevels);
        out += ",\"worldspace\":\"" + manifest.mWorldspace + "\"}";
        return out;
    }

    LodError parseClusterManifestJson(std::string_view text, ClusterManifest& manifest)
    {
        if (text.size() > maxManifestBytes)
            return fail("manifest", 0, "the manifest is larger than 4096 bytes");
        ClusterManifest parsed;
        Cursor cursor(text);
        const bool shape = cursor.literal("{\"bakerVersion\":") && cursor.number(parsed.mBakerVersion)
            && cursor.literal(",\"cellSize\":") && cursor.number(parsed.mCellSize) && cursor.literal(",\"errors\":")
            && cursor.numbers(parsed.mErrors) && cursor.literal(",\"layoutVersion\":")
            && cursor.number(parsed.mLayoutVersion) && cursor.literal(",\"levels\":") && cursor.numbers(parsed.mLevels)
            && cursor.literal(",\"worldspace\":") && cursor.plainString(parsed.mWorldspace) && cursor.literal("}")
            && cursor.atEnd();
        if (!shape)
            return fail("manifest", cursor.position(), "the manifest is not the canonical text");
        if (LodError error = validateManifest(parsed))
            return error;
        manifest = std::move(parsed);
        return {};
    }

    EdenArchive::Hash clusterContentDigest(const ClusterManifest& manifest, const std::vector<BakedCluster>& clusters)
    {
        std::vector<std::pair<std::uint64_t, const BakedCluster*>> ordered;
        ordered.reserve(clusters.size());
        for (const BakedCluster& cluster : clusters)
            ordered.emplace_back(packClusterKey(cluster.mId).value_or(0), &cluster);
        std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        EdenArchive::Hasher hasher;
        const std::string json = writeClusterManifestJson(manifest);
        hasher.update(json.data(), json.size());
        for (const auto& entry : ordered)
            hasher.update(entry.second->mSourceDigest.data(), entry.second->mSourceDigest.size());
        return hasher.finalize();
    }

    namespace
    {
        LodError buildEntries(const ClusterManifest& manifest, const std::vector<BakedCluster>& clusters,
            const TilePackOptions& options, std::vector<EdenArchive::InputEntry>& entries)
        {
            if (LodError error = validateManifest(manifest))
                return error;
            std::vector<std::uint64_t> keys;
            keys.reserve(clusters.size());
            for (const BakedCluster& cluster : clusters)
            {
                const std::optional<std::uint64_t> key = packClusterKey(cluster.mId);
                if (!key)
                    return fail("tileId", 0, "the tile id does not pack into a key");
                if (cluster.mData.mLevel != cluster.mId.mLevel)
                    return fail("level", 0, "the cluster's level differs from its id's level");
                keys.push_back(*key);

                EdenArchive::InputEntry entry;
                entry.mKey = *key;
                entry.mType = entryTypeObjectCluster;
                if (LodError error = writeObjectCluster(cluster.mData, entry.mData))
                    return error;
                entry.mHintFlags = EdenArchive::entryFlagHintMemSize;
                entry.mHintMemorySize = static_cast<std::uint32_t>(entry.mData.size());
                entry.mLayoutParamA = static_cast<std::uint32_t>(cluster.mData.mGroups.size());
                entry.mLayoutParamB = static_cast<std::uint32_t>(cluster.mData.mCells.size());
                entry.mCompression = EdenArchive::Compression::Auto;
                entry.mProvenance = EdenArchive::ProvenanceInput{
                    std::string(clusterToolId), cluster.mSourceDigest, cluster.mParameters
                };
                entries.push_back(std::move(entry));
            }
            std::sort(keys.begin(), keys.end());
            if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
                return fail("tileId", 0, "two clusters have the same id");

            EdenArchive::InputEntry manifestEntry;
            manifestEntry.mPath = std::string(clusterManifestPath);
            manifestEntry.mType = 0;
            const std::string json = writeClusterManifestJson(manifest);
            manifestEntry.mData.assign(json.begin(), json.end());
            manifestEntry.mProvenance
                = EdenArchive::ProvenanceInput{ std::string(clusterToolId), options.mSourceDigest, {} };
            entries.push_back(std::move(manifestEntry));
            return {};
        }

        EdenArchive::WriteOptions writeOptionsFor(const TilePackOptions& options)
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

    LodError buildClusterPack(const ClusterManifest& manifest, const std::vector<BakedCluster>& clusters,
        const TilePackOptions& options, std::vector<std::uint8_t>& out)
    {
        std::vector<EdenArchive::InputEntry> entries;
        if (LodError error = buildEntries(manifest, clusters, options, entries))
            return error;
        out.clear();
        const EdenArchive::Error error = EdenArchive::writePackToMemory(entries, out, writeOptionsFor(options));
        if (error)
        {
            out.clear();
            return fromArchive(error, "pack");
        }
        return {};
    }

    LodError writeClusterPack(const ClusterManifest& manifest, const std::vector<BakedCluster>& clusters,
        const TilePackOptions& options, const std::filesystem::path& path)
    {
        std::vector<EdenArchive::InputEntry> entries;
        if (LodError error = buildEntries(manifest, clusters, options, entries))
            return error;
        const EdenArchive::Error error = EdenArchive::writePack(entries, path, writeOptionsFor(options));
        if (error)
            return fromArchive(error, "pack");
        return {};
    }

    struct ClusterPack::Impl
    {
        std::unique_ptr<EdenArchive::Archive> mArchive;
        ClusterManifest mManifest;
        std::vector<TileId> mIds; // key order
    };

    ClusterPack::ClusterPack(std::unique_ptr<Impl> impl)
        : mImpl(std::move(impl))
    {
    }

    ClusterPack::~ClusterPack() = default;

    std::unique_ptr<ClusterPack> ClusterPack::open(const std::filesystem::path& path, LodError& error)
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

    std::unique_ptr<ClusterPack> ClusterPack::open(std::shared_ptr<const EdenArchive::Source> source, LodError& error)
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
        const std::optional<std::size_t> manifestIndex = archive.findPath(clusterManifestPath);
        if (!manifestIndex)
        {
            error = fail("manifest", 0, "the pack has no lod/objects.json entry");
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
        error = parseClusterManifestJson(
            std::string_view(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size()),
            impl->mManifest);
        if (error)
            return nullptr;
        if (impl->mManifest.mLayoutVersion != clusterLayoutVersion)
        {
            error = fail("layoutVersion", 0, "the pack was made with another cluster layout version");
            return nullptr;
        }
        std::vector<std::uint64_t> keys;
        for (std::size_t i = 0; i < archive.entryCount(); ++i)
        {
            const EdenArchive::EntryInfo entry = archive.entry(i);
            if (entry.mHasKey && (entry.mKey >> 56) == tagObjectCluster)
                keys.push_back(entry.mKey);
        }
        std::sort(keys.begin(), keys.end());
        for (const std::uint64_t key : keys)
        {
            const std::optional<TileId> id = unpackClusterKey(key);
            if (!id)
            {
                error = fail("tileKey", 0, "a cluster entry has a key that is not a valid tile key");
                return nullptr;
            }
            impl->mIds.push_back(*id);
        }
        return std::unique_ptr<ClusterPack>(new ClusterPack(std::move(impl)));
    }

    const ClusterManifest& ClusterPack::manifest() const noexcept
    {
        return mImpl->mManifest;
    }

    std::uint64_t ClusterPack::generation() const noexcept
    {
        return mImpl->mArchive->info().mGeneration;
    }

    const EdenArchive::Hash& ClusterPack::sourceDigest() const noexcept
    {
        return mImpl->mArchive->info().mSourceDigest;
    }

    const std::vector<TileId>& ClusterPack::tileIds() const noexcept
    {
        return mImpl->mIds;
    }

    const EdenArchive::Archive& ClusterPack::archive() const noexcept
    {
        return *mImpl->mArchive;
    }

    bool ClusterPack::contains(const TileId& id) const noexcept
    {
        const std::optional<std::uint64_t> key = packClusterKey(id);
        return key && mImpl->mArchive->findKey(*key).has_value();
    }

    LodError ClusterPack::readCluster(const TileId& id, ObjectCluster& cluster, EdenArchive::DecodeBudget* budget) const
    {
        const std::optional<std::uint64_t> key = packClusterKey(id);
        if (!key)
            return fail("tileId", 0, "the tile id does not pack into a key");
        const std::optional<std::size_t> index = mImpl->mArchive->findKey(*key);
        if (!index)
            return fail("tileId", 0, "the pack has no such cluster");
        std::vector<std::uint8_t> bytes;
        const EdenArchive::Error archiveError = mImpl->mArchive->readEntry(*index, bytes, budget);
        if (archiveError)
            return fromArchive(archiveError, "cluster");
        return readObjectCluster(bytes, cluster, &id);
    }
}
