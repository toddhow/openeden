#include "tilepack.hpp"

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

        // ---- canonical JSON writing ----

        void appendString(std::string& out, std::string_view text)
        {
            static constexpr char hex[] = "0123456789abcdef";
            out += '"';
            for (const char c : text)
            {
                const auto byte = static_cast<unsigned char>(c);
                if (c == '"')
                    out += "\\\"";
                else if (c == '\\')
                    out += "\\\\";
                else if (byte < 0x20)
                {
                    out += "\\u00";
                    out += hex[byte >> 4];
                    out += hex[byte & 15];
                }
                else
                    out += c;
            }
            out += '"';
        }

        // ---- a tiny JSON reader (objects, arrays, strings, unsigned integers) ----

        struct JsonValue
        {
            enum class Kind
            {
                Object,
                Array,
                String,
                Number,
            };
            Kind mKind = Kind::Number;
            std::size_t mOffset = 0;
            std::string mString;
            std::uint64_t mNumber = 0;
            std::vector<std::pair<std::string, JsonValue>> mMembers;
            std::vector<JsonValue> mItems;
        };

        class JsonReader
        {
        public:
            explicit JsonReader(std::string_view text)
                : mText(text)
            {
            }

            LodError parse(JsonValue& value)
            {
                skipSpace();
                if (LodError error = parseValue(value, 0))
                    return error;
                skipSpace();
                if (mPos != mText.size())
                    return fail("manifest", mPos, "text after the end of the value");
                return {};
            }

        private:
            static constexpr int maxDepth = 8;
            // The manifest of a world is a few dozen values; this bounds the memory a hostile one can take.
            static constexpr std::size_t maxNodes = 4096;

            void skipSpace()
            {
                while (mPos < mText.size()
                    && (mText[mPos] == ' ' || mText[mPos] == '\n' || mText[mPos] == '\t' || mText[mPos] == '\r'))
                    ++mPos;
            }

            LodError parseValue(JsonValue& value, int depth)
            {
                if (depth > maxDepth)
                    return fail("manifest", mPos, "nested too deeply");
                if (++mNodes > maxNodes)
                    return fail("manifest", mPos, "the manifest has too many values");
                if (mPos >= mText.size())
                    return fail("manifest", mPos, "the text ends inside a value");
                value.mOffset = mPos;
                const char c = mText[mPos];
                if (c == '{')
                    return parseObject(value, depth);
                if (c == '[')
                    return parseArray(value, depth);
                if (c == '"')
                {
                    value.mKind = JsonValue::Kind::String;
                    return parseString(value.mString);
                }
                if (c >= '0' && c <= '9')
                    return parseNumber(value);
                return fail("manifest", mPos, "unexpected character");
            }

            LodError parseNumber(JsonValue& value)
            {
                value.mKind = JsonValue::Kind::Number;
                const std::size_t start = mPos;
                std::uint64_t result = 0;
                while (mPos < mText.size() && mText[mPos] >= '0' && mText[mPos] <= '9')
                {
                    const std::uint64_t digit = static_cast<std::uint64_t>(mText[mPos] - '0');
                    if (result > (~std::uint64_t{ 0 } - digit) / 10)
                        return fail("manifest", start, "the number is too large");
                    result = result * 10 + digit;
                    ++mPos;
                }
                value.mNumber = result;
                return {};
            }

            LodError parseString(std::string& out)
            {
                ++mPos; // the opening quote
                out.clear();
                while (true)
                {
                    if (mPos >= mText.size())
                        return fail("manifest", mPos, "the text ends inside a string");
                    const char c = mText[mPos++];
                    if (c == '"')
                        return {};
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    if (mPos >= mText.size())
                        return fail("manifest", mPos, "the text ends inside a string");
                    const char e = mText[mPos++];
                    switch (e)
                    {
                        case '"':
                        case '\\':
                        case '/':
                            out += e;
                            break;
                        case 'b':
                            out += '\b';
                            break;
                        case 'f':
                            out += '\f';
                            break;
                        case 'n':
                            out += '\n';
                            break;
                        case 'r':
                            out += '\r';
                            break;
                        case 't':
                            out += '\t';
                            break;
                        case 'u':
                        {
                            unsigned int code = 0;
                            for (int i = 0; i < 4; ++i)
                            {
                                if (mPos >= mText.size())
                                    return fail("manifest", mPos, "the text ends inside a string");
                                const char h = mText[mPos++];
                                unsigned int digit = 0;
                                if (h >= '0' && h <= '9')
                                    digit = static_cast<unsigned int>(h - '0');
                                else if (h >= 'a' && h <= 'f')
                                    digit = static_cast<unsigned int>(h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F')
                                    digit = static_cast<unsigned int>(h - 'A' + 10);
                                else
                                    return fail("manifest", mPos - 1, "bad hexadecimal digit");
                                code = code * 16 + digit;
                            }
                            // Only the escapes the writer produces are needed: control characters. Others are read
                            // as UTF-8 and then refused as non-canonical by the comparison.
                            if (code >= 0xD800 && code <= 0xDFFF)
                                return fail("manifest", mPos - 6, "surrogate escapes are not supported");
                            if (code < 0x80)
                                out += static_cast<char>(code);
                            else if (code < 0x800)
                            {
                                out += static_cast<char>(0xC0 | (code >> 6));
                                out += static_cast<char>(0x80 | (code & 0x3F));
                            }
                            else
                            {
                                out += static_cast<char>(0xE0 | (code >> 12));
                                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                                out += static_cast<char>(0x80 | (code & 0x3F));
                            }
                            break;
                        }
                        default:
                            return fail("manifest", mPos - 1, "unknown escape");
                    }
                }
            }

            LodError parseArray(JsonValue& value, int depth)
            {
                value.mKind = JsonValue::Kind::Array;
                ++mPos;
                skipSpace();
                if (mPos < mText.size() && mText[mPos] == ']')
                {
                    ++mPos;
                    return {};
                }
                while (true)
                {
                    skipSpace();
                    JsonValue item;
                    if (LodError error = parseValue(item, depth + 1))
                        return error;
                    value.mItems.push_back(std::move(item));
                    skipSpace();
                    if (mPos >= mText.size())
                        return fail("manifest", mPos, "the text ends inside an array");
                    if (mText[mPos] == ',')
                    {
                        ++mPos;
                        continue;
                    }
                    if (mText[mPos] == ']')
                    {
                        ++mPos;
                        return {};
                    }
                    return fail("manifest", mPos, "expected , or ]");
                }
            }

            LodError parseObject(JsonValue& value, int depth)
            {
                value.mKind = JsonValue::Kind::Object;
                ++mPos;
                skipSpace();
                if (mPos < mText.size() && mText[mPos] == '}')
                {
                    ++mPos;
                    return {};
                }
                while (true)
                {
                    skipSpace();
                    if (mPos >= mText.size() || mText[mPos] != '"')
                        return fail("manifest", mPos, "expected a key");
                    std::string key;
                    if (LodError error = parseString(key))
                        return error;
                    skipSpace();
                    if (mPos >= mText.size() || mText[mPos] != ':')
                        return fail("manifest", mPos, "expected :");
                    ++mPos;
                    skipSpace();
                    JsonValue member;
                    if (LodError error = parseValue(member, depth + 1))
                        return error;
                    value.mMembers.emplace_back(std::move(key), std::move(member));
                    skipSpace();
                    if (mPos >= mText.size())
                        return fail("manifest", mPos, "the text ends inside an object");
                    if (mText[mPos] == ',')
                    {
                        ++mPos;
                        continue;
                    }
                    if (mText[mPos] == '}')
                    {
                        ++mPos;
                        return {};
                    }
                    return fail("manifest", mPos, "expected , or }");
                }
            }

            std::string_view mText;
            std::size_t mPos = 0;
            std::size_t mNodes = 0;
        };

        const JsonValue* findMember(const JsonValue& object, std::string_view key)
        {
            for (const auto& [name, value] : object.mMembers)
            {
                if (name == key)
                    return &value;
            }
            return nullptr;
        }

        LodError wantNumber(const JsonValue& object, std::string_view key, std::uint32_t& out)
        {
            const JsonValue* value = findMember(object, key);
            if (value == nullptr)
                return fail("manifest", object.mOffset, "a required key is missing");
            if (value->mKind != JsonValue::Kind::Number)
                return fail("manifest", value->mOffset, "the value must be a number");
            if (value->mNumber > 0xFFFFFFFFull)
                return fail("manifest", value->mOffset, "the number does not fit 32 bits");
            out = static_cast<std::uint32_t>(value->mNumber);
            return {};
        }

        LodError wantArray(const JsonValue& object, std::string_view key, const JsonValue*& out)
        {
            const JsonValue* value = findMember(object, key);
            if (value == nullptr)
                return fail("manifest", object.mOffset, "a required key is missing");
            if (value->mKind != JsonValue::Kind::Array)
                return fail("manifest", value->mOffset, "the value must be an array");
            out = value;
            return {};
        }

        std::size_t firstDifference(std::string_view a, std::string_view b)
        {
            const std::size_t common = std::min(a.size(), b.size());
            for (std::size_t i = 0; i < common; ++i)
            {
                if (a[i] != b[i])
                    return i;
            }
            return common;
        }
    }

    namespace
    {
        // Range checks of the values a consumer would trust (the round trip alone does not make them sensible).
        LodError validateManifest(const TileManifest& manifest)
        {
            if (manifest.mGridSize != 0 && (manifest.mGridSize < minGridSize || manifest.mGridSize > maxGridSize))
                return fail("manifest", 0, "gridSize must be 0 or between 3 and 129");
            if (manifest.mAlbedoSize != 0
                && (manifest.mAlbedoSize > maxAlbedoSide || (manifest.mAlbedoSize & (manifest.mAlbedoSize - 1)) != 0))
                return fail("manifest", 0, "albedoSize must be 0 or a power of two up to 2048");
            for (const std::uint32_t level : manifest.mLevels)
            {
                if (level > maxLevel)
                    return fail("manifest", 0, "a level is above 15");
            }
            for (const WorldspaceInfo& space : manifest.mWorldspaces)
            {
                if (space.mIndex > 255 || space.mCellSize == 0)
                    return fail("manifest", 0, "a worldspace index must be below 256 and its cell size above 0");
            }
            return {};
        }
    }

    std::string writeManifestJson(const TileManifest& manifest)
    {
        // Keys in alphabetical order at every level, no spaces: the one canonical form.
        std::string out = "{\"albedoSize\":";
        out += std::to_string(manifest.mAlbedoSize);
        out += ",\"bakerVersion\":" + std::to_string(manifest.mBakerVersion);
        out += ",\"gridSize\":" + std::to_string(manifest.mGridSize);
        out += ",\"layoutVersion\":" + std::to_string(manifest.mLayoutVersion);
        out += ",\"levels\":[";
        for (std::size_t i = 0; i < manifest.mLevels.size(); ++i)
        {
            if (i != 0)
                out += ',';
            out += std::to_string(manifest.mLevels[i]);
        }
        out += "],\"worldspaces\":[";
        for (std::size_t i = 0; i < manifest.mWorldspaces.size(); ++i)
        {
            const WorldspaceInfo& space = manifest.mWorldspaces[i];
            if (i != 0)
                out += ',';
            out += "{\"cellSize\":" + std::to_string(space.mCellSize);
            out += ",\"index\":" + std::to_string(space.mIndex);
            out += ",\"name\":";
            appendString(out, space.mName);
            out += '}';
        }
        out += "]}";
        return out;
    }

    LodError parseManifestJson(std::string_view text, TileManifest& manifest)
    {
        manifest = {};
        JsonValue root;
        if (LodError error = JsonReader(text).parse(root))
            return error;
        if (root.mKind != JsonValue::Kind::Object)
            return fail("manifest", root.mOffset, "the manifest must be an object");
        static constexpr std::string_view known[]
            = { "albedoSize", "bakerVersion", "gridSize", "layoutVersion", "levels", "worldspaces" };
        for (const auto& [name, member] : root.mMembers)
        {
            if (std::find(std::begin(known), std::end(known), name) == std::end(known))
                return fail("manifest", member.mOffset, "unknown key");
        }
        TileManifest parsed;
        if (LodError error = wantNumber(root, "albedoSize", parsed.mAlbedoSize))
            return error;
        if (LodError error = wantNumber(root, "bakerVersion", parsed.mBakerVersion))
            return error;
        if (LodError error = wantNumber(root, "gridSize", parsed.mGridSize))
            return error;
        if (LodError error = wantNumber(root, "layoutVersion", parsed.mLayoutVersion))
            return error;
        const JsonValue* levels = nullptr;
        if (LodError error = wantArray(root, "levels", levels))
            return error;
        for (const JsonValue& item : levels->mItems)
        {
            if (item.mKind != JsonValue::Kind::Number || item.mNumber > 0xFFFFFFFFull)
                return fail("manifest", item.mOffset, "a level must be a number that fits 32 bits");
            parsed.mLevels.push_back(static_cast<std::uint32_t>(item.mNumber));
        }
        const JsonValue* spaces = nullptr;
        if (LodError error = wantArray(root, "worldspaces", spaces))
            return error;
        for (const JsonValue& item : spaces->mItems)
        {
            if (item.mKind != JsonValue::Kind::Object)
                return fail("manifest", item.mOffset, "a worldspace must be an object");
            WorldspaceInfo space;
            if (LodError error = wantNumber(item, "cellSize", space.mCellSize))
                return error;
            if (LodError error = wantNumber(item, "index", space.mIndex))
                return error;
            const JsonValue* name = findMember(item, "name");
            if (name == nullptr)
                return fail("manifest", item.mOffset, "a required key is missing");
            if (name->mKind != JsonValue::Kind::String)
                return fail("manifest", name->mOffset, "the name must be a string");
            space.mName = name->mString;
            if (item.mMembers.size() != 3)
                return fail("manifest", item.mOffset, "a worldspace has exactly cellSize, index and name");
            parsed.mWorldspaces.push_back(std::move(space));
        }
        if (LodError error = validateManifest(parsed))
            return error;
        const std::string canonical = writeManifestJson(parsed);
        if (canonical != text)
            return fail("manifest", firstDifference(canonical, text), "the text is not in canonical form");
        manifest = std::move(parsed);
        return {};
    }

    namespace
    {
        LodError buildEntries(const TileManifest& manifest, const std::vector<BakedTile>& tiles,
            const TilePackOptions& options, std::vector<EdenArchive::InputEntry>& entries)
        {
            std::vector<std::uint64_t> keys;
            keys.reserve(tiles.size());
            for (const BakedTile& tile : tiles)
            {
                const std::optional<std::uint64_t> key = packTileKey(tile.mId);
                if (!key)
                    return fail("tileId", 0, "the tile id does not pack into a key");
                if (tile.mData.mLevel != tile.mId.mLevel)
                    return fail("level", 0, "the tile's level differs from its id's level");
                keys.push_back(*key);

                EdenArchive::InputEntry entry;
                entry.mKey = *key;
                entry.mType = entryTypeTerrainTile;
                if (LodError error = writeTerrainTile(tile.mData, entry.mData))
                    return error;
                entry.mHintFlags = EdenArchive::entryFlagHintMemSize;
                entry.mHintMemorySize = static_cast<std::uint32_t>(entry.mData.size());
                entry.mLayoutParamA = tile.mData.mGridSize;
                entry.mLayoutParamB = tile.mData.mAlbedoWidth;
                entry.mCompression = EdenArchive::Compression::Auto;
                entry.mProvenance
                    = EdenArchive::ProvenanceInput{ std::string(bakerToolId), tile.mSourceDigest, tile.mParameters };
                entries.push_back(std::move(entry));
            }
            std::sort(keys.begin(), keys.end());
            if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
                return fail("tileId", 0, "two tiles have the same id");

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

    LodError buildTilePack(const TileManifest& manifest, const std::vector<BakedTile>& tiles,
        const TilePackOptions& options, std::vector<std::uint8_t>& out)
    {
        out.clear();
        std::vector<EdenArchive::InputEntry> entries;
        if (LodError error = buildEntries(manifest, tiles, options, entries))
            return error;
        const EdenArchive::Error error = EdenArchive::writePackToMemory(entries, out, writeOptionsFor(options));
        if (error)
        {
            out.clear();
            return fromArchive(error, "pack");
        }
        return {};
    }

    LodError writeTilePack(const TileManifest& manifest, const std::vector<BakedTile>& tiles,
        const TilePackOptions& options, const std::filesystem::path& path)
    {
        std::vector<EdenArchive::InputEntry> entries;
        if (LodError error = buildEntries(manifest, tiles, options, entries))
            return error;
        const EdenArchive::Error error = EdenArchive::writePack(entries, path, writeOptionsFor(options));
        if (error)
            return fromArchive(error, "pack");
        return {};
    }

    struct TilePack::Impl
    {
        std::unique_ptr<EdenArchive::Archive> mArchive;
        TileManifest mManifest;
        std::vector<TileId> mIds; // in key order
    };

    TilePack::TilePack(std::unique_ptr<Impl> impl)
        : mImpl(std::move(impl))
    {
    }

    TilePack::~TilePack() = default;

    std::unique_ptr<TilePack> TilePack::open(const std::filesystem::path& path, LodError& error)
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

    std::unique_ptr<TilePack> TilePack::open(std::shared_ptr<const EdenArchive::Source> source, LodError& error)
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
            error = fail("manifest", 0, "the pack has no lod/manifest.json entry");
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
        if (impl->mManifest.mLayoutVersion != tileKeyLayoutVersion)
        {
            error = fail("layoutVersion", 0,
                "the pack was made with another tile layout version (a draft version 0 pack must be baked again)");
            return nullptr;
        }
        std::vector<std::uint64_t> keys;
        for (std::size_t i = 0; i < archive.entryCount(); ++i)
        {
            const EdenArchive::EntryInfo entry = archive.entry(i);
            if (!entry.mHasKey || (entry.mKey >> 56) != tagTerrainTile)
                continue;
            keys.push_back(entry.mKey);
        }
        std::sort(keys.begin(), keys.end());
        for (const std::uint64_t key : keys)
        {
            const std::optional<TileId> id = unpackTileKey(key);
            if (!id)
            {
                error = fail("tileKey", 0, "a tile entry has a key that is not a valid tile key");
                return nullptr;
            }
            impl->mIds.push_back(*id);
        }
        return std::unique_ptr<TilePack>(new TilePack(std::move(impl)));
    }

    const TileManifest& TilePack::manifest() const noexcept
    {
        return mImpl->mManifest;
    }

    std::uint64_t TilePack::generation() const noexcept
    {
        return mImpl->mArchive->info().mGeneration;
    }

    const EdenArchive::Hash& TilePack::sourceDigest() const noexcept
    {
        return mImpl->mArchive->info().mSourceDigest;
    }

    const std::vector<TileId>& TilePack::tileIds() const noexcept
    {
        return mImpl->mIds;
    }

    const EdenArchive::Archive& TilePack::archive() const noexcept
    {
        return *mImpl->mArchive;
    }

    bool TilePack::contains(const TileId& id) const noexcept
    {
        const std::optional<std::uint64_t> key = packTileKey(id);
        return key && mImpl->mArchive->findKey(*key).has_value();
    }

    namespace
    {
        LodError findTileEntry(
            const EdenArchive::Archive& archive, const TileId& id, std::size_t& index, std::uint64_t& size)
        {
            const std::optional<std::uint64_t> key = packTileKey(id);
            if (!key)
                return fail("tileId", 0, "the tile id does not pack into a key");
            const std::optional<std::size_t> found = archive.findKey(*key);
            if (!found)
                return fail("tileId", 0, "the pack has no such tile");
            index = *found;
            size = archive.entry(index).mSize;
            return {};
        }
    }

    LodError TilePack::readHeader(const TileId& id, TerrainTile& tile, TerrainTileLayout& layout) const
    {
        std::size_t index = 0;
        std::uint64_t size = 0;
        if (LodError error = findTileEntry(*mImpl->mArchive, id, index, size))
            return error;
        if (size < terrainTileHeaderSize)
            return fail("header", 0, "the tile entry is shorter than its header");
        std::uint8_t header[terrainTileHeaderSize];
        const EdenArchive::Error archiveError = mImpl->mArchive->readRange(index, 0, terrainTileHeaderSize, header);
        if (archiveError)
            return fromArchive(archiveError, "tile");
        if (LodError error
            = parseTerrainTileHeader(std::span<const std::uint8_t>(header, terrainTileHeaderSize), size, tile, layout))
            return error;
        if (tile.mLevel != id.mLevel)
            return fail("level", 12, "the tile's level differs from the level in its key");
        return {};
    }

    LodError TilePack::readTile(const TileId& id, TerrainTile& tile, EdenArchive::DecodeBudget* budget) const
    {
        std::size_t index = 0;
        std::uint64_t size = 0;
        if (LodError error = findTileEntry(*mImpl->mArchive, id, index, size))
            return error;
        std::vector<std::uint8_t> bytes;
        const EdenArchive::Error archiveError = mImpl->mArchive->readEntry(index, bytes, budget);
        if (archiveError)
            return fromArchive(archiveError, "tile");
        if (LodError error = parseTerrainTile(bytes, tile))
            return error;
        if (tile.mLevel != id.mLevel)
            return fail("level", 12, "the tile's level differs from the level in its key");
        return {};
    }

    LodError TilePack::readAlbedo(
        const TileId& id, std::vector<std::uint8_t>& out, EdenArchive::DecodeBudget* budget) const
    {
        out.clear();
        TerrainTile tile;
        TerrainTileLayout layout;
        if (LodError error = readHeader(id, tile, layout))
            return error;
        if (!tile.mHasAlbedo)
            return fail("albedo", 0, "the tile has no albedo");
        std::size_t index = 0;
        std::uint64_t size = 0;
        if (LodError error = findTileEntry(*mImpl->mArchive, id, index, size))
            return error;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(layout.mAlbedoSize));
        const EdenArchive::Error archiveError
            = mImpl->mArchive->readRange(index, layout.mAlbedoOffset, bytes.size(), bytes.data(), budget);
        if (archiveError)
            return fromArchive(archiveError, "tile");
        out = std::move(bytes);
        return {};
    }
}

namespace Lod
{
    std::optional<EdenArchive::Hash> TilePack::tileSourceDigest(const TileId& id) const
    {
        std::size_t index = 0;
        std::uint64_t size = 0;
        if (findTileEntry(*mImpl->mArchive, id, index, size))
        {
            return std::nullopt;
        }
        const std::optional<EdenArchive::ProvenanceInfo> provenance = mImpl->mArchive->provenance(index);
        if (!provenance)
        {
            return std::nullopt;
        }
        return provenance->mSourceDigest;
    }
}
