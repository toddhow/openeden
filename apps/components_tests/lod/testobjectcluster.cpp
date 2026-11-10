// Task 0059 (stage S4b): the payload of one baked object cluster (DRAFT layout, version 0). Pure code; no GPU, window
// or game data. The expected bytes are written out by hand from the spec table, independently of the writer.
#include <gtest/gtest.h>

#include <components/lod/objectcluster.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace Lod;

    // Header field offsets (task 0059, "Draft cluster entry layout").
    constexpr std::size_t offMagic = 0;
    constexpr std::size_t offVersion = 4;
    constexpr std::size_t offFlags = 6;
    constexpr std::size_t offLevel = 8;
    constexpr std::size_t offSide = 12;
    constexpr std::size_t offOriginX = 16;
    constexpr std::size_t offOriginY = 20;
    constexpr std::size_t offMinZ = 24;
    constexpr std::size_t offMaxZ = 28;
    constexpr std::size_t offError = 32;
    constexpr std::size_t offGroupCount = 36;
    constexpr std::size_t offRangeCount = 40;
    constexpr std::size_t offCellCount = 44;
    constexpr std::size_t offRefCount = 48;
    constexpr std::size_t offVertexCount = 52;
    constexpr std::size_t offIndexCount = 56;
    constexpr std::size_t offStringBytes = 60;
    constexpr std::size_t offTableOffsets = 64; // seven u32: groups, ranges, cells, refs, vertices, indices, strings
    constexpr std::size_t offSourceTriangles = 92;
    constexpr std::size_t offOverhang = 96;
    constexpr std::size_t offReserved = 100;

    // Canonical table offsets of the hand-built cluster below (every table 8-aligned).
    constexpr std::size_t tabGroups = 104; // 2 x 32
    constexpr std::size_t tabRanges = 168; // 3 x 16
    constexpr std::size_t tabCells = 216; // 2 x 16
    constexpr std::size_t tabRefs = 248; // 3 x 8
    constexpr std::size_t tabVertices = 272; // 6 x 18 = 108, padded to 112
    constexpr std::size_t tabIndices = 384; // 9 x 4 = 36, padded to 40
    constexpr std::size_t tabStrings = 424; // 31 bytes, padded to 32
    constexpr std::size_t totalSize = 456;
    constexpr std::size_t vertexStride = 18;

    constexpr std::size_t noOffset = std::numeric_limits<std::size_t>::max();
    constexpr float nanValue = std::numeric_limits<float>::quiet_NaN();
    constexpr float infValue = std::numeric_limits<float>::infinity();

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

        std::uint64_t below(std::uint64_t bound) { return next() % bound; }

    private:
        std::uint64_t mState;
    };

    void putU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value)
    {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    }

    void putU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
    }

    void putF32(std::vector<std::uint8_t>& bytes, std::size_t offset, float value)
    {
        putU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
    }

    std::uint32_t getU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i)
        {
            value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
        }
        return value;
    }

    std::vector<std::uint8_t> u32Bytes(std::uint32_t value)
    {
        std::vector<std::uint8_t> out(4);
        putU32(out, 0, value);
        return out;
    }

    std::vector<std::uint8_t> f32Bytes(float value)
    {
        return u32Bytes(std::bit_cast<std::uint32_t>(value));
    }

    struct VertexSpec
    {
        float mPos[3];
        std::int8_t mNormal[3];
        std::uint8_t mSlot;
        std::uint8_t mColor[4];
        float mUv[2];
    };

    // Positions are relative to the tile origin in x and y. All numbers are exactly representable.
    const VertexSpec vertexSpecs[6] = {
        { { 1000, 2000, 100 }, { 0, 0, 127 }, 0, { 255, 0, 0, 255 }, { 0, 0 } },
        { { 2000, 2000, 200 }, { -127, 64, 0 }, 0, { 255, 10, 0, 255 }, { 1, 0 } },
        { { 1500, 3000, 300 }, { 0, 0, 127 }, 0, { 255, 20, 0, 255 }, { 0, 1 } },
        { { 9000, 2000, 150 }, { 0, 127, 0 }, 1, { 255, 30, 0, 255 }, { 0.25f, 0.25f } },
        { { 10000, 2500, 500 }, { 0, 127, 0 }, 1, { 255, 40, 0, 255 }, { 0.5f, 0.5f } },
        { { 9500, 3500, 250 }, { 0, 127, 0 }, 1, { 255, 50, 0, 255 }, { 0.75f, 0.75f } },
    };

    const std::uint32_t handIndices[9] = { 0, 1, 2, 3, 4, 5, 3, 5, 4 };

    ObjectCluster makeCluster()
    {
        ObjectCluster cluster;
        cluster.mLevel = 1;
        cluster.mSide = 16384;
        cluster.mOriginX = -8192;
        cluster.mOriginY = 0;
        cluster.mMinZ = 100;
        cluster.mMaxZ = 500;
        cluster.mGeometricError = 64;
        cluster.mOverhang = 0;
        cluster.mSourceTriangles = 5;

        ClusterGroup first;
        first.mTexture = "textures/a.dds";
        first.mFlags = 0;
        first.mAlphaCutoff = 0;
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
        cluster.mCells = { ClusterCell{ -1, 0, 0, 2 }, ClusterCell{ 0, 0, 2, 1 } };
        cluster.mRefs = { ClusterRef{ 1, 0 }, ClusterRef{ 5, 0 }, ClusterRef{ 2, 1 } };

        for (const VertexSpec& spec : vertexSpecs)
        {
            ClusterVertex vertex;
            for (std::size_t k = 0; k < 3; ++k)
            {
                vertex.mPosition[k] = spec.mPos[k];
                vertex.mNormal[k] = spec.mNormal[k];
            }
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

    // The canonical bytes of makeCluster(), filled in by hand from the layout table.
    std::vector<std::uint8_t> makeExpectedBytes()
    {
        std::vector<std::uint8_t> b(totalSize, 0);
        b[offMagic + 0] = 'E';
        b[offMagic + 1] = 'T';
        b[offMagic + 2] = 'C';
        b[offMagic + 3] = 'L';
        putU16(b, offVersion, 1);
        putU16(b, offFlags, 0);
        putU32(b, offLevel, 1);
        putF32(b, offSide, 16384.0f);
        putF32(b, offOriginX, -8192.0f);
        putF32(b, offOriginY, 0.0f);
        putF32(b, offMinZ, 100.0f);
        putF32(b, offMaxZ, 500.0f);
        putF32(b, offError, 64.0f);
        putU32(b, offGroupCount, 2);
        putU32(b, offRangeCount, 3);
        putU32(b, offCellCount, 2);
        putU32(b, offRefCount, 3);
        putU32(b, offVertexCount, 6);
        putU32(b, offIndexCount, 9);
        putU32(b, offStringBytes, 31);
        putU32(b, offTableOffsets + 0, static_cast<std::uint32_t>(tabGroups));
        putU32(b, offTableOffsets + 4, static_cast<std::uint32_t>(tabRanges));
        putU32(b, offTableOffsets + 8, static_cast<std::uint32_t>(tabCells));
        putU32(b, offTableOffsets + 12, static_cast<std::uint32_t>(tabRefs));
        putU32(b, offTableOffsets + 16, static_cast<std::uint32_t>(tabVertices));
        putU32(b, offTableOffsets + 20, static_cast<std::uint32_t>(tabIndices));
        putU32(b, offTableOffsets + 24, static_cast<std::uint32_t>(tabStrings));
        putU32(b, offSourceTriangles, 5);
        putF32(b, offOverhang, 0.0f);
        // The position grid: the tile side 16384 does not fit 65533 steps of 0.25, so the step is 2^-1.
        putU32(b, offReserved, 0xFFFFFFFFu);

        // Groups: path offset, path length, flags, alpha cutoff, first index, index count, 8 reserved bytes.
        putU32(b, tabGroups + 0, 0);
        putU32(b, tabGroups + 4, 14);
        putU32(b, tabGroups + 8, 0);
        putF32(b, tabGroups + 12, 0.0f);
        putU32(b, tabGroups + 16, 0);
        putU32(b, tabGroups + 20, 6);
        putU32(b, tabGroups + 32 + 0, 14);
        putU32(b, tabGroups + 32 + 4, 17);
        putU32(b, tabGroups + 32 + 8, 1);
        putF32(b, tabGroups + 32 + 12, 0.5f);
        putU32(b, tabGroups + 32 + 16, 6);
        putU32(b, tabGroups + 32 + 20, 3);

        // Ranges: group, cell slot, first index, index count.
        const std::uint32_t ranges[3][4] = { { 0, 0, 0, 3 }, { 0, 1, 3, 3 }, { 1, 1, 6, 3 } };
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t k = 0; k < 4; ++k)
            {
                putU32(b, tabRanges + 16 * i + 4 * k, ranges[i][k]);
            }
        }

        // Cells: x, y (i32 as bit patterns), first reference, reference count.
        putU32(b, tabCells + 0, 0xFFFFFFFFu);
        putU32(b, tabCells + 4, 0);
        putU32(b, tabCells + 8, 0);
        putU32(b, tabCells + 12, 2);
        putU32(b, tabCells + 16 + 0, 0);
        putU32(b, tabCells + 16 + 4, 0);
        putU32(b, tabCells + 16 + 8, 2);
        putU32(b, tabCells + 16 + 12, 1);

        // References: index, then content file.
        const std::uint32_t refs[3][2] = { { 1, 0 }, { 5, 0 }, { 2, 1 } };
        for (std::size_t i = 0; i < 3; ++i)
        {
            putU32(b, tabRefs + 8 * i, refs[i][0]);
            putU32(b, tabRefs + 8 * i + 4, refs[i][1]);
        }

        // Vertices (18 bytes): position as three u16 on the grid (step 0.5; x and y from 0, z from minZ = 100), normal
        // snorm8 x 3, cell slot, colour x 4, uv as two half floats.
        const auto half = [](float value) -> std::uint16_t {
            if (value == 0.f)
                return 0x0000;
            if (value == 1.f)
                return 0x3C00;
            if (value == 0.25f)
                return 0x3400;
            if (value == 0.5f)
                return 0x3800;
            return 0x3A00; // 0.75
        };
        for (std::size_t i = 0; i < 6; ++i)
        {
            const VertexSpec& spec = vertexSpecs[i];
            const std::size_t at = tabVertices + vertexStride * i;
            putU16(b, at + 0, static_cast<std::uint16_t>(spec.mPos[0] / 0.5f));
            putU16(b, at + 2, static_cast<std::uint16_t>(spec.mPos[1] / 0.5f));
            putU16(b, at + 4, static_cast<std::uint16_t>((spec.mPos[2] - 100.f) / 0.5f));
            b[at + 6] = static_cast<std::uint8_t>(spec.mNormal[0]);
            b[at + 7] = static_cast<std::uint8_t>(spec.mNormal[1]);
            b[at + 8] = static_cast<std::uint8_t>(spec.mNormal[2]);
            b[at + 9] = spec.mSlot;
            b[at + 10] = spec.mColor[0];
            b[at + 11] = spec.mColor[1];
            b[at + 12] = spec.mColor[2];
            b[at + 13] = spec.mColor[3];
            putU16(b, at + 14, half(spec.mUv[0]));
            putU16(b, at + 16, half(spec.mUv[1]));
        }

        for (std::size_t i = 0; i < 9; ++i)
        {
            putU32(b, tabIndices + 4 * i, handIndices[i]);
        }

        const std::string strings = "textures/a.ddstextures/tree.dds";
        for (std::size_t i = 0; i < strings.size(); ++i)
        {
            b[tabStrings + i] = static_cast<std::uint8_t>(strings[i]);
        }
        return b;
    }

    std::vector<std::uint8_t> makeBaseBytes()
    {
        std::vector<std::uint8_t> out;
        const LodError error = writeObjectCluster(makeCluster(), out);
        EXPECT_TRUE(error.ok()) << error.describe();
        return out;
    }

    void expectRefused(const std::vector<std::uint8_t>& bytes, const std::string& what, std::size_t offset = noOffset)
    {
        ObjectCluster out;
        const LodError error = readObjectCluster(bytes, out);
        EXPECT_FALSE(error.mField.empty()) << what;
        if (offset != noOffset)
        {
            EXPECT_EQ(error.mOffset, offset) << what << ": " << error.describe();
        }
    }

    void applyPatch(std::vector<std::uint8_t>& bytes, std::size_t offset, const std::vector<std::uint8_t>& value)
    {
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            bytes[offset + i] = value[i];
        }
    }

    // ---- keys ----

    constexpr std::uint64_t lowBits = (std::uint64_t{ 1 } << 56) - 1;

    TEST(LodObjectCluster, KeyHasClusterTagAndTerrainLowBits)
    {
        const std::int32_t cells[][2] = { { 0, 0 }, { -1, 0 }, { 17, -5 }, { -30, 22 }, { 100, 100 } };
        for (const std::uint32_t level : { 1u, 2u, 3u })
        {
            for (const auto& cell : cells)
            {
                const auto id = tileContainingCell(3, level, cell[0], cell[1]);
                ASSERT_TRUE(id.has_value());
                const auto cluster = packClusterKey(*id);
                const auto terrain = packTileKey(*id);
                ASSERT_TRUE(cluster.has_value());
                ASSERT_TRUE(terrain.has_value());
                EXPECT_EQ(*cluster >> 56, 0x11u);
                EXPECT_EQ(*terrain >> 56, 0x10u);
                EXPECT_EQ(*cluster & lowBits, *terrain & lowBits);
                EXPECT_EQ(*cluster >> 56, static_cast<std::uint64_t>(tagObjectCluster));
            }
        }
    }

    TEST(LodObjectCluster, KeyRoundTrips)
    {
        const std::int32_t cells[][2] = { { 0, 0 }, { -1, 0 }, { 17, -5 }, { -30, 22 } };
        for (const std::uint32_t level : { 1u, 2u, 3u })
        {
            for (const auto& cell : cells)
            {
                const auto id = tileContainingCell(7, level, cell[0], cell[1]);
                ASSERT_TRUE(id.has_value());
                const auto key = packClusterKey(*id);
                ASSERT_TRUE(key.has_value());
                const auto back = unpackClusterKey(*key);
                ASSERT_TRUE(back.has_value());
                EXPECT_EQ(*back, *id);
            }
        }
    }

    TEST(LodObjectCluster, KeyUnpackRefusesOtherTags)
    {
        const auto id = tileContainingCell(0, 2, 5, 6);
        ASSERT_TRUE(id.has_value());
        const auto terrain = packTileKey(*id);
        ASSERT_TRUE(terrain.has_value());
        EXPECT_FALSE(unpackClusterKey(*terrain).has_value()); // tag 0x10
        EXPECT_FALSE(unpackClusterKey(0).has_value());
        EXPECT_FALSE(unpackClusterKey((*terrain & lowBits) | (std::uint64_t{ 0x12 } << 56)).has_value());
        EXPECT_FALSE(unpackClusterKey(*terrain & lowBits).has_value()); // tag 0
        EXPECT_FALSE(unpackClusterKey(~std::uint64_t{ 0 }).has_value());
        // The same low bits with the cluster tag unpack to the same tile.
        const auto viaTag = unpackClusterKey((*terrain & lowBits) | (std::uint64_t{ tagObjectCluster } << 56));
        ASSERT_TRUE(viaTag.has_value());
        EXPECT_EQ(*viaTag, *id);
    }

    TEST(LodObjectCluster, KeyRefusesInvalidIds)
    {
        TileId badLevel;
        badLevel.mWorldspace = 0;
        badLevel.mLevel = 16;
        EXPECT_FALSE(packClusterKey(badLevel).has_value());

        TileId badCoordinate;
        badCoordinate.mWorldspace = 0;
        badCoordinate.mLevel = 1;
        badCoordinate.mX = 1u << 21; // 2^(22 - 1) is one too many
        badCoordinate.mY = 0;
        EXPECT_FALSE(packClusterKey(badCoordinate).has_value());

        // Tag 0x11, level 1, x = 2^22 - 1 (too large for level 1).
        const std::uint64_t tagBits = std::uint64_t{ 0x11 } << 56;
        const std::uint64_t allCoordinates = (std::uint64_t{ 1 } << 44) - 1;
        const std::uint64_t key = tagBits | (std::uint64_t{ 1 } << 44) | allCoordinates;
        EXPECT_FALSE(unpackClusterKey(key).has_value());
        // Tag 0x11 with level 15 and a coordinate above 2^7.
        const std::uint64_t key15 = tagBits | (std::uint64_t{ 15 } << 44) | allCoordinates;
        EXPECT_FALSE(unpackClusterKey(key15).has_value());
    }

    // ---- bytes ----

    TEST(LodObjectCluster, HandBuiltClusterValidates)
    {
        const LodError error = validateObjectCluster(makeCluster());
        EXPECT_TRUE(error.ok()) << error.describe();
    }

    TEST(LodObjectCluster, WriterMatchesHandBuiltBytes)
    {
        const std::vector<std::uint8_t> expected = makeExpectedBytes();
        std::vector<std::uint8_t> bytes;
        const LodError error = writeObjectCluster(makeCluster(), bytes);
        ASSERT_TRUE(error.ok()) << error.describe();
        ASSERT_EQ(bytes.size(), expected.size());
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            ASSERT_EQ(bytes[i], expected[i]) << "byte " << i;
        }
    }

    TEST(LodObjectCluster, ReaderParsesHandBuiltBytes)
    {
        ObjectCluster cluster;
        const LodError error = readObjectCluster(makeExpectedBytes(), cluster);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_EQ(cluster, makeCluster());
    }

    TEST(LodObjectCluster, RoundTripKeepsEveryField)
    {
        const ObjectCluster original = makeCluster();
        std::vector<std::uint8_t> bytes;
        ASSERT_TRUE(writeObjectCluster(original, bytes).ok());
        ObjectCluster back;
        const LodError error = readObjectCluster(bytes, back);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_EQ(back, original);
        EXPECT_EQ(back.mGroups[1].mTexture, "textures/tree.dds");
        EXPECT_EQ(back.mCells[0].mCellX, -1);
        EXPECT_EQ(back.mVertices[1].mNormal[0], -127);
    }

    TEST(LodObjectCluster, RewriteIsCanonical)
    {
        const std::vector<std::uint8_t> bytes = makeBaseBytes();
        ObjectCluster cluster;
        ASSERT_TRUE(readObjectCluster(bytes, cluster).ok());
        std::vector<std::uint8_t> again;
        ASSERT_TRUE(writeObjectCluster(cluster, again).ok());
        EXPECT_EQ(again, bytes);
    }

    TEST(LodObjectCluster, PaddingIsZeroAndSizeIsMultipleOfEight)
    {
        const std::vector<std::uint8_t> bytes = makeBaseBytes();
        EXPECT_EQ(bytes.size() % 8, 0u);
        EXPECT_EQ(bytes.size(), totalSize);
        // The header's reserved word, the group reserved bytes, and the pads after the indices and the strings.
        EXPECT_EQ(getU32(bytes, offReserved), 0xFFFFFFFFu);
        for (std::size_t i = tabGroups + 24; i < tabGroups + 32; ++i)
        {
            EXPECT_EQ(bytes[i], 0) << i;
        }
        for (std::size_t i = tabIndices + 36; i < tabStrings; ++i)
        {
            EXPECT_EQ(bytes[i], 0) << i;
        }
        for (std::size_t i = tabStrings + 31; i < bytes.size(); ++i)
        {
            EXPECT_EQ(bytes[i], 0) << i;
        }
    }

    TEST(LodObjectCluster, ReadAcceptsMatchingExpectedTile)
    {
        const auto tile = tileContainingCell(0, 1, -1, 0);
        ASSERT_TRUE(tile.has_value());
        ObjectCluster cluster;
        const LodError error = readObjectCluster(makeBaseBytes(), cluster, &*tile);
        EXPECT_TRUE(error.ok()) << error.describe();
    }

    TEST(LodObjectCluster, ReadRefusesExpectedTileOfAnotherLevel)
    {
        for (const std::uint32_t level : { 2u, 3u })
        {
            const auto tile = tileContainingCell(0, level, -1, 0);
            ASSERT_TRUE(tile.has_value());
            ObjectCluster cluster;
            const LodError error = readObjectCluster(makeBaseBytes(), cluster, &*tile);
            EXPECT_FALSE(error.mField.empty());
            EXPECT_EQ(error.mOffset, offLevel);
        }
    }

    // ---- refusals in the header ----

    TEST(LodObjectCluster, RefusesBadMagic)
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            std::vector<std::uint8_t> bytes = makeBaseBytes();
            bytes[offMagic + i] ^= 0x20;
            expectRefused(bytes, "magic byte " + std::to_string(i), offMagic);
        }
    }

    TEST(LodObjectCluster, RefusesOtherVersions)
    {
        for (const std::uint16_t version : { std::uint16_t{ 0 }, std::uint16_t{ 3 }, std::uint16_t{ 0xFFFF } })
        {
            std::vector<std::uint8_t> bytes = makeBaseBytes();
            putU16(bytes, offVersion, version);
            expectRefused(bytes, "version " + std::to_string(version), offVersion);
        }
    }

    TEST(LodObjectCluster, RefusesAnyFlagBit)
    {
        for (std::size_t bit = 0; bit < 16; ++bit)
        {
            std::vector<std::uint8_t> bytes = makeBaseBytes();
            putU16(bytes, offFlags, static_cast<std::uint16_t>(1u << bit));
            expectRefused(bytes, "flag bit " + std::to_string(bit), offFlags);
        }
    }

    TEST(LodObjectCluster, RefusesLevelsOutsideOneToFour)
    {
        for (const std::uint32_t level : { 0u, 5u, 15u, 0xFFFFFFFFu })
        {
            std::vector<std::uint8_t> bytes = makeBaseBytes();
            putU32(bytes, offLevel, level);
            expectRefused(bytes, "level " + std::to_string(level), offLevel);
        }
    }

    TEST(LodObjectCluster, RefusesNonFiniteHeaderNumbers)
    {
        const std::size_t fields[] = { offSide, offOriginX, offOriginY, offMinZ, offMaxZ, offError, offOverhang };
        for (const std::size_t field : fields)
        {
            for (const float bad : { nanValue, infValue, -infValue })
            {
                std::vector<std::uint8_t> bytes = makeBaseBytes();
                putF32(bytes, field, bad);
                expectRefused(bytes, "header float at " + std::to_string(field), field);
            }
        }
    }

    TEST(LodObjectCluster, RefusesNonZeroReservedWord)
    {
        std::vector<std::uint8_t> bytes = makeBaseBytes();
        putU32(bytes, offReserved, 1);
        expectRefused(bytes, "reserved", offReserved);
        bytes = makeBaseBytes();
        bytes[offReserved + 3] = 0x80;
        expectRefused(bytes, "reserved high byte", offReserved);
    }

    TEST(LodObjectCluster, RefusesCountsAboveTheCaps)
    {
        struct Cap
        {
            const char* mName;
            std::size_t mOffset;
            std::uint32_t mLimit;
        };
        const Cap caps[] = {
            { "groups", offGroupCount, maxClusterGroups },
            { "ranges", offRangeCount, maxClusterRanges },
            { "cells", offCellCount, maxClusterCells },
            { "refs", offRefCount, maxClusterRefs },
            { "vertices", offVertexCount, maxClusterVertices },
            { "indices", offIndexCount, maxClusterIndices },
        };
        for (const Cap& cap : caps)
        {
            for (const std::uint32_t value : { cap.mLimit + 1, cap.mLimit + 3, 0xFFFFFFFFu, 0x80000000u })
            {
                std::vector<std::uint8_t> bytes = makeBaseBytes();
                putU32(bytes, cap.mOffset, value);
                expectRefused(bytes, std::string(cap.mName) + " count " + std::to_string(value), cap.mOffset);
            }
        }
    }

    TEST(LodObjectCluster, RefusesCountsThatDisagreeWithTheTables)
    {
        const std::size_t counts[] = { offGroupCount, offRangeCount, offCellCount, offRefCount, offVertexCount,
            offIndexCount, offStringBytes };
        for (const std::size_t field : counts)
        {
            for (const std::int32_t delta : { -1, 1, 3 })
            {
                std::vector<std::uint8_t> bytes = makeBaseBytes();
                const auto current = static_cast<std::int32_t>(getU32(bytes, field));
                putU32(bytes, field, static_cast<std::uint32_t>(current + delta));
                expectRefused(bytes, "count at " + std::to_string(field) + " delta " + std::to_string(delta));
            }
        }
    }

    TEST(LodObjectCluster, RefusesZeroGroups)
    {
        std::vector<std::uint8_t> bytes = makeBaseBytes();
        putU32(bytes, offGroupCount, 0);
        expectRefused(bytes, "no groups");
    }

    TEST(LodObjectCluster, RefusesNonCanonicalOffsets)
    {
        for (std::size_t i = 0; i < 7; ++i)
        {
            const std::size_t field = offTableOffsets + 4 * i;
            const std::uint32_t original = getU32(makeBaseBytes(), field);
            for (const std::uint32_t value :
                { original + 8, original + 1, original - 8, std::uint32_t{ 0 }, 0xFFFFFFF8u, 0xFFFFFFFFu })
            {
                std::vector<std::uint8_t> bytes = makeBaseBytes();
                putU32(bytes, field, value);
                expectRefused(bytes, "table offset " + std::to_string(i) + " = " + std::to_string(value));
            }
        }
    }

    TEST(LodObjectCluster, RefusesOverlappingSpans)
    {
        // Every table pointed at the one before it.
        for (std::size_t i = 1; i < 7; ++i)
        {
            std::vector<std::uint8_t> bytes = makeBaseBytes();
            putU32(bytes, offTableOffsets + 4 * i, getU32(bytes, offTableOffsets + 4 * (i - 1)));
            expectRefused(bytes, "overlap " + std::to_string(i));
        }
        // The last table moved so that it leaves the entry.
        std::vector<std::uint8_t> bytes = makeBaseBytes();
        putU32(bytes, offTableOffsets + 24, static_cast<std::uint32_t>(totalSize));
        expectRefused(bytes, "strings after the end");
    }

    TEST(LodObjectCluster, RefusesTruncationAtEveryLength)
    {
        const std::vector<std::uint8_t> bytes = makeBaseBytes();
        for (std::size_t length = 0; length < bytes.size(); ++length)
        {
            ObjectCluster out;
            const LodError error = readObjectCluster(std::span<const std::uint8_t>(bytes.data(), length), out);
            EXPECT_FALSE(error.mField.empty()) << "length " << length;
        }
        ObjectCluster whole;
        EXPECT_TRUE(readObjectCluster(bytes, whole).ok());
    }

    TEST(LodObjectCluster, RefusesTrailingBytes)
    {
        for (const std::size_t extra : { std::size_t{ 1 }, std::size_t{ 7 }, std::size_t{ 8 }, std::size_t{ 64 } })
        {
            std::vector<std::uint8_t> bytes = makeBaseBytes();
            bytes.insert(bytes.end(), extra, std::uint8_t{ 0 });
            expectRefused(bytes, "trailing zero bytes " + std::to_string(extra));
            bytes = makeBaseBytes();
            bytes.insert(bytes.end(), extra, std::uint8_t{ 0xAB });
            expectRefused(bytes, "trailing bytes " + std::to_string(extra));
        }
    }

    // ---- refusals in the tables, by patching the hand-built bytes in place ----

    struct Patch
    {
        std::string mName;
        std::size_t mOffset;
        std::vector<std::uint8_t> mValue;
    };

    TEST(LodObjectCluster, RefusesBrokenTables)
    {
        const std::size_t range1 = tabRanges + 16;
        const std::size_t range2 = tabRanges + 32;
        const std::size_t group1 = tabGroups + 32;
        const std::size_t cell1 = tabCells + 16;
        const std::size_t vertex0 = tabVertices;
        const std::size_t vertex3 = tabVertices + 3 * vertexStride;
        const std::vector<Patch> patches = {
            // Ranges.
            { "range 0 count too big", tabRanges + 12, u32Bytes(6) },
            { "range 0 count zero", tabRanges + 12, u32Bytes(0) },
            { "range 0 count not multiple of 3", tabRanges + 12, u32Bytes(4) },
            { "range 1 first index leaves a gap", range1 + 8, u32Bytes(4) },
            { "range 1 first index overlaps", range1 + 8, u32Bytes(2) },
            { "range 2 group repeated", range2 + 0, u32Bytes(0) },
            { "range 2 group out of range", range2 + 0, u32Bytes(2) },
            { "range 0 slot out of range", tabRanges + 4, u32Bytes(2) },
            { "range 1 slot out of range", range1 + 4, u32Bytes(255) },
            { "range 1 slot duplicates range 0", range1 + 4, u32Bytes(0) },
            // Groups.
            { "group 0 first index", tabGroups + 16, u32Bytes(1) },
            { "group 1 first index overlaps", group1 + 16, u32Bytes(3) },
            { "group 1 count short", group1 + 20, u32Bytes(0) },
            { "group 1 count long", group1 + 20, u32Bytes(6) },
            { "group 0 path offset beyond strings", tabGroups + 0, u32Bytes(100) },
            { "group 1 path overruns strings", group1 + 4, u32Bytes(18) },
            { "group 0 empty path", tabGroups + 4, u32Bytes(0) },
            { "group 1 path over 1024 bytes", group1 + 4, u32Bytes(1025) },
            { "group 1 path length huge", group1 + 4, u32Bytes(0xFFFFFFFFu) },
            { "group 0 unknown flag bit 2", tabGroups + 8, u32Bytes(4) },
            { "group 0 unknown flag bit 31", tabGroups + 8, u32Bytes(0x80000000u) },
            { "group 0 alpha cutoff without alpha test", tabGroups + 12, f32Bytes(0.5f) },
            { "group 1 alpha test off but cutoff set", group1 + 8, u32Bytes(0) },
            { "group 1 alpha cutoff NaN", group1 + 12, f32Bytes(nanValue) },
            { "group 1 alpha cutoff inf", group1 + 12, f32Bytes(infValue) },
            { "group 0 reserved byte 24", tabGroups + 24, { 1 } },
            { "group 0 reserved byte 31", tabGroups + 31, { 1 } },
            { "group 1 reserved byte 28", group1 + 28, { 0x80 } },
            // Groups out of order: paths swapped.
            { "groups unsorted (path 0)", tabGroups + 0, u32Bytes(14) },
            // Cells.
            { "cell 0 y raised so cells are unsorted", tabCells + 4, u32Bytes(2) },
            { "cell 1 repeats cell 0", cell1 + 0, u32Bytes(0xFFFFFFFFu) },
            { "cell 1 x smaller than cell 0 in the same row", cell1 + 0, u32Bytes(0xFFFFFFFEu) },
            { "cell 0 first reference", tabCells + 8, u32Bytes(1) },
            { "cell 0 reference count short", tabCells + 12, u32Bytes(1) },
            { "cell 0 reference count zero", tabCells + 12, u32Bytes(0) },
            { "cell 1 first reference leaves a gap", cell1 + 8, u32Bytes(3) },
            { "cell 1 reference count long", cell1 + 12, u32Bytes(2) },
            // References.
            { "references unsorted within a cell", tabRefs + 0, u32Bytes(9) },
            { "references duplicated within a cell", tabRefs + 8, u32Bytes(1) },
            // Vertices.
            { "vertex 0 slot equals cell count", vertex0 + 9, { 2 } },
            { "vertex 0 slot 255", vertex0 + 9, { 255 } },
            // Positions are grid numbers now: a number cannot be below the grid, only beyond the tile or the z range.
            { "vertex 0 x above side", vertex0 + 0, { 0x82, 0x80 } }, // 32898 * 0.5 = 16449
            { "vertex 0 y above side", vertex0 + 2, { 0x01, 0x80 } }, // 32769 * 0.5 = 16384.5
            { "vertex 3 z above max", vertex3 + 4, { 0x22, 0x03 } }, // 802 * 0.5 + 100 = 501
            { "vertex 3 u NaN", vertex3 + 14, { 0x00, 0x7E } },
            { "vertex 3 v inf", vertex3 + 16, { 0x00, 0xFC } },
            // Indices.
            { "index equal to vertex count", tabIndices + 0, u32Bytes(6) },
            { "index huge", tabIndices + 8, u32Bytes(0xFFFFFFFFu) },
            { "last index out of range", tabIndices + 32, u32Bytes(7) },
            // Strings and padding.
            { "path byte 0xFF", tabStrings + 0, { 0xFF } },
            { "path overlong encoding", tabStrings + 0, { 0xC0, 0x80 } },
            { "path truncated multibyte", tabStrings + 13, { 0xE2 } },
            { "path lone continuation byte", tabStrings + 20, { 0x80 } },
            { "index padding byte 0", tabIndices + 36, { 1 } },
            { "index padding byte 3", tabIndices + 39, { 0x80 } },
            { "string padding byte", tabStrings + 31, { 1 } },
        };
        for (const Patch& patch : patches)
        {
            std::vector<std::uint8_t> bytes = makeBaseBytes();
            ASSERT_LE(patch.mOffset + patch.mValue.size(), bytes.size()) << patch.mName;
            applyPatch(bytes, patch.mOffset, patch.mValue);
            expectRefused(bytes, patch.mName);
        }
    }

    TEST(LodObjectCluster, RefusesPathsOverTheLimitEvenWhenTheTableIsConsistent)
    {
        ObjectCluster cluster = makeCluster();
        cluster.mGroups[0].mTexture = std::string(maxClusterTexturePath + 1, 'a');
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(writeObjectCluster(cluster, bytes).mField.empty());
        EXPECT_FALSE(validateObjectCluster(cluster).mField.empty());

        // The limit itself is fine.
        cluster.mGroups[0].mTexture = std::string(maxClusterTexturePath, 'a');
        cluster.mGroups[1].mTexture = std::string(maxClusterTexturePath, 'b');
        EXPECT_TRUE(validateObjectCluster(cluster).ok());
        ASSERT_TRUE(writeObjectCluster(cluster, bytes).ok());
        ObjectCluster back;
        EXPECT_TRUE(readObjectCluster(bytes, back).ok());
        EXPECT_EQ(back, cluster);
    }

    // ---- the writer and validateObjectCluster ----

    TEST(LodObjectCluster, WriterRefusesAClusterWithNoGroups)
    {
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(writeObjectCluster(ObjectCluster{}, bytes).mField.empty());

        ObjectCluster cluster = makeCluster();
        cluster.mGroups.clear();
        cluster.mRanges.clear();
        cluster.mIndices.clear();
        EXPECT_FALSE(writeObjectCluster(cluster, bytes).mField.empty());
    }

    TEST(LodObjectCluster, WriterAndValidatorRefuseLogicallyInvalidClusters)
    {
        struct Case
        {
            const char* mName;
            std::function<void(ObjectCluster&)> mMutate;
        };
        const Case cases[] = {
            { "level 0", [](ObjectCluster& c) { c.mLevel = 0; } },
            { "level 5", [](ObjectCluster& c) { c.mLevel = 5; } },
            { "side NaN", [](ObjectCluster& c) { c.mSide = nanValue; } },
            { "origin inf", [](ObjectCluster& c) { c.mOriginX = infValue; } },
            { "origin y NaN", [](ObjectCluster& c) { c.mOriginY = nanValue; } },
            { "max z NaN", [](ObjectCluster& c) { c.mMaxZ = nanValue; } },
            { "min z inf", [](ObjectCluster& c) { c.mMinZ = -infValue; } },
            { "error inf", [](ObjectCluster& c) { c.mGeometricError = infValue; } },
            { "overhang NaN", [](ObjectCluster& c) { c.mOverhang = nanValue; } },
            { "vertex NaN", [](ObjectCluster& c) { c.mVertices[2].mPosition[1] = nanValue; } },
            { "uv inf", [](ObjectCluster& c) { c.mVertices[4].mUv[0] = infValue; } },
            { "min z above vertices", [](ObjectCluster& c) { c.mMinZ = 150; } },
            { "max z below vertices", [](ObjectCluster& c) { c.mMaxZ = 450; } },
            { "vertex x outside", [](ObjectCluster& c) { c.mVertices[0].mPosition[0] = -2; } },
            { "vertex x beyond side", [](ObjectCluster& c) { c.mVertices[5].mPosition[0] = 16385; } },
            { "vertex y outside", [](ObjectCluster& c) { c.mVertices[0].mPosition[1] = -2; } },
            { "vertex y beyond side", [](ObjectCluster& c) { c.mVertices[5].mPosition[1] = 16400; } },
            { "vertex slot", [](ObjectCluster& c) { c.mVertices[3].mCellSlot = 2; } },
            { "index out of range", [](ObjectCluster& c) { c.mIndices[4] = 6; } },
            { "index count not a multiple of 3", [](ObjectCluster& c) { c.mIndices.push_back(0); } },
            { "index count short", [](ObjectCluster& c) { c.mIndices.pop_back(); } },
            { "empty path", [](ObjectCluster& c) { c.mGroups[0].mTexture.clear(); } },
            { "path over the limit", [](ObjectCluster& c) { c.mGroups[1].mTexture.assign(1025, 'z'); } },
            { "path not UTF-8", [](ObjectCluster& c) { c.mGroups[0].mTexture = "textures/\xFF.dds"; } },
            { "unknown flag", [](ObjectCluster& c) { c.mGroups[0].mFlags = 4; } },
            { "cutoff without alpha test", [](ObjectCluster& c) { c.mGroups[0].mAlphaCutoff = 0.25f; } },
            { "cutoff NaN", [](ObjectCluster& c) { c.mGroups[1].mAlphaCutoff = nanValue; } },
            { "groups unsorted", [](ObjectCluster& c) { std::swap(c.mGroups[0].mTexture, c.mGroups[1].mTexture); } },
            { "group spans do not tile", [](ObjectCluster& c) { c.mGroups[1].mFirstIndex = 5; } },
            { "ranges do not tile", [](ObjectCluster& c) { c.mRanges[0].mIndexCount = 6; } },
            { "range with no triangles", [](ObjectCluster& c) { c.mRanges[0].mIndexCount = 0; } },
            { "ranges unsorted", [](ObjectCluster& c) { std::swap(c.mRanges[0], c.mRanges[1]); } },
            { "range slot", [](ObjectCluster& c) { c.mRanges[2].mCellSlot = 7; } },
            { "range group", [](ObjectCluster& c) { c.mRanges[2].mGroup = 9; } },
            { "cells unsorted", [](ObjectCluster& c) { std::swap(c.mCells[0], c.mCells[1]); } },
            { "cell repeated", [](ObjectCluster& c) { c.mCells[1].mCellX = -1; } },
            { "cell ref span", [](ObjectCluster& c) { c.mCells[1].mFirstRef = 3; } },
            { "cell ref count", [](ObjectCluster& c) { c.mCells[0].mRefCount = 1; } },
            { "cell without references",
                [](ObjectCluster& c) {
                    c.mCells.push_back(ClusterCell{ 5, 5, 3, 0 });
                } },
            { "refs unsorted", [](ObjectCluster& c) { std::swap(c.mRefs[0], c.mRefs[1]); } },
            { "refs duplicated", [](ObjectCluster& c) { c.mRefs[1] = c.mRefs[0]; } },
            { "too many cells",
                [](ObjectCluster& c) {
                    for (std::int32_t i = 1; i <= 256; ++i)
                    {
                        c.mCells.push_back(ClusterCell{ i, 7, 3, 0 });
                    }
                } },
        };
        for (const Case& item : cases)
        {
            ObjectCluster cluster = makeCluster();
            item.mMutate(cluster);
            const LodError validated = validateObjectCluster(cluster);
            EXPECT_FALSE(validated.mField.empty()) << item.mName;
            std::vector<std::uint8_t> bytes;
            const LodError written = writeObjectCluster(cluster, bytes);
            EXPECT_FALSE(written.mField.empty()) << item.mName;
            EXPECT_EQ(written.mField, validated.mField) << item.mName;
        }
    }

    // Layout version 1 (toddhow approved the draft as the frozen layout, 2026-11-03; task 0066 stage d): the same bytes
    // with the version field 1, level 4 allowed and 256 cell slots (a slot is one byte).
    ObjectCluster makeClusterWithCells(std::uint32_t level, std::uint32_t cellCount)
    {
        ObjectCluster cluster = makeCluster();
        cluster.mLevel = level;
        cluster.mCells.clear();
        cluster.mRefs.clear();
        for (std::uint32_t i = 0; i < cellCount; ++i)
        {
            cluster.mCells.push_back(
                ClusterCell{ static_cast<std::int32_t>(i % 16), static_cast<std::int32_t>(i / 16), i, 1 });
            cluster.mRefs.push_back(ClusterRef{ i + 1, 0 });
        }
        return cluster;
    }

    TEST(LodObjectCluster, LayoutVersionIsOne)
    {
        EXPECT_EQ(objectClusterPayloadVersion, 1);
        std::vector<std::uint8_t> bytes;
        ASSERT_TRUE(writeObjectCluster(makeCluster(), bytes).ok());
        EXPECT_EQ(bytes[offVersion], 1u);
        EXPECT_EQ(bytes[offVersion + 1], 0u);
    }

    TEST(LodObjectCluster, LevelFourWithTwoHundredFiftySixCellsRoundTrips)
    {
        EXPECT_EQ(maxClusterLevel, 4u);
        EXPECT_EQ(maxClusterCells, 256u);
        const ObjectCluster cluster = makeClusterWithCells(4, 256);
        ASSERT_TRUE(validateObjectCluster(cluster).ok()) << validateObjectCluster(cluster).describe();
        std::vector<std::uint8_t> bytes;
        ASSERT_TRUE(writeObjectCluster(cluster, bytes).ok());
        ObjectCluster read;
        const LodError error = readObjectCluster(bytes, read);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_EQ(read, cluster);
        std::vector<std::uint8_t> again;
        ASSERT_TRUE(writeObjectCluster(read, again).ok());
        EXPECT_EQ(again, bytes);
        ClusterHeaderInfo info;
        EXPECT_TRUE(readObjectClusterHeader(bytes, info).ok());
        EXPECT_EQ(info.mLevel, 4u);
    }

    TEST(LodObjectCluster, TwoHundredFiftySevenCellsAreRefused)
    {
        const ObjectCluster cluster = makeClusterWithCells(4, 257);
        const LodError error = validateObjectCluster(cluster);
        EXPECT_FALSE(error.ok());
        EXPECT_EQ(error.mField, "cellCount");
    }

    TEST(LodObjectCluster, LevelFiveIsRefusedByTheWriterAndTheReader)
    {
        ObjectCluster cluster = makeCluster();
        cluster.mLevel = 5;
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(writeObjectCluster(cluster, bytes).ok());
        std::vector<std::uint8_t> good = makeBaseBytes();
        putU32(good, offLevel, 5);
        expectRefused(good, "level 5", offLevel);
    }

    TEST(LodObjectCluster, OverhangAllowsVerticesOutsideTheTileSquare)
    {
        ObjectCluster cluster = makeCluster();
        cluster.mVertices[0].mPosition[0] = -3;
        cluster.mOverhang = 3;
        EXPECT_TRUE(validateObjectCluster(cluster).ok());
        std::vector<std::uint8_t> bytes;
        ASSERT_TRUE(writeObjectCluster(cluster, bytes).ok());
        ObjectCluster back;
        ASSERT_TRUE(readObjectCluster(bytes, back).ok());
        EXPECT_EQ(back, cluster);

        // The same vertex without the overhang is refused.
        cluster.mOverhang = 0;
        EXPECT_FALSE(validateObjectCluster(cluster).mField.empty());
    }

    // ---- mutation ----

    TEST(LodObjectCluster, MutatedPayloadsNeverCrashAndAcceptedOnesAreCanonical)
    {
        const std::vector<std::uint8_t> base = makeBaseBytes();
        const std::uint32_t specials[] = { 0, 1, 2, 3, 64, 65, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 4096, 4097 };
        Rng rng(0x0059C1A5'7E5ull);
        std::size_t accepted = 0;
        std::size_t refused = 0;
        for (std::size_t iteration = 0; iteration < 20000; ++iteration)
        {
            std::vector<std::uint8_t> mutated = base;
            switch (rng.below(5))
            {
                case 0:
                    mutated[rng.below(mutated.size())] = static_cast<std::uint8_t>(rng.below(256));
                    break;
                case 1:
                {
                    const std::uint64_t flips = 2 + rng.below(3);
                    for (std::uint64_t i = 0; i < flips; ++i)
                    {
                        mutated[rng.below(mutated.size())] ^= static_cast<std::uint8_t>(1u << rng.below(8));
                    }
                    break;
                }
                case 2:
                    mutated.resize(rng.below(mutated.size()));
                    break;
                case 3:
                {
                    const std::uint64_t extra = 1 + rng.below(16);
                    for (std::uint64_t i = 0; i < extra; ++i)
                    {
                        mutated.push_back(static_cast<std::uint8_t>(rng.below(256)));
                    }
                    break;
                }
                default:
                {
                    const std::size_t at = static_cast<std::size_t>(rng.below(mutated.size() - 3));
                    putU32(mutated, at, specials[rng.below(std::size(specials))]);
                    break;
                }
            }

            ObjectCluster out;
            const LodError error = readObjectCluster(mutated, out);
            if (!error.ok())
            {
                ++refused;
                EXPECT_FALSE(error.mField.empty());
                continue;
            }
            ++accepted;
            std::vector<std::uint8_t> again;
            const LodError rewritten = writeObjectCluster(out, again);
            ASSERT_TRUE(rewritten.ok()) << "iteration " << iteration << ": " << rewritten.describe();
            ASSERT_EQ(again, mutated) << "iteration " << iteration;
        }
        EXPECT_GT(refused, 0u);
        EXPECT_GT(accepted + refused, 0u);
    }
}
