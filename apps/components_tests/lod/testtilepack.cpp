// Task 0055 (stage S1b): the manifest and the derived pack of baked terrain tiles. Pure code over generated tiles and
// in-memory packs; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/tilepack.hpp>

#include <edenarchive/source.hpp>
#include <edenarchive/writer.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace Lod;

    // ---- helpers ------------------------------------------------------------------------------------------------

    class Rng
    {
    public:
        explicit Rng(std::uint64_t seed)
            : mState(seed)
        {
        }

        std::uint64_t next()
        {
            mState ^= mState << 13;
            mState ^= mState >> 7;
            mState ^= mState << 17;
            return mState;
        }

    private:
        std::uint64_t mState;
    };

    EdenArchive::Hash filledHash(std::uint8_t value)
    {
        EdenArchive::Hash hash{};
        hash.fill(value);
        return hash;
    }

    TerrainTile makeTile(
        std::uint32_t grid, bool normals, std::uint32_t albedoSide, std::uint32_t mips, std::uint32_t seed)
    {
        TerrainTile tile;
        tile.mGridSize = grid;
        tile.mLevel = 1;
        tile.mSide = 16384.f;
        tile.mOriginX = static_cast<float>(seed) * 16384.f;
        tile.mOriginY = -static_cast<float>(seed) * 16384.f;
        tile.mMinHeight = -12.5f;
        tile.mMaxHeight = 300.25f + static_cast<float>(seed);
        tile.mGeometricError = 3.5f;
        Rng rng(0xFEEDF00Dull * (seed + 1));
        tile.mHeights.resize(static_cast<std::size_t>(grid) * grid);
        for (std::uint16_t& height : tile.mHeights)
        {
            height = static_cast<std::uint16_t>(rng.next());
        }
        tile.mHasNormals = normals;
        if (normals)
        {
            tile.mNormals.resize(static_cast<std::size_t>(grid) * grid * 2);
            for (std::int8_t& value : tile.mNormals)
            {
                value = static_cast<std::int8_t>(rng.next());
            }
        }
        if (albedoSide != 0)
        {
            tile.mHasAlbedo = true;
            tile.mAlbedoWidth = albedoSide;
            tile.mAlbedoHeight = albedoSide;
            tile.mAlbedoFormat = AlbedoFormat::Rgba8;
            tile.mAlbedoMipCount = mips;
            tile.mAlbedo.resize(albedoByteSize(albedoSide, albedoSide, AlbedoFormat::Rgba8, mips));
            for (std::uint8_t& texel : tile.mAlbedo)
            {
                texel = static_cast<std::uint8_t>(rng.next());
            }
        }
        return tile;
    }

    void expectSameTile(const TerrainTile& expected, const TerrainTile& actual)
    {
        EXPECT_EQ(expected.mGridSize, actual.mGridSize);
        EXPECT_EQ(expected.mLevel, actual.mLevel);
        EXPECT_EQ(expected.mSide, actual.mSide);
        EXPECT_EQ(expected.mOriginX, actual.mOriginX);
        EXPECT_EQ(expected.mOriginY, actual.mOriginY);
        EXPECT_EQ(expected.mMinHeight, actual.mMinHeight);
        EXPECT_EQ(expected.mMaxHeight, actual.mMaxHeight);
        EXPECT_EQ(expected.mGeometricError, actual.mGeometricError);
        EXPECT_EQ(expected.mHasNormals, actual.mHasNormals);
        EXPECT_EQ(expected.mHasAlbedo, actual.mHasAlbedo);
        EXPECT_EQ(expected.mAlbedoWidth, actual.mAlbedoWidth);
        EXPECT_EQ(expected.mAlbedoHeight, actual.mAlbedoHeight);
        EXPECT_EQ(expected.mAlbedoFormat, actual.mAlbedoFormat);
        EXPECT_EQ(expected.mAlbedoMipCount, actual.mAlbedoMipCount);
        EXPECT_EQ(expected.mHeights, actual.mHeights);
        EXPECT_EQ(expected.mNormals, actual.mNormals);
        EXPECT_EQ(expected.mAlbedo, actual.mAlbedo);
    }

    TileManifest makeManifest(std::uint32_t grid, std::uint32_t albedoSide)
    {
        TileManifest manifest;
        manifest.mLayoutVersion = tileKeyLayoutVersion;
        manifest.mBakerVersion = 1;
        manifest.mWorldspaces = { WorldspaceInfo{ 0, "Morrowind", 8192 } };
        manifest.mLevels = { 1 };
        manifest.mGridSize = grid;
        manifest.mAlbedoSize = albedoSide;
        return manifest;
    }

    struct Fixture
    {
        TileManifest mManifest;
        std::vector<BakedTile> mTiles;
        TilePackOptions mOptions;
    };

    // Six level-1 tiles, including negative cells, N = 5, an 8 x 8 albedo with 4 mips.
    Fixture makeFixture()
    {
        Fixture fixture;
        fixture.mManifest = makeManifest(5, 8);
        const std::pair<std::int32_t, std::int32_t> cells[]
            = { { 0, 0 }, { 2, 0 }, { -2, 0 }, { 0, -2 }, { -2, -2 }, { 4, 2 } };
        std::uint32_t seed = 0;
        for (const auto& cell : cells)
        {
            BakedTile baked;
            const std::optional<TileId> id = tileContainingCell(0, 1, cell.first, cell.second);
            EXPECT_TRUE(id.has_value());
            baked.mId = *id;
            baked.mData = makeTile(5, true, 8, 4, seed);
            baked.mSourceDigest = filledHash(static_cast<std::uint8_t>(0x10 + seed));
            baked.mParameters = { static_cast<std::uint8_t>(seed), 1, 2, 3 };
            fixture.mTiles.push_back(std::move(baked));
            ++seed;
        }
        fixture.mOptions.mGeneration = 7;
        fixture.mOptions.mSourceDigest = filledHash(0xAB);
        fixture.mOptions.mThreads = 1;
        fixture.mOptions.mStoreOnly = true;
        return fixture;
    }

    std::vector<std::uint8_t> buildPack(const Fixture& fixture)
    {
        std::vector<std::uint8_t> bytes;
        const LodError error = buildTilePack(fixture.mManifest, fixture.mTiles, fixture.mOptions, bytes);
        EXPECT_TRUE(error.ok()) << error.describe();
        return bytes;
    }

    std::unique_ptr<TilePack> openBytes(const std::vector<std::uint8_t>& bytes, LodError& error)
    {
        return TilePack::open(EdenArchive::makeMemorySource(bytes), error);
    }

    std::vector<std::uint8_t> toBytes(const std::string& text)
    {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }

    // A pack written directly with the container writer, for the cases buildTilePack would never produce.
    std::vector<std::uint8_t> rawPack(bool derived, const std::string* manifestText,
        const std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>>& tileEntries)
    {
        std::vector<EdenArchive::InputEntry> entries;
        if (manifestText != nullptr)
        {
            EdenArchive::InputEntry manifest;
            manifest.mPath = std::string(manifestPath);
            manifest.mData = toBytes(*manifestText);
            manifest.mProvenance = EdenArchive::ProvenanceInput{ "openeden.lod.test", filledHash(1), {} };
            entries.push_back(std::move(manifest));
        }
        for (const auto& tileEntry : tileEntries)
        {
            EdenArchive::InputEntry entry;
            entry.mKey = tileEntry.first;
            entry.mType = entryTypeTerrainTile;
            entry.mData = tileEntry.second;
            entry.mProvenance = EdenArchive::ProvenanceInput{ "openeden.lod.test", filledHash(2), {} };
            entries.push_back(std::move(entry));
        }
        EdenArchive::WriteOptions options;
        options.mStoreOnly = true;
        options.mDerived = derived;
        options.mGeneration = 1;
        options.mSourceDigest = filledHash(3);
        std::vector<std::uint8_t> bytes;
        const EdenArchive::Error error = EdenArchive::writePackToMemory(entries, bytes, options);
        EXPECT_FALSE(error) << error.describe();
        return bytes;
    }

    const std::string canonicalManifest = R"({"albedoSize":256,"bakerVersion":1,"gridSize":33,"layoutVersion":1,)"
                                          R"("levels":[1],"worldspaces":[{"cellSize":8192,"index":0,)"
                                          R"("name":"Morrowind"}]})";

    TileManifest canonicalValue()
    {
        return makeManifest(33, 256);
    }

    std::string replaceOnce(std::string text, const std::string& from, const std::string& to)
    {
        const std::size_t position = text.find(from);
        EXPECT_NE(position, std::string::npos) << from;
        if (position != std::string::npos)
        {
            text.replace(position, from.size(), to);
        }
        return text;
    }

    // A source that counts the reads that reach it. Reads may come from several threads.
    class CountingSource final : public EdenArchive::Source
    {
    public:
        explicit CountingSource(std::shared_ptr<const EdenArchive::Source> inner)
            : mInner(std::move(inner))
        {
        }

        std::uint64_t size() const noexcept override { return mInner->size(); }

        bool read(std::uint64_t offset, void* destination, std::size_t length) const noexcept override
        {
            mReads.fetch_add(1);
            mBytes.fetch_add(length);
            return mInner->read(offset, destination, length);
        }

        void reset() const
        {
            mReads.store(0);
            mBytes.store(0);
        }

        std::uint64_t reads() const { return mReads.load(); }
        std::uint64_t bytes() const { return mBytes.load(); }

    private:
        std::shared_ptr<const EdenArchive::Source> mInner;
        mutable std::atomic<std::uint64_t> mReads{ 0 };
        mutable std::atomic<std::uint64_t> mBytes{ 0 };
    };

    // ---- the manifest -------------------------------------------------------------------------------------------

    TEST(LodTilePack, manifest_writes_the_pinned_canonical_text)
    {
        EXPECT_EQ(writeManifestJson(canonicalValue()), canonicalManifest);
        EXPECT_EQ(canonicalManifest,
            "{\"albedoSize\":256,\"bakerVersion\":1,\"gridSize\":33,\"layoutVersion\":1,\"levels\":[1],"
            "\"worldspaces\":[{\"cellSize\":8192,\"index\":0,\"name\":\"Morrowind\"}]}");
    }

    TEST(LodTilePack, manifest_parses_the_pinned_text_and_round_trips_byte_for_byte)
    {
        TileManifest parsed;
        const LodError error = parseManifestJson(canonicalManifest, parsed);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_EQ(parsed, canonicalValue());
        EXPECT_EQ(writeManifestJson(parsed), canonicalManifest);
    }

    TEST(LodTilePack, manifest_round_trips_other_values)
    {
        TileManifest several;
        several.mLayoutVersion = tileKeyLayoutVersion;
        several.mBakerVersion = 4000000000u;
        several.mWorldspaces = { WorldspaceInfo{ 0, "Morrowind", 8192 }, WorldspaceInfo{ 1, "Solstheim", 8192 },
            WorldspaceInfo{ 255, "", 4096 } };
        several.mLevels = { 1, 2, 3, 15 };
        several.mGridSize = 129;
        several.mAlbedoSize = 0;

        TileManifest empty;
        empty.mBakerVersion = 0;
        empty.mGridSize = 3;

        TileManifest odd = makeManifest(33, 256);
        odd.mWorldspaces[0].mName = "a\"b\\c \xC3\xA9 tab\there";

        for (const TileManifest* manifest : { &several, &empty, &odd })
        {
            const std::string text = writeManifestJson(*manifest);
            TileManifest parsed;
            const LodError error = parseManifestJson(text, parsed);
            ASSERT_TRUE(error.ok()) << error.describe() << " in " << text;
            EXPECT_EQ(parsed, *manifest);
            EXPECT_EQ(writeManifestJson(parsed), text);
            EXPECT_EQ(text.find('\n'), std::string::npos);
            if (manifest != &odd)
            {
                EXPECT_EQ(text.find(' '), std::string::npos);
            }
        }
    }

    void expectManifestRefusedAt(const std::string& text, std::uint64_t expectedOffset, const char* what)
    {
        SCOPED_TRACE(what);
        TileManifest parsed;
        const LodError error = parseManifestJson(text, parsed);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
        EXPECT_EQ(error.mOffset, expectedOffset);
    }

    void expectManifestRefusedWithin(const std::string& text, std::uint64_t first, std::uint64_t last, const char* what)
    {
        SCOPED_TRACE(what);
        TileManifest parsed;
        const LodError error = parseManifestJson(text, parsed);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
        EXPECT_GE(error.mOffset, first);
        EXPECT_LE(error.mOffset, last);
    }

    TEST(LodTilePack, manifest_refuses_added_whitespace_at_its_offset)
    {
        expectManifestRefusedAt(" " + canonicalManifest, 0, "leading space");
        {
            std::string text = canonicalManifest;
            text.insert(1, " ");
            expectManifestRefusedAt(text, 1, "space after the opening brace");
        }
        {
            std::string text = canonicalManifest;
            const std::size_t colon = text.find(':') + 1;
            text.insert(colon, " ");
            expectManifestRefusedAt(text, colon, "space after the first colon");
        }
        {
            std::string text = canonicalManifest;
            const std::size_t comma = text.find(',') + 1;
            text.insert(comma, " ");
            expectManifestRefusedAt(text, comma, "space after the first comma");
        }
        {
            std::string text = canonicalManifest;
            text.insert(text.size() - 1, " ");
            expectManifestRefusedAt(text, text.size() - 2, "space before the closing brace");
        }
    }

    TEST(LodTilePack, manifest_refuses_trailing_text_at_the_end_of_the_canonical_form)
    {
        expectManifestRefusedAt(canonicalManifest + "x", canonicalManifest.size(), "trailing garbage");
        expectManifestRefusedAt(canonicalManifest + "\n", canonicalManifest.size(), "trailing newline");
        expectManifestRefusedAt(canonicalManifest + canonicalManifest, canonicalManifest.size(), "two objects");
    }

    TEST(LodTilePack, manifest_refuses_reordered_keys)
    {
        const std::string text = replaceOnce(
            canonicalManifest, "\"albedoSize\":256,\"bakerVersion\":1", "\"bakerVersion\":1,\"albedoSize\":256");
        // The first key where "albedoSize" is expected: at the quote (offset 1) or the first differing letter (2).
        expectManifestRefusedWithin(text, 1, 2, "bakerVersion before albedoSize");
    }

    TEST(LodTilePack, manifest_refuses_a_missing_key)
    {
        const std::string noGrid = replaceOnce(canonicalManifest, "\"gridSize\":33,", "");
        TileManifest parsed;
        EXPECT_FALSE(parseManifestJson(noGrid, parsed).ok());
        expectManifestRefusedWithin(noGrid, 0, noGrid.size(), "no gridSize");

        const std::string noWorldspaces = replaceOnce(
            canonicalManifest, ",\"worldspaces\":[{\"cellSize\":8192,\"index\":0,\"name\":\"Morrowind\"}]", "");
        expectManifestRefusedWithin(noWorldspaces, 0, noWorldspaces.size(), "no worldspaces");

        const std::string noName = replaceOnce(canonicalManifest, ",\"name\":\"Morrowind\"", "");
        expectManifestRefusedWithin(noName, 0, noName.size(), "no worldspace name");
    }

    TEST(LodTilePack, manifest_refuses_a_value_of_the_wrong_type_at_the_start_of_the_value)
    {
        const std::size_t valueStart = canonicalManifest.find(':') + 1;
        expectManifestRefusedAt(replaceOnce(canonicalManifest, "256", "\"256\""), valueStart, "string for a number");
        expectManifestRefusedAt(replaceOnce(canonicalManifest, "256", "true"), valueStart, "boolean for a number");
        expectManifestRefusedAt(replaceOnce(canonicalManifest, "256", "null"), valueStart, "null for a number");
        expectManifestRefusedAt(replaceOnce(canonicalManifest, "256", "[256]"), valueStart, "array for a number");
        expectManifestRefusedWithin(
            replaceOnce(canonicalManifest, "[1]", "1"), 0, canonicalManifest.size(), "number for an array");
        expectManifestRefusedWithin(
            replaceOnce(canonicalManifest, "\"Morrowind\"", "7"), 0, canonicalManifest.size(), "number for a string");
    }

    TEST(LodTilePack, manifest_refuses_numbers_that_are_too_large_or_badly_written)
    {
        const std::size_t valueStart = canonicalManifest.find(':') + 1;
        expectManifestRefusedWithin(
            replaceOnce(canonicalManifest, "256", "4294967296"), valueStart, valueStart + 10, "2^32");
        expectManifestRefusedWithin(replaceOnce(canonicalManifest, "256", "99999999999999999999999"), valueStart,
            valueStart + 23, "far too large");
        expectManifestRefusedWithin(
            replaceOnce(canonicalManifest, "256", "-1"), valueStart, valueStart + 2, "negative");
        expectManifestRefusedWithin(
            replaceOnce(canonicalManifest, "256", "0256"), valueStart, valueStart + 4, "leading zero");
        expectManifestRefusedWithin(
            replaceOnce(canonicalManifest, "256", "256.0"), valueStart, valueStart + 5, "fraction");
        expectManifestRefusedWithin(
            replaceOnce(canonicalManifest, "256", "2.56e2"), valueStart, valueStart + 6, "exponent");
    }

    TEST(LodTilePack, manifest_refuses_truncated_and_empty_text)
    {
        TileManifest parsed;
        EXPECT_FALSE(parseManifestJson("", parsed).ok());
        EXPECT_FALSE(parseManifestJson("{", parsed).ok());
        EXPECT_FALSE(parseManifestJson("{}", parsed).ok());
        EXPECT_FALSE(parseManifestJson("[]", parsed).ok());
        EXPECT_FALSE(parseManifestJson("null", parsed).ok());
        for (std::size_t length = 0; length < canonicalManifest.size(); ++length)
        {
            const std::string text = canonicalManifest.substr(0, length);
            const LodError error = parseManifestJson(text, parsed);
            EXPECT_FALSE(error.ok()) << "prefix of length " << length;
            EXPECT_LE(error.mOffset, text.size()) << "prefix of length " << length;
        }
        // A cut in the middle: the problem is where the text ends.
        const std::string half = canonicalManifest.substr(0, canonicalManifest.size() / 2);
        expectManifestRefusedWithin(half, 0, half.size(), "half of the text");
    }

    // ---- building and reading a pack -----------------------------------------------------------------------

    TEST(LodTilePack, the_output_does_not_depend_on_the_order_of_the_tiles)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> forward = buildPack(fixture);
        ASSERT_FALSE(forward.empty());

        Fixture reversed = fixture;
        std::reverse(reversed.mTiles.begin(), reversed.mTiles.end());
        EXPECT_EQ(buildPack(reversed), forward);

        Fixture rotated = fixture;
        std::rotate(rotated.mTiles.begin(), rotated.mTiles.begin() + 2, rotated.mTiles.end());
        EXPECT_EQ(buildPack(rotated), forward);

        Fixture threaded = fixture;
        threaded.mOptions.mThreads = 3;
        EXPECT_EQ(buildPack(threaded), forward);

        EXPECT_EQ(buildPack(fixture), forward);
    }

    TEST(LodTilePack, an_open_pack_reports_its_properties)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();
        EXPECT_TRUE(error.ok());

        EXPECT_NE(pack->archive().info().mPackFlags & EdenArchive::packFlagDerived, 0u);
        EXPECT_EQ(pack->generation(), 7u);
        EXPECT_EQ(pack->archive().info().mGeneration, 7u);
        EXPECT_EQ(pack->sourceDigest(), filledHash(0xAB));
        EXPECT_EQ(pack->manifest(), fixture.mManifest);

        std::vector<TileId> expectedIds;
        for (const BakedTile& baked : fixture.mTiles)
        {
            expectedIds.push_back(baked.mId);
        }
        std::sort(expectedIds.begin(), expectedIds.end(),
            [](const TileId& a, const TileId& b) { return *packTileKey(a) < *packTileKey(b); });
        EXPECT_EQ(pack->tileIds(), expectedIds);

        for (const TileId& id : expectedIds)
        {
            EXPECT_TRUE(pack->contains(id));
        }
        EXPECT_FALSE(pack->contains(*tileContainingCell(0, 1, 100, 100)));
        EXPECT_FALSE(pack->contains(*tileContainingCell(0, 2, 0, 0)));
        EXPECT_FALSE(pack->contains(*tileContainingCell(1, 1, 0, 0)));
    }

    TEST(LodTilePack, the_manifest_is_a_path_entry_holding_the_canonical_text)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        const std::optional<std::size_t> index = pack->archive().findPath(manifestPath);
        ASSERT_TRUE(index.has_value());
        std::vector<std::uint8_t> content;
        const EdenArchive::Error readError = pack->archive().readEntry(*index, content);
        ASSERT_FALSE(readError) << readError.describe();
        EXPECT_EQ(content, toBytes(writeManifestJson(fixture.mManifest)));
        EXPECT_EQ(pack->archive().entryCount(), fixture.mTiles.size() + 1);
    }

    TEST(LodTilePack, every_tile_reads_back_equal_to_the_input)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        for (const BakedTile& baked : fixture.mTiles)
        {
            SCOPED_TRACE(testing::Message() << "tile " << baked.mId.mX << "," << baked.mId.mY);
            TerrainTile tile;
            const LodError readError = pack->readTile(baked.mId, tile);
            ASSERT_TRUE(readError.ok()) << readError.describe();
            expectSameTile(baked.mData, tile);

            TerrainTile header;
            TerrainTileLayout layout;
            const LodError headerError = pack->readHeader(baked.mId, header, layout);
            ASSERT_TRUE(headerError.ok()) << headerError.describe();
            EXPECT_EQ(header.mGridSize, baked.mData.mGridSize);
            EXPECT_EQ(header.mAlbedoWidth, baked.mData.mAlbedoWidth);
            EXPECT_EQ(header.mGeometricError, baked.mData.mGeometricError);
            EXPECT_EQ(header.mOriginX, baked.mData.mOriginX);
            EXPECT_TRUE(header.mHeights.empty());
            EXPECT_TRUE(header.mAlbedo.empty());
            std::vector<std::uint8_t> payload;
            ASSERT_TRUE(writeTerrainTile(baked.mData, payload).ok());
            EXPECT_EQ(layout.mTotalSize, payload.size());

            std::vector<std::uint8_t> albedo;
            const LodError albedoError = pack->readAlbedo(baked.mId, albedo);
            ASSERT_TRUE(albedoError.ok()) << albedoError.describe();
            EXPECT_EQ(albedo, baked.mData.mAlbedo);
        }
    }

    // Task 0133 stage 3: a neutral world's terrain pack is compressed. Everything that reads it (whole tiles, headers,
    // the albedo range, the digest and provenance a staleness check compares) must give what the stored pack gives.
    TEST(LodTilePack, a_compressed_pack_reads_back_like_a_stored_one)
    {
        Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> storedBytes = buildPack(fixture);
        fixture.mOptions.mStoreOnly = false;
        fixture.mOptions.mZstdLevel = 9;
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        EXPECT_NE(bytes, storedBytes);

        LodError error;
        const std::unique_ptr<TilePack> stored = openBytes(storedBytes, error);
        ASSERT_NE(stored, nullptr) << error.describe();
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();
        EXPECT_EQ(pack->sourceDigest(), stored->sourceDigest());
        EXPECT_EQ(pack->generation(), stored->generation());
        EXPECT_TRUE(pack->manifest() == stored->manifest());
        ASSERT_EQ(pack->tileIds().size(), fixture.mTiles.size());

        for (const BakedTile& baked : fixture.mTiles)
        {
            SCOPED_TRACE(testing::Message() << "tile " << baked.mId.mX << "," << baked.mId.mY);
            TerrainTile tile;
            const LodError readError = pack->readTile(baked.mId, tile);
            ASSERT_TRUE(readError.ok()) << readError.describe();
            expectSameTile(baked.mData, tile);
            TerrainTile header;
            TerrainTileLayout layout;
            ASSERT_TRUE(pack->readHeader(baked.mId, header, layout).ok());
            EXPECT_EQ(header.mGridSize, baked.mData.mGridSize);
            std::vector<std::uint8_t> albedo;
            ASSERT_TRUE(pack->readAlbedo(baked.mId, albedo).ok());
            EXPECT_EQ(albedo, baked.mData.mAlbedo);
        }
    }

    TEST(LodTilePack, tile_entries_have_type_0x0010_a_tagged_key_and_provenance)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        for (const BakedTile& baked : fixture.mTiles)
        {
            SCOPED_TRACE(testing::Message() << "tile " << baked.mId.mX << "," << baked.mId.mY);
            const std::uint64_t key = *packTileKey(baked.mId);
            const std::optional<std::size_t> index = pack->archive().findKey(key);
            ASSERT_TRUE(index.has_value());
            const EdenArchive::EntryInfo entry = pack->archive().entry(*index);
            EXPECT_TRUE(entry.mHasKey);
            EXPECT_EQ(entry.mKey, key);
            EXPECT_EQ(entry.mKey >> 56, 0x10u);
            EXPECT_EQ(entry.mType, 0x0010u);
            EXPECT_EQ(entry.mType, entryTypeTerrainTile);
            EXPECT_TRUE(entry.mHasProvenance);
            EXPECT_EQ(entry.mLayoutParamA, baked.mData.mGridSize);
            EXPECT_EQ(entry.mLayoutParamB, baked.mData.mAlbedoWidth);

            std::vector<std::uint8_t> payload;
            ASSERT_TRUE(writeTerrainTile(baked.mData, payload).ok());
            EXPECT_EQ(entry.mSize, payload.size());

            const std::optional<EdenArchive::ProvenanceInfo> provenance = pack->archive().provenance(*index);
            ASSERT_TRUE(provenance.has_value());
            EXPECT_EQ(provenance->mToolId, bakerToolId);
            EXPECT_EQ(provenance->mToolId, "openeden.lod.terrain");
            EXPECT_EQ(provenance->mSourceDigest, baked.mSourceDigest);
            EXPECT_EQ(std::vector<std::uint8_t>(provenance->mParameters.begin(), provenance->mParameters.end()),
                baked.mParameters);
        }
    }

    TEST(LodTilePack, build_refuses_a_duplicate_tile_id)
    {
        Fixture fixture = makeFixture();
        fixture.mTiles.push_back(fixture.mTiles[1]);
        std::vector<std::uint8_t> bytes;
        const LodError error = buildTilePack(fixture.mManifest, fixture.mTiles, fixture.mOptions, bytes);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
    }

    TEST(LodTilePack, build_refuses_an_id_that_does_not_pack)
    {
        Fixture fixture = makeFixture();
        fixture.mTiles[2].mId.mLevel = 16;
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(buildTilePack(fixture.mManifest, fixture.mTiles, fixture.mOptions, bytes).ok());

        fixture = makeFixture();
        fixture.mTiles[0].mId.mX = 1u << 22;
        EXPECT_FALSE(buildTilePack(fixture.mManifest, fixture.mTiles, fixture.mOptions, bytes).ok());
    }

    TEST(LodTilePack, build_refuses_an_invalid_tile)
    {
        Fixture fixture = makeFixture();
        fixture.mTiles[3].mData.mMinHeight = 1000.f; // above the maximum
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(buildTilePack(fixture.mManifest, fixture.mTiles, fixture.mOptions, bytes).ok());

        fixture = makeFixture();
        fixture.mTiles[0].mData.mHeights.pop_back();
        EXPECT_FALSE(buildTilePack(fixture.mManifest, fixture.mTiles, fixture.mOptions, bytes).ok());
    }

    TEST(LodTilePack, a_pack_of_no_tiles_is_valid_and_empty)
    {
        Fixture fixture = makeFixture();
        fixture.mTiles.clear();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();
        EXPECT_TRUE(pack->tileIds().empty());
        EXPECT_EQ(pack->manifest(), fixture.mManifest);
    }

    // ---- opening bad packs ---------------------------------------------------------------------------------

    TEST(LodTilePack, open_refuses_a_pack_without_a_manifest)
    {
        const std::vector<std::uint8_t> bytes = rawPack(true, nullptr, {});
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        EXPECT_EQ(pack, nullptr);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
    }

    TEST(LodTilePack, open_refuses_a_pack_without_the_derived_flag)
    {
        const std::vector<std::uint8_t> bytes = rawPack(false, &canonicalManifest, {});
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        EXPECT_EQ(pack, nullptr);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());

        // The same content with the flag is accepted.
        LodError accepted;
        EXPECT_NE(openBytes(rawPack(true, &canonicalManifest, {}), accepted), nullptr) << accepted.describe();
    }

    TEST(LodTilePack, open_refuses_a_manifest_with_another_layout_version)
    {
        for (const char* version : { "0", "2", "4000000000" })
        {
            const std::string text
                = replaceOnce(canonicalManifest, "\"layoutVersion\":1", std::string("\"layoutVersion\":") + version);
            const std::vector<std::uint8_t> bytes = rawPack(true, &text, {});
            LodError error;
            const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
            EXPECT_EQ(pack, nullptr) << version;
            EXPECT_FALSE(error.ok()) << version;
            EXPECT_FALSE(error.mField.empty()) << version;
        }
    }

    TEST(LodTilePack, open_refuses_a_malformed_manifest)
    {
        for (const std::string& text : { std::string("not json"), std::string("{}"), canonicalManifest + "\n",
                 replaceOnce(canonicalManifest, "256", "\"256\"") })
        {
            const std::vector<std::uint8_t> bytes = rawPack(true, &text, {});
            LodError error;
            EXPECT_EQ(openBytes(bytes, error), nullptr) << text;
            EXPECT_FALSE(error.ok()) << text;
        }
    }

    TEST(LodTilePack, open_refuses_bytes_that_are_not_a_pack)
    {
        LodError error;
        EXPECT_EQ(openBytes(std::vector<std::uint8_t>(), error), nullptr);
        EXPECT_FALSE(error.ok());

        error = LodError();
        EXPECT_EQ(openBytes(toBytes("this is not an Eden Archive at all, just some text"), error), nullptr);
        EXPECT_FALSE(error.ok());

        error = LodError();
        const std::vector<std::uint8_t> good = buildPack(makeFixture());
        std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(good.size() / 2));
        EXPECT_EQ(openBytes(cut, error), nullptr);
        EXPECT_FALSE(error.ok());
    }

    TEST(LodTilePack, open_refuses_a_missing_file)
    {
        LodError error;
        const std::filesystem::path missing
            = std::filesystem::temp_directory_path() / "openeden_lod_no_such_pack.edenpak";
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        EXPECT_EQ(TilePack::open(missing, error), nullptr);
        EXPECT_FALSE(error.ok());
    }

    // ---- a damaged tile ----------------------------------------------------------------------------------------

    TEST(LodTilePack, a_damaged_tile_fails_to_read_and_the_other_tiles_still_read)
    {
        const Fixture fixture = makeFixture();
        std::vector<std::uint8_t> bytes = buildPack(fixture);

        // Find the first tile's albedo in the (stored, uncompressed) pack and damage one byte of it.
        const std::vector<std::uint8_t>& albedo = fixture.mTiles[0].mData.mAlbedo;
        ASSERT_GE(albedo.size(), 64u);
        const auto found = std::search(bytes.begin(), bytes.end(), albedo.begin(), albedo.begin() + 16);
        ASSERT_NE(found, bytes.end());
        *(found + 8) ^= 0x40;

        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        TerrainTile tile;
        EXPECT_FALSE(pack->readTile(fixture.mTiles[0].mId, tile).ok());
        std::vector<std::uint8_t> albedoBytes;
        EXPECT_FALSE(pack->readAlbedo(fixture.mTiles[0].mId, albedoBytes).ok());

        for (std::size_t i = 1; i < fixture.mTiles.size(); ++i)
        {
            TerrainTile other;
            const LodError readError = pack->readTile(fixture.mTiles[i].mId, other);
            ASSERT_TRUE(readError.ok()) << i << ": " << readError.describe();
            expectSameTile(fixture.mTiles[i].mData, other);
        }
    }

    TEST(LodTilePack, reading_an_unknown_tile_is_an_error)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        const TileId unknown = *tileContainingCell(0, 1, 100, 100);
        TerrainTile tile;
        TerrainTileLayout layout;
        std::vector<std::uint8_t> albedo;
        EXPECT_FALSE(pack->readTile(unknown, tile).ok());
        EXPECT_FALSE(pack->readHeader(unknown, tile, layout).ok());
        EXPECT_FALSE(pack->readAlbedo(unknown, albedo).ok());
    }

    TEST(LodTilePack, a_tile_entry_that_is_not_a_tile_is_refused_when_read_not_when_opened_or_not_at_all)
    {
        const Fixture fixture = makeFixture();
        std::vector<std::uint8_t> goodPayload;
        ASSERT_TRUE(writeTerrainTile(fixture.mTiles[0].mData, goodPayload).ok());
        const std::string manifestText = writeManifestJson(fixture.mManifest);

        std::vector<std::uint8_t> shortPayload(goodPayload.begin(), goodPayload.begin() + 100);
        const std::uint64_t goodKey = *packTileKey(fixture.mTiles[0].mId);
        const std::uint64_t garbageKey = *packTileKey(fixture.mTiles[1].mId);
        const std::uint64_t shortKey = *packTileKey(fixture.mTiles[2].mId);
        const std::vector<std::uint8_t> bytes = rawPack(true, &manifestText,
            { { goodKey, goodPayload }, { garbageKey, toBytes("this is certainly not a terrain tile") },
                { shortKey, shortPayload } });

        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        if (pack == nullptr)
        {
            // Refusing the whole pack at open is also acceptable, with a located error.
            EXPECT_FALSE(error.ok());
            return;
        }
        TerrainTile tile;
        EXPECT_TRUE(pack->readTile(fixture.mTiles[0].mId, tile).ok());
        expectSameTile(fixture.mTiles[0].mData, tile);
        TerrainTile bad;
        EXPECT_FALSE(pack->readTile(fixture.mTiles[1].mId, bad).ok());
        EXPECT_FALSE(pack->readTile(fixture.mTiles[2].mId, bad).ok());
    }

    // ---- reads that touch only what they need ---------------------------------------------------------------

    TEST(LodTilePack, reading_the_header_touches_far_less_than_the_whole_entry)
    {
        // A tile of about 355 KB: N = 33 and a 256 x 256 albedo with 9 mips. Stored, it spans several of the
        // container's default 128 KiB chunks. The albedo is the bulk of the entry and starts in the first chunk, so a
        // read of the albedo cannot be cheaper than the entry (nothing is asserted about it); the header, the
        // manifest-sized and other small reads are.
        Fixture fixture;
        fixture.mManifest = makeManifest(33, 256);
        BakedTile baked;
        baked.mId = *tileContainingCell(0, 1, 0, 0);
        baked.mData = makeTile(33, true, 256, 9, 5);
        baked.mSourceDigest = filledHash(0x44);
        fixture.mTiles.push_back(baked);
        fixture.mOptions.mStoreOnly = true;
        const std::vector<std::uint8_t> bytes = buildPack(fixture);

        std::vector<std::uint8_t> payload;
        ASSERT_TRUE(writeTerrainTile(baked.mData, payload).ok());
        ASSERT_GT(payload.size(), 300000u);

        const auto counting = std::make_shared<CountingSource>(EdenArchive::makeMemorySource(bytes));
        LodError error;
        const std::unique_ptr<TilePack> pack = TilePack::open(counting, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        // The container keeps at least three chunks for this entry.
        const std::optional<std::size_t> index = pack->archive().findKey(*packTileKey(baked.mId));
        ASSERT_TRUE(index.has_value());
        EXPECT_GE(pack->archive().entry(*index).mChunkCount, 3u);

        counting->reset();
        TerrainTile header;
        TerrainTileLayout layout;
        const LodError headerError = pack->readHeader(baked.mId, header, layout);
        ASSERT_TRUE(headerError.ok()) << headerError.describe();
        EXPECT_GE(counting->reads(), 1u);
        EXPECT_LT(counting->bytes(), payload.size() / 2);
        const std::uint64_t headerBytes = counting->bytes();

        counting->reset();
        TerrainTile whole;
        ASSERT_TRUE(pack->readTile(baked.mId, whole).ok());
        EXPECT_GE(counting->bytes(), payload.size());
        EXPECT_LT(headerBytes, counting->bytes());
        expectSameTile(baked.mData, whole);

        counting->reset();
        std::vector<std::uint8_t> albedo;
        ASSERT_TRUE(pack->readAlbedo(baked.mId, albedo).ok());
        EXPECT_EQ(albedo, baked.mData.mAlbedo);
    }

    TEST(LodTilePack, a_range_read_of_the_tail_of_a_tile_reads_only_the_chunks_it_needs)
    {
        Fixture fixture;
        fixture.mManifest = makeManifest(33, 256);
        BakedTile baked;
        baked.mId = *tileContainingCell(0, 1, 0, 0);
        baked.mData = makeTile(33, true, 256, 9, 6);
        fixture.mTiles.push_back(baked);
        fixture.mOptions.mStoreOnly = true;
        const std::vector<std::uint8_t> bytes = buildPack(fixture);

        std::vector<std::uint8_t> payload;
        ASSERT_TRUE(writeTerrainTile(baked.mData, payload).ok());

        const auto counting = std::make_shared<CountingSource>(EdenArchive::makeMemorySource(bytes));
        LodError error;
        const std::unique_ptr<TilePack> pack = TilePack::open(counting, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        const std::optional<std::size_t> index = pack->archive().findKey(*packTileKey(baked.mId));
        ASSERT_TRUE(index.has_value());
        counting->reset();
        std::vector<std::uint8_t> tail(64);
        const EdenArchive::Error readError
            = pack->archive().readRange(*index, payload.size() - tail.size(), tail.size(), tail.data());
        ASSERT_FALSE(readError) << readError.describe();
        EXPECT_TRUE(std::equal(tail.begin(), tail.end(), payload.end() - static_cast<std::ptrdiff_t>(tail.size())));
        EXPECT_LT(counting->bytes(), payload.size() / 2);
    }

    // ---- files ------------------------------------------------------------------------------------------------

    TEST(LodTilePack, a_pack_written_to_a_file_is_the_same_bytes_and_opens_by_path)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> expected = buildPack(fixture);

        const std::filesystem::path path
            = std::filesystem::temp_directory_path() / "openeden_lod_tilepack_test.edenpak";
        std::error_code ignored;
        std::filesystem::remove(path, ignored);

        const LodError writeError = writeTilePack(fixture.mManifest, fixture.mTiles, fixture.mOptions, path);
        ASSERT_TRUE(writeError.ok()) << writeError.describe();

        {
            std::ifstream stream(path, std::ios::binary);
            ASSERT_TRUE(stream.good());
            const std::vector<std::uint8_t> onDisk(
                (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
            EXPECT_EQ(onDisk, expected);
        }
        {
            LodError error;
            const std::unique_ptr<TilePack> pack = TilePack::open(path, error);
            ASSERT_NE(pack, nullptr) << error.describe();
            EXPECT_EQ(pack->generation(), 7u);
            EXPECT_EQ(pack->tileIds().size(), fixture.mTiles.size());
            TerrainTile tile;
            ASSERT_TRUE(pack->readTile(fixture.mTiles[4].mId, tile).ok());
            expectSameTile(fixture.mTiles[4].mData, tile);
        }
        std::filesystem::remove(path, ignored);
    }

    // Task 0066 stage e: the staleness check of a pack read up to a level compares the tiles it needs one by one.
    TEST(LodTilePack, a_tile_reports_the_source_digest_it_was_written_with)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();
        for (const BakedTile& baked : fixture.mTiles)
        {
            const std::optional<EdenArchive::Hash> digest = pack->tileSourceDigest(baked.mId);
            ASSERT_TRUE(digest.has_value());
            EXPECT_EQ(*digest, baked.mSourceDigest);
        }
    }

    TEST(LodTilePack, a_tile_that_is_not_in_the_pack_has_no_source_digest)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<TilePack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();
        const std::optional<TileId> other = tileContainingCell(0, 1, 40, 40);
        ASSERT_TRUE(other.has_value());
        EXPECT_FALSE(pack->tileSourceDigest(*other).has_value());
    }
}
