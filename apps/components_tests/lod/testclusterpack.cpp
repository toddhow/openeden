// Task 0059 (stage S4c): the manifest and the derived pack of baked object clusters (DRAFT layout version 0). Pure code
// over generated clusters and in-memory packs; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/clusterpack.hpp>

#include <edenarchive/source.hpp>
#include <edenarchive/writer.hpp>

#include <algorithm>
#include <chrono>
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

    constexpr float cellSizeUnits = 8192.f;

    // ---- helpers ------------------------------------------------------------------------------------------------

    EdenArchive::Hash filledHash(std::uint8_t value)
    {
        EdenArchive::Hash hash{};
        hash.fill(value);
        return hash;
    }

    std::vector<std::uint8_t> toBytes(const std::string& text)
    {
        return std::vector<std::uint8_t>(text.begin(), text.end());
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

    const std::string canonicalManifest
        = R"({"bakerVersion":1,"cellSize":8192,"errors":[64,192,576],"layoutVersion":0,"levels":[1,2,3],)"
          R"("worldspace":"Morrowind"})";

    ClusterManifest canonicalValue()
    {
        ClusterManifest manifest;
        manifest.mLayoutVersion = 0;
        manifest.mBakerVersion = 1;
        manifest.mCellSize = 8192;
        manifest.mWorldspace = "Morrowind";
        manifest.mLevels = { 1, 2, 3 };
        manifest.mErrors = { 64, 192, 576 };
        return manifest;
    }

    struct VertexSpec
    {
        float mPos[3];
        std::int8_t mNormal[3];
        std::uint8_t mSlot;
        std::uint8_t mColor[4];
        float mUv[2];
    };

    // Positions are relative to the tile origin in x and y; all numbers are exactly representable.
    const VertexSpec vertexSpecs[6] = {
        { { 1000, 2000, 100 }, { 0, 0, 127 }, 0, { 255, 0, 0, 255 }, { 0, 0 } },
        { { 2000, 2000, 200 }, { -127, 64, 0 }, 0, { 255, 10, 0, 255 }, { 1, 0 } },
        { { 1500, 3000, 300 }, { 0, 0, 127 }, 0, { 255, 20, 0, 255 }, { 0, 1 } },
        { { 9000, 2000, 150 }, { 0, 127, 0 }, 1, { 255, 30, 0, 255 }, { 0.25f, 0.25f } },
        { { 10000, 2500, 500 }, { 0, 127, 0 }, 1, { 255, 40, 0, 255 }, { 0.5f, 0.5f } },
        { { 9500, 3500, 250 }, { 0, 127, 0 }, 1, { 255, 50, 0, 255 }, { 0.75f, 0.75f } },
    };

    const std::uint32_t handIndices[9] = { 0, 1, 2, 3, 4, 5, 3, 5, 4 };

    // A valid cluster of the tile at `level` containing cell (cellX, cellY); `seed` makes the content differ.
    ObjectCluster makeCluster(std::uint32_t level, std::int32_t cellX, std::int32_t cellY, std::uint32_t seed)
    {
        const float zShift = static_cast<float>(seed);
        ObjectCluster cluster;
        cluster.mLevel = level;
        cluster.mSide = cellSizeUnits * static_cast<float>(1u << level);
        cluster.mOriginX = static_cast<float>(cellX) * cellSizeUnits;
        cluster.mOriginY = static_cast<float>(cellY) * cellSizeUnits;
        cluster.mMinZ = 100 + zShift;
        cluster.mMaxZ = 500 + zShift;
        cluster.mGeometricError = 64;
        cluster.mOverhang = 0;
        cluster.mSourceTriangles = 5;

        ClusterGroup first;
        first.mTexture = "textures/a.dds";
        first.mFirstIndex = 0;
        first.mIndexCount = 6;
        ClusterGroup second;
        second.mTexture = "textures/tree.dds";
        second.mFlags = clusterFlagAlphaTest;
        second.mAlphaCutoff = 0.5f;
        second.mFirstIndex = 6;
        second.mIndexCount = 3;
        cluster.mGroups = { first, second };

        cluster.mRanges = { ClusterRange{ 0, 0, 0, 3 }, ClusterRange{ 0, 1, 3, 3 }, ClusterRange{ 1, 1, 6, 3 } };
        cluster.mCells = { ClusterCell{ cellX, cellY, 0, 2 }, ClusterCell{ cellX + 1, cellY, 2, 1 } };
        cluster.mRefs = { ClusterRef{ 1 + seed, 0 }, ClusterRef{ 5 + seed, 0 }, ClusterRef{ 2 + seed, 1 } };

        for (const VertexSpec& spec : vertexSpecs)
        {
            ClusterVertex vertex;
            for (std::size_t k = 0; k < 3; ++k)
            {
                vertex.mPosition[k] = spec.mPos[k];
                vertex.mNormal[k] = spec.mNormal[k];
            }
            vertex.mPosition[2] += zShift;
            vertex.mCellSlot = spec.mSlot;
            for (std::size_t k = 0; k < 4; ++k)
            {
                vertex.mColor[k] = spec.mColor[k];
            }
            vertex.mUv[0] = spec.mUv[0];
            vertex.mUv[1] = spec.mUv[1];
            cluster.mVertices.push_back(vertex);
        }
        cluster.mIndices.assign(std::begin(handIndices), std::end(handIndices));
        return cluster;
    }

    BakedCluster makeBaked(std::uint32_t level, std::int32_t cellX, std::int32_t cellY, std::uint32_t seed)
    {
        BakedCluster baked;
        const std::optional<TileId> id = tileContainingCell(0, level, cellX, cellY);
        EXPECT_TRUE(id.has_value());
        baked.mId = *id;
        std::int32_t originX = 0;
        std::int32_t originY = 0;
        tileOriginCell(baked.mId, originX, originY);
        baked.mData = makeCluster(level, originX, originY, seed);
        baked.mSourceDigest = filledHash(static_cast<std::uint8_t>(0x20 + seed));
        baked.mParameters = { static_cast<std::uint8_t>(seed), 9, 8, 7 };
        return baked;
    }

    struct Fixture
    {
        ClusterManifest mManifest;
        std::vector<BakedCluster> mClusters;
        TilePackOptions mOptions;
    };

    // Two level-1 clusters (one at negative cells) and one level-2 cluster.
    Fixture makeFixture()
    {
        Fixture fixture;
        fixture.mManifest = canonicalValue();
        fixture.mClusters.push_back(makeBaked(1, 0, 0, 0));
        fixture.mClusters.push_back(makeBaked(1, -2, 0, 1));
        fixture.mClusters.push_back(makeBaked(2, 4, 0, 2));
        fixture.mOptions.mGeneration = 9;
        fixture.mOptions.mSourceDigest = filledHash(0xCD);
        fixture.mOptions.mThreads = 1;
        fixture.mOptions.mStoreOnly = true;
        return fixture;
    }

    std::vector<std::uint8_t> buildPack(const Fixture& fixture)
    {
        std::vector<std::uint8_t> bytes;
        const LodError error = buildClusterPack(fixture.mManifest, fixture.mClusters, fixture.mOptions, bytes);
        EXPECT_TRUE(error.ok()) << error.describe();
        return bytes;
    }

    std::unique_ptr<ClusterPack> openBytes(const std::vector<std::uint8_t>& bytes, LodError& error)
    {
        return ClusterPack::open(EdenArchive::makeMemorySource(bytes), error);
    }

    // A pack written directly with the container writer, for the cases buildClusterPack would never produce.
    std::vector<std::uint8_t> rawPack(bool derived, const std::string* manifestText,
        const std::vector<std::pair<std::uint64_t, std::vector<std::uint8_t>>>& clusterEntries)
    {
        std::vector<EdenArchive::InputEntry> entries;
        if (manifestText != nullptr)
        {
            EdenArchive::InputEntry manifest;
            manifest.mPath = std::string(clusterManifestPath);
            manifest.mData = toBytes(*manifestText);
            manifest.mProvenance = EdenArchive::ProvenanceInput{ "openeden.lod.test", filledHash(1), {} };
            entries.push_back(std::move(manifest));
        }
        for (const auto& clusterEntry : clusterEntries)
        {
            EdenArchive::InputEntry entry;
            entry.mKey = clusterEntry.first;
            entry.mType = entryTypeObjectCluster;
            entry.mData = clusterEntry.second;
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

    void expectManifestRefused(const std::string& text, const char* what)
    {
        SCOPED_TRACE(what);
        ClusterManifest parsed;
        const LodError error = parseClusterManifestJson(text, parsed);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
        EXPECT_LE(error.mOffset, text.size());
    }

    // ---- the manifest -------------------------------------------------------------------------------------------

    TEST(LodClusterPack, manifest_writes_the_pinned_canonical_text)
    {
        EXPECT_EQ(writeClusterManifestJson(canonicalValue()), canonicalManifest);
        EXPECT_EQ(canonicalManifest,
            "{\"bakerVersion\":1,\"cellSize\":8192,\"errors\":[64,192,576],\"layoutVersion\":0,\"levels\":[1,2,3],"
            "\"worldspace\":\"Morrowind\"}");
    }

    TEST(LodClusterPack, manifest_parses_the_pinned_text_and_round_trips_byte_for_byte)
    {
        ClusterManifest parsed;
        const LodError error = parseClusterManifestJson(canonicalManifest, parsed);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_EQ(parsed, canonicalValue());
        EXPECT_EQ(writeClusterManifestJson(parsed), canonicalManifest);
    }

    TEST(LodClusterPack, manifest_round_trips_other_values)
    {
        ClusterManifest single;
        single.mBakerVersion = 4000000000u;
        single.mCellSize = 4096;
        single.mWorldspace = "Solstheim";
        single.mLevels = { 2 };
        single.mErrors = { 192 };

        ClusterManifest unicode = canonicalValue();
        unicode.mWorldspace = "Caf\xC3\xA9 Nord";

        ClusterManifest unnamed = canonicalValue();
        unnamed.mWorldspace = "";

        for (const ClusterManifest* manifest : { &single, &unicode, &unnamed })
        {
            const std::string text = writeClusterManifestJson(*manifest);
            ClusterManifest parsed;
            const LodError error = parseClusterManifestJson(text, parsed);
            ASSERT_TRUE(error.ok()) << error.describe() << " in " << text;
            EXPECT_EQ(parsed, *manifest);
            EXPECT_EQ(writeClusterManifestJson(parsed), text);
            EXPECT_EQ(text.find('\n'), std::string::npos);
        }
    }

    TEST(LodClusterPack, manifest_refuses_every_non_canonical_variant)
    {
        expectManifestRefused(" " + canonicalManifest, "leading space");
        {
            std::string text = canonicalManifest;
            text.insert(1, " ");
            expectManifestRefused(text, "space after the opening brace");
        }
        {
            std::string text = canonicalManifest;
            text.insert(text.find(':') + 1, " ");
            expectManifestRefused(text, "space after the first colon");
        }
        expectManifestRefused(replaceOnce(canonicalManifest, "[64,192", "[64, 192"), "space in an array");
        {
            std::string text = canonicalManifest;
            text.insert(text.size() - 1, " ");
            expectManifestRefused(text, "space before the closing brace");
        }
        expectManifestRefused(replaceOnce(canonicalManifest, "\"bakerVersion\":1,\"cellSize\":8192",
                                  "\"cellSize\":8192,\"bakerVersion\":1"),
            "reordered keys");
        expectManifestRefused(replaceOnce(canonicalManifest, "8192", "08192"), "leading zero");
        expectManifestRefused(
            replaceOnce(canonicalManifest, "\"layoutVersion\":0", "\"layoutVersion\":00"), "leading zero on zero");
        expectManifestRefused(replaceOnce(canonicalManifest, "[64,", "[-64,"), "negative number");
        expectManifestRefused(replaceOnce(canonicalManifest, "8192", "8192.0"), "fraction");
        expectManifestRefused(replaceOnce(canonicalManifest, "8192", "4294967296"), "number above 2^32 - 1");
        expectManifestRefused(replaceOnce(canonicalManifest, "\"cellSize\":8192,", ""), "missing key");
        expectManifestRefused(replaceOnce(canonicalManifest, ",\"worldspace\":\"Morrowind\"", ""), "missing last key");
        expectManifestRefused(canonicalManifest + "x", "trailing text");
        expectManifestRefused(canonicalManifest + "\n", "trailing newline");
        expectManifestRefused(canonicalManifest + canonicalManifest, "two objects");
        expectManifestRefused(replaceOnce(canonicalManifest, "\"Morrowind\"", "\"Morrow\\u001Find\""), "uppercase hex");
        expectManifestRefused(replaceOnce(canonicalManifest, "{", "{\"aaa\":1,"), "unknown key first");
        expectManifestRefused(replaceOnce(canonicalManifest, "}", ",\"zzz\":1}"), "unknown key last");
        expectManifestRefused("", "empty text");
        expectManifestRefused("{}", "empty object");
        expectManifestRefused("[]", "array");
    }

    TEST(LodClusterPack, manifest_refuses_truncated_text)
    {
        for (std::size_t length = 0; length < canonicalManifest.size(); ++length)
        {
            ClusterManifest parsed;
            const LodError error = parseClusterManifestJson(canonicalManifest.substr(0, length), parsed);
            EXPECT_FALSE(error.ok()) << "prefix of length " << length;
        }
    }

    TEST(LodClusterPack, manifest_refuses_text_over_4096_bytes)
    {
        ClusterManifest big = canonicalValue();
        big.mWorldspace = std::string(4100, 'a');
        const std::string text = replaceOnce(canonicalManifest, "Morrowind", big.mWorldspace);
        ASSERT_GT(text.size(), 4096u);
        expectManifestRefused(text, "oversized text");
    }

    TEST(LodClusterPack, manifest_refuses_levels_outside_one_to_three)
    {
        expectManifestRefused(replaceOnce(canonicalManifest, "\"levels\":[1,2,3]", "\"levels\":[0,2,3]"), "level 0");
        expectManifestRefused(replaceOnce(canonicalManifest, "\"levels\":[1,2,3]", "\"levels\":[1,2,4]"), "level 4");
        expectManifestRefused(replaceOnce(canonicalManifest, "\"levels\":[1,2,3]", "\"levels\":[1,2,15]"), "level 15");
    }

    TEST(LodClusterPack, manifest_refuses_empty_levels)
    {
        const std::string text
            = replaceOnce(replaceOnce(canonicalManifest, "\"levels\":[1,2,3]", "\"levels\":[]"), "[64,192,576]", "[]");
        expectManifestRefused(text, "no levels");
    }

    TEST(LodClusterPack, manifest_refuses_levels_and_errors_of_different_length)
    {
        expectManifestRefused(replaceOnce(canonicalManifest, "[64,192,576]", "[64,192]"), "fewer errors");
        expectManifestRefused(replaceOnce(canonicalManifest, "[64,192,576]", "[64,192,576,1000]"), "more errors");
        expectManifestRefused(replaceOnce(canonicalManifest, "\"levels\":[1,2,3]", "\"levels\":[1,2]"), "fewer levels");
    }

    TEST(LodClusterPack, manifest_refuses_a_cell_size_of_zero)
    {
        expectManifestRefused(replaceOnce(canonicalManifest, "\"cellSize\":8192", "\"cellSize\":0"), "cell size 0");
    }

    TEST(LodClusterPack, manifest_refuses_a_worldspace_name_with_a_quote_or_backslash)
    {
        expectManifestRefused(replaceOnce(canonicalManifest, "Morrowind", "Morrow\\\"ind"), "escaped quote");
        expectManifestRefused(replaceOnce(canonicalManifest, "Morrowind", "Morrow\\\\ind"), "escaped backslash");
        expectManifestRefused(replaceOnce(canonicalManifest, "Morrowind", "Morrow\\ind"), "single backslash");
        expectManifestRefused(replaceOnce(canonicalManifest, "Morrowind", "Morrow\"ind"), "raw quote");
    }

    // ---- building and reading a pack ------------------------------------------------------------------------

    TEST(LodClusterPack, the_output_does_not_depend_on_the_order_of_the_clusters)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> forward = buildPack(fixture);
        ASSERT_FALSE(forward.empty());

        Fixture reversed = fixture;
        std::reverse(reversed.mClusters.begin(), reversed.mClusters.end());
        EXPECT_EQ(buildPack(reversed), forward);

        Fixture rotated = fixture;
        std::rotate(rotated.mClusters.begin(), rotated.mClusters.begin() + 1, rotated.mClusters.end());
        EXPECT_EQ(buildPack(rotated), forward);

        EXPECT_EQ(buildPack(fixture), forward);
    }

    TEST(LodClusterPack, build_refuses_a_duplicate_tile_id)
    {
        Fixture fixture = makeFixture();
        fixture.mClusters.push_back(fixture.mClusters[1]);
        std::vector<std::uint8_t> bytes;
        const LodError error = buildClusterPack(fixture.mManifest, fixture.mClusters, fixture.mOptions, bytes);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
    }

    TEST(LodClusterPack, build_refuses_a_cluster_whose_level_differs_from_its_id)
    {
        Fixture fixture = makeFixture();
        // The id says level 1; the data is a valid level-2 cluster.
        fixture.mClusters[0].mData = makeCluster(2, 0, 0, 0);
        std::vector<std::uint8_t> bytes;
        const LodError error = buildClusterPack(fixture.mManifest, fixture.mClusters, fixture.mOptions, bytes);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
    }

    TEST(LodClusterPack, build_refuses_an_invalid_cluster_and_an_id_that_does_not_pack)
    {
        Fixture fixture = makeFixture();
        fixture.mClusters[2].mData.mIndices.pop_back();
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(buildClusterPack(fixture.mManifest, fixture.mClusters, fixture.mOptions, bytes).ok());

        fixture = makeFixture();
        fixture.mClusters[0].mId.mLevel = 16;
        EXPECT_FALSE(buildClusterPack(fixture.mManifest, fixture.mClusters, fixture.mOptions, bytes).ok());
    }

    TEST(LodClusterPack, a_pack_of_no_clusters_is_valid_and_empty)
    {
        Fixture fixture = makeFixture();
        fixture.mClusters.clear();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<ClusterPack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();
        EXPECT_TRUE(pack->tileIds().empty());
        EXPECT_EQ(pack->manifest(), fixture.mManifest);
    }

    TEST(LodClusterPack, an_open_pack_reports_its_properties)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<ClusterPack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();
        EXPECT_TRUE(error.ok());

        EXPECT_NE(pack->archive().info().mPackFlags & EdenArchive::packFlagDerived, 0u);
        EXPECT_EQ(pack->generation(), 9u);
        EXPECT_EQ(pack->sourceDigest(), filledHash(0xCD));
        EXPECT_EQ(pack->manifest(), fixture.mManifest);

        std::vector<TileId> expectedIds;
        for (const BakedCluster& baked : fixture.mClusters)
        {
            expectedIds.push_back(baked.mId);
        }
        std::sort(expectedIds.begin(), expectedIds.end(),
            [](const TileId& a, const TileId& b) { return *packClusterKey(a) < *packClusterKey(b); });
        EXPECT_EQ(pack->tileIds(), expectedIds);

        for (const TileId& id : expectedIds)
        {
            EXPECT_TRUE(pack->contains(id));
        }
        EXPECT_FALSE(pack->contains(*tileContainingCell(0, 1, 100, 100)));
        EXPECT_FALSE(pack->contains(*tileContainingCell(0, 3, 0, 0)));
        EXPECT_FALSE(pack->contains(*tileContainingCell(1, 1, 0, 0)));
    }

    TEST(LodClusterPack, the_manifest_is_a_path_entry_holding_the_canonical_text)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<ClusterPack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        EXPECT_EQ(clusterManifestPath, "lod/objects.json");
        const std::optional<std::size_t> index = pack->archive().findPath(clusterManifestPath);
        ASSERT_TRUE(index.has_value());
        std::vector<std::uint8_t> content;
        const EdenArchive::Error readError = pack->archive().readEntry(*index, content);
        ASSERT_FALSE(readError) << readError.describe();
        EXPECT_EQ(content, toBytes(writeClusterManifestJson(fixture.mManifest)));
        EXPECT_EQ(pack->archive().entryCount(), fixture.mClusters.size() + 1);
    }

    TEST(LodClusterPack, every_cluster_reads_back_equal_to_the_input)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<ClusterPack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        for (const BakedCluster& baked : fixture.mClusters)
        {
            SCOPED_TRACE(testing::Message()
                << "tile level " << static_cast<int>(baked.mId.mLevel) << " " << baked.mId.mX << "," << baked.mId.mY);
            ObjectCluster cluster;
            const LodError readError = pack->readCluster(baked.mId, cluster);
            ASSERT_TRUE(readError.ok()) << readError.describe();
            EXPECT_EQ(cluster, baked.mData);
        }
    }

    TEST(LodClusterPack, cluster_entries_have_type_0x0011_a_tagged_key_and_provenance)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<ClusterPack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        for (const BakedCluster& baked : fixture.mClusters)
        {
            const std::uint64_t key = *packClusterKey(baked.mId);
            const std::optional<std::size_t> index = pack->archive().findKey(key);
            ASSERT_TRUE(index.has_value());
            const EdenArchive::EntryInfo entry = pack->archive().entry(*index);
            EXPECT_TRUE(entry.mHasKey);
            EXPECT_EQ(entry.mKey >> 56, 0x11u);
            EXPECT_EQ(entry.mType, 0x0011u);
            EXPECT_EQ(entry.mLayoutParamA, baked.mData.mGroups.size());
            EXPECT_EQ(entry.mLayoutParamB, baked.mData.mCells.size());
            EXPECT_TRUE(entry.mHasProvenance);

            std::vector<std::uint8_t> payload;
            ASSERT_TRUE(writeObjectCluster(baked.mData, payload).ok());
            EXPECT_EQ(entry.mSize, payload.size());

            const std::optional<EdenArchive::ProvenanceInfo> provenance = pack->archive().provenance(*index);
            ASSERT_TRUE(provenance.has_value());
            EXPECT_EQ(provenance->mToolId, clusterToolId);
            EXPECT_EQ(provenance->mToolId, "openeden.lod.objects");
            EXPECT_EQ(provenance->mSourceDigest, baked.mSourceDigest);
            EXPECT_EQ(std::vector<std::uint8_t>(provenance->mParameters.begin(), provenance->mParameters.end()),
                baked.mParameters);
        }
    }

    TEST(LodClusterPack, reading_a_missing_tile_is_an_error)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> bytes = buildPack(fixture);
        LodError error;
        const std::unique_ptr<ClusterPack> pack = openBytes(bytes, error);
        ASSERT_NE(pack, nullptr) << error.describe();

        ObjectCluster cluster;
        const LodError readError = pack->readCluster(*tileContainingCell(0, 1, 100, 100), cluster);
        EXPECT_FALSE(readError.ok());
        EXPECT_FALSE(readError.mField.empty());
    }

    TEST(LodClusterPack, a_garbage_cluster_entry_fails_to_read_and_the_others_still_read)
    {
        const Fixture fixture = makeFixture();
        std::vector<std::uint8_t> goodPayload;
        ASSERT_TRUE(writeObjectCluster(fixture.mClusters[0].mData, goodPayload).ok());
        const std::string manifestText = writeClusterManifestJson(fixture.mManifest);
        const std::vector<std::uint8_t> shortPayload(goodPayload.begin(), goodPayload.begin() + 100);
        const std::vector<std::uint8_t> bytes = rawPack(true, &manifestText,
            { { *packClusterKey(fixture.mClusters[0].mId), goodPayload },
                { *packClusterKey(fixture.mClusters[1].mId), toBytes("certainly not an object cluster") },
                { *packClusterKey(fixture.mClusters[2].mId), shortPayload } });

        LodError error;
        const std::unique_ptr<ClusterPack> pack = openBytes(bytes, error);
        if (pack == nullptr)
        {
            // Refusing the whole pack at open is also acceptable, with a located error.
            EXPECT_FALSE(error.ok());
            return;
        }
        ObjectCluster cluster;
        EXPECT_TRUE(pack->readCluster(fixture.mClusters[0].mId, cluster).ok());
        EXPECT_EQ(cluster, fixture.mClusters[0].mData);
        ObjectCluster bad;
        EXPECT_FALSE(pack->readCluster(fixture.mClusters[1].mId, bad).ok());
        EXPECT_FALSE(pack->readCluster(fixture.mClusters[2].mId, bad).ok());
    }

    // ---- opening bad packs ----------------------------------------------------------------------------------

    TEST(LodClusterPack, open_refuses_a_pack_without_the_derived_flag)
    {
        const std::vector<std::uint8_t> bytes = rawPack(false, &canonicalManifest, {});
        LodError error;
        EXPECT_EQ(openBytes(bytes, error), nullptr);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());

        // The same content with the flag is accepted.
        LodError accepted;
        EXPECT_NE(openBytes(rawPack(true, &canonicalManifest, {}), accepted), nullptr) << accepted.describe();
    }

    TEST(LodClusterPack, open_refuses_a_manifest_with_layout_version_1)
    {
        for (const char* version : { "1", "2", "4000000000" })
        {
            const std::string text
                = replaceOnce(canonicalManifest, "\"layoutVersion\":0", std::string("\"layoutVersion\":") + version);
            const std::vector<std::uint8_t> bytes = rawPack(true, &text, {});
            LodError error;
            EXPECT_EQ(openBytes(bytes, error), nullptr) << version;
            EXPECT_FALSE(error.ok()) << version;
            EXPECT_FALSE(error.mField.empty()) << version;
        }
    }

    TEST(LodClusterPack, open_refuses_a_pack_with_no_manifest_entry)
    {
        const std::vector<std::uint8_t> bytes = rawPack(true, nullptr, {});
        LodError error;
        EXPECT_EQ(openBytes(bytes, error), nullptr);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
    }

    TEST(LodClusterPack, open_refuses_a_malformed_manifest_and_bytes_that_are_not_a_pack)
    {
        for (const std::string& text : { std::string("not json"), std::string("{}"), canonicalManifest + "\n" })
        {
            const std::vector<std::uint8_t> bytes = rawPack(true, &text, {});
            LodError error;
            EXPECT_EQ(openBytes(bytes, error), nullptr) << text;
            EXPECT_FALSE(error.ok()) << text;
        }
        LodError error;
        EXPECT_EQ(openBytes(std::vector<std::uint8_t>(), error), nullptr);
        EXPECT_FALSE(error.ok());
    }

    TEST(LodClusterPack, open_refuses_a_missing_file)
    {
        LodError error;
        const std::filesystem::path missing
            = std::filesystem::temp_directory_path() / "openeden_lod_no_such_cluster_pack.edenpak";
        std::error_code ignored;
        std::filesystem::remove(missing, ignored);
        EXPECT_EQ(ClusterPack::open(missing, error), nullptr);
        EXPECT_FALSE(error.ok());
    }

    // ---- terrain packs and cluster packs are not interchangeable ------------------------------------------------

    TerrainTile makeTerrainTile()
    {
        TerrainTile tile;
        tile.mGridSize = 5;
        tile.mLevel = 1;
        tile.mSide = 16384.f;
        tile.mOriginX = 0;
        tile.mOriginY = 0;
        tile.mMinHeight = -12.5f;
        tile.mMaxHeight = 300.25f;
        tile.mGeometricError = 3.5f;
        tile.mHeights.assign(25, 1234);
        tile.mHasNormals = false;
        return tile;
    }

    TEST(LodClusterPack, a_terrain_tile_pack_is_refused_by_the_cluster_pack_and_the_reverse)
    {
        TileManifest terrainManifest;
        terrainManifest.mBakerVersion = 1;
        terrainManifest.mWorldspaces = { WorldspaceInfo{ 0, "Morrowind", 8192 } };
        terrainManifest.mLevels = { 1 };
        terrainManifest.mGridSize = 5;
        terrainManifest.mAlbedoSize = 0;
        BakedTile baked;
        baked.mId = *tileContainingCell(0, 1, 0, 0);
        baked.mData = makeTerrainTile();
        baked.mSourceDigest = filledHash(0x11);
        TilePackOptions options;
        options.mGeneration = 1;
        options.mStoreOnly = true;
        std::vector<std::uint8_t> terrainBytes;
        const LodError buildError = buildTilePack(terrainManifest, { baked }, options, terrainBytes);
        ASSERT_TRUE(buildError.ok()) << buildError.describe();

        LodError error;
        EXPECT_EQ(openBytes(terrainBytes, error), nullptr);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());

        const std::vector<std::uint8_t> clusterBytes = buildPack(makeFixture());
        LodError reverseError;
        EXPECT_EQ(TilePack::open(EdenArchive::makeMemorySource(clusterBytes), reverseError), nullptr);
        EXPECT_FALSE(reverseError.ok());
        EXPECT_FALSE(reverseError.mField.empty());
    }

    // ---- the content digest -----------------------------------------------------------------------------------

    TEST(LodClusterPack, the_content_digest_does_not_depend_on_the_order_of_the_clusters)
    {
        const Fixture fixture = makeFixture();
        const EdenArchive::Hash digest = clusterContentDigest(fixture.mManifest, fixture.mClusters);
        EXPECT_NE(digest, EdenArchive::Hash{});

        std::vector<BakedCluster> reversed = fixture.mClusters;
        std::reverse(reversed.begin(), reversed.end());
        EXPECT_EQ(clusterContentDigest(fixture.mManifest, reversed), digest);

        std::vector<BakedCluster> rotated = fixture.mClusters;
        std::rotate(rotated.begin(), rotated.begin() + 2, rotated.end());
        EXPECT_EQ(clusterContentDigest(fixture.mManifest, rotated), digest);
        EXPECT_EQ(clusterContentDigest(fixture.mManifest, fixture.mClusters), digest);
    }

    TEST(LodClusterPack, the_content_digest_changes_with_a_source_digest)
    {
        const Fixture fixture = makeFixture();
        const EdenArchive::Hash digest = clusterContentDigest(fixture.mManifest, fixture.mClusters);
        for (std::size_t i = 0; i < fixture.mClusters.size(); ++i)
        {
            std::vector<BakedCluster> changed = fixture.mClusters;
            changed[i].mSourceDigest[7] ^= 0x01;
            EXPECT_NE(clusterContentDigest(fixture.mManifest, changed), digest) << "cluster " << i;
        }
    }

    TEST(LodClusterPack, the_content_digest_changes_with_the_manifest_and_with_a_missing_cluster)
    {
        const Fixture fixture = makeFixture();
        const EdenArchive::Hash digest = clusterContentDigest(fixture.mManifest, fixture.mClusters);

        ClusterManifest changed = fixture.mManifest;
        changed.mBakerVersion += 1;
        EXPECT_NE(clusterContentDigest(changed, fixture.mClusters), digest);

        changed = fixture.mManifest;
        changed.mErrors[0] += 1;
        EXPECT_NE(clusterContentDigest(changed, fixture.mClusters), digest);

        std::vector<BakedCluster> fewer = fixture.mClusters;
        fewer.pop_back();
        EXPECT_NE(clusterContentDigest(fixture.mManifest, fewer), digest);
    }

    // ---- files ------------------------------------------------------------------------------------------------

    TEST(LodClusterPack, a_pack_written_to_a_file_is_the_same_bytes_and_opens_by_path)
    {
        const Fixture fixture = makeFixture();
        const std::vector<std::uint8_t> expected = buildPack(fixture);

        const std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openeden_lod_clusterpack_test_"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".edenpak");
        std::error_code ignored;
        std::filesystem::remove(path, ignored);

        const LodError writeError = writeClusterPack(fixture.mManifest, fixture.mClusters, fixture.mOptions, path);
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
            const std::unique_ptr<ClusterPack> pack = ClusterPack::open(path, error);
            ASSERT_NE(pack, nullptr) << error.describe();
            EXPECT_EQ(pack->generation(), 9u);
            EXPECT_EQ(pack->tileIds().size(), fixture.mClusters.size());
            ObjectCluster cluster;
            ASSERT_TRUE(pack->readCluster(fixture.mClusters[1].mId, cluster).ok());
            EXPECT_EQ(cluster, fixture.mClusters[1].mData);
        }
        std::filesystem::remove(path, ignored);
    }
}
