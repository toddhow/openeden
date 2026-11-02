// Task 0055 (stage S1b): the payload of one baked terrain tile. Pure code; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/terraintile.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <vector>

namespace
{
    using namespace Lod;

    // Header field offsets (task 0055, "The tile payload").
    constexpr std::size_t offMagic = 0;
    constexpr std::size_t offVersion = 4;
    constexpr std::size_t offFlags = 6;
    constexpr std::size_t offGrid = 8;
    constexpr std::size_t offMinHeight = 28;
    constexpr std::size_t offMaxHeight = 32;
    constexpr std::size_t offError = 36;
    constexpr std::size_t offAlbedoWidth = 40;
    constexpr std::size_t offAlbedoHeight = 42;
    constexpr std::size_t offAlbedoMips = 48;
    constexpr std::size_t offHeights = 52;
    constexpr std::size_t offNormals = 56;
    constexpr std::size_t offAlbedo = 60;
    constexpr std::size_t offReserved = 64;

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

    void setU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value)
    {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    }

    void setU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            bytes[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
        }
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

    std::uint32_t floatBits(float value)
    {
        return std::bit_cast<std::uint32_t>(value);
    }

    // A deterministic tile. albedoSide 0 means no albedo; otherwise a square RGBA8 picture with `mips` levels.
    TerrainTile makeTile(
        std::uint32_t grid, bool normals, std::uint32_t albedoSide, std::uint32_t mips, std::uint32_t seed = 1)
    {
        TerrainTile tile;
        tile.mGridSize = grid;
        tile.mLevel = 1;
        tile.mSide = 16384.f;
        tile.mOriginX = -16384.f;
        tile.mOriginY = 32768.f;
        tile.mMinHeight = -12.5f;
        tile.mMaxHeight = 300.25f;
        tile.mGeometricError = 3.5f;
        Rng rng(0xC0FFEEull + seed);
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

    std::vector<std::uint8_t> payloadOf(const TerrainTile& tile)
    {
        std::vector<std::uint8_t> bytes;
        const LodError error = writeTerrainTile(tile, bytes);
        EXPECT_TRUE(error.ok()) << error.describe();
        return bytes;
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

    // Parsing must fail and the located error must name a field and the header byte of that field.
    void expectRefusedAt(const std::vector<std::uint8_t>& bytes, std::uint64_t expectedOffset, const char* what)
    {
        SCOPED_TRACE(what);
        TerrainTile tile;
        const LodError error = parseTerrainTile(bytes, tile);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
        EXPECT_EQ(error.mOffset, expectedOffset);
    }

    // Parsing must fail; the offset is not asserted (the failure has more than one possible location).
    void expectRefused(const std::vector<std::uint8_t>& bytes, const char* what)
    {
        SCOPED_TRACE(what);
        TerrainTile tile;
        const LodError error = parseTerrainTile(bytes, tile);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
    }

    // The tile used as the base of the malformed cases: N = 5, normals, an 8 x 8 albedo with 4 mips.
    TerrainTile baseTile()
    {
        return makeTile(5, true, 8, 4);
    }

    // ---- albedoByteSize ---------------------------------------------------------------------------------------

    TEST(LodTerrainTile, albedo_byte_size_of_rgba8_mip_chains)
    {
        EXPECT_EQ(albedoByteSize(1, 1, AlbedoFormat::Rgba8, 1), 4u);
        EXPECT_EQ(albedoByteSize(4, 4, AlbedoFormat::Rgba8, 1), 64u);
        EXPECT_EQ(albedoByteSize(4, 4, AlbedoFormat::Rgba8, 3), 64u + 16u + 4u);

        std::uint64_t expected = 0;
        for (std::uint32_t mip = 0; mip < 9; ++mip)
        {
            const std::uint64_t side = 256u >> mip;
            expected += side * side * 4;
        }
        EXPECT_EQ(albedoByteSize(256, 256, AlbedoFormat::Rgba8, 9), expected);
        EXPECT_EQ(expected, 349524u);
        EXPECT_EQ(albedoByteSize(256, 256, AlbedoFormat::Rgba8, 1), 256u * 256u * 4u);
        EXPECT_EQ(albedoByteSize(2048, 2048, AlbedoFormat::Rgba8, 1), 2048ull * 2048ull * 4ull);
        // A picture that is not square: each level is max(1, size >> mip) wide and high.
        EXPECT_EQ(albedoByteSize(8, 2, AlbedoFormat::Rgba8, 4), (8u * 2u + 4u * 1u + 2u * 1u + 1u * 1u) * 4u);
    }

    TEST(LodTerrainTile, albedo_byte_size_of_bc1_counts_blocks_with_a_minimum_of_one)
    {
        EXPECT_EQ(albedoByteSize(4, 4, AlbedoFormat::Bc1, 1), 8u);
        EXPECT_EQ(albedoByteSize(8, 8, AlbedoFormat::Bc1, 1), 32u);
        // 8x8: 4 blocks, 4x4: 1 block, 2x2: 1 block, 1x1: 1 block.
        EXPECT_EQ(albedoByteSize(8, 8, AlbedoFormat::Bc1, 4), (4u + 1u + 1u + 1u) * 8u);
        EXPECT_EQ(albedoByteSize(1, 1, AlbedoFormat::Bc1, 1), 8u);
        EXPECT_EQ(albedoByteSize(256, 256, AlbedoFormat::Bc1, 1), 64u * 64u * 8u);
    }

    TEST(LodTerrainTile, albedo_byte_size_is_zero_for_invalid_pictures)
    {
        EXPECT_EQ(albedoByteSize(0, 4, AlbedoFormat::Rgba8, 1), 0u);
        EXPECT_EQ(albedoByteSize(4, 0, AlbedoFormat::Rgba8, 1), 0u);
        EXPECT_EQ(albedoByteSize(3, 4, AlbedoFormat::Rgba8, 1), 0u);
        EXPECT_EQ(albedoByteSize(4, 6, AlbedoFormat::Rgba8, 1), 0u);
        EXPECT_EQ(albedoByteSize(100, 100, AlbedoFormat::Rgba8, 1), 0u);
        EXPECT_EQ(albedoByteSize(4096, 4096, AlbedoFormat::Rgba8, 1), 0u);
        EXPECT_EQ(albedoByteSize(2048, 4096, AlbedoFormat::Rgba8, 1), 0u);
        EXPECT_EQ(albedoByteSize(4, 4, AlbedoFormat::Rgba8, 0), 0u);
        // 4 x 4 has at most 3 mips (4, 2, 1).
        EXPECT_EQ(albedoByteSize(4, 4, AlbedoFormat::Rgba8, 4), 0u);
        EXPECT_EQ(albedoByteSize(256, 256, AlbedoFormat::Rgba8, 10), 0u);
        EXPECT_EQ(albedoByteSize(256, 256, AlbedoFormat::Rgba8, 1000), 0u);
        EXPECT_EQ(albedoByteSize(1, 1, AlbedoFormat::Rgba8, 2), 0u);
        EXPECT_EQ(albedoByteSize(8, 8, AlbedoFormat::Bc1, 5), 0u);
        EXPECT_EQ(albedoByteSize(8, 8, AlbedoFormat::Bc1, 0), 0u);
    }

    // ---- layout -----------------------------------------------------------------------------------------------

    TEST(LodTerrainTile, layout_of_a_tile_with_everything)
    {
        const TerrainTile tile = makeTile(3, true, 4, 3);
        TerrainTileLayout layout;
        ASSERT_TRUE(computeTerrainTileLayout(tile, layout).ok());
        EXPECT_EQ(layout.mHeightsOffset, 88u);
        EXPECT_EQ(layout.mHeightsSize, 18u);
        EXPECT_EQ(layout.mNormalsOffset, 112u); // align8(88 + 18)
        EXPECT_EQ(layout.mNormalsSize, 18u);
        EXPECT_EQ(layout.mAlbedoOffset, 136u); // align8(112 + 18)
        EXPECT_EQ(layout.mAlbedoSize, 84u);
        EXPECT_EQ(layout.mTotalSize, 224u); // align8(136 + 84)
    }

    TEST(LodTerrainTile, layout_with_absent_sections_has_offset_zero_and_size_zero)
    {
        TerrainTileLayout layout;
        ASSERT_TRUE(computeTerrainTileLayout(makeTile(3, false, 0, 0), layout).ok());
        EXPECT_EQ(layout.mHeightsOffset, 88u);
        EXPECT_EQ(layout.mHeightsSize, 18u);
        EXPECT_EQ(layout.mNormalsOffset, 0u);
        EXPECT_EQ(layout.mNormalsSize, 0u);
        EXPECT_EQ(layout.mAlbedoOffset, 0u);
        EXPECT_EQ(layout.mAlbedoSize, 0u);
        EXPECT_EQ(layout.mTotalSize, 112u); // align8(106)

        // Normals only, N = 5: heights 50 bytes, normals at align8(138) = 144, end 194, total 200.
        ASSERT_TRUE(computeTerrainTileLayout(makeTile(5, true, 0, 0), layout).ok());
        EXPECT_EQ(layout.mNormalsOffset, 144u);
        EXPECT_EQ(layout.mNormalsSize, 50u);
        EXPECT_EQ(layout.mAlbedoOffset, 0u);
        EXPECT_EQ(layout.mTotalSize, 200u);

        // Albedo only, N = 4: heights 32 bytes end at 120 (aligned), albedo 4 x 4 x 1 mip = 64, total 184.
        ASSERT_TRUE(computeTerrainTileLayout(makeTile(4, false, 4, 1), layout).ok());
        EXPECT_EQ(layout.mNormalsOffset, 0u);
        EXPECT_EQ(layout.mAlbedoOffset, 120u);
        EXPECT_EQ(layout.mAlbedoSize, 64u);
        EXPECT_EQ(layout.mTotalSize, 184u);
    }

    TEST(LodTerrainTile, layout_fails_for_an_invalid_tile)
    {
        TerrainTileLayout layout;
        EXPECT_FALSE(computeTerrainTileLayout(makeTile(2, false, 0, 0), layout).ok());
        TerrainTile tile = makeTile(3, false, 0, 0);
        tile.mMinHeight = 10.f;
        tile.mMaxHeight = 5.f;
        EXPECT_FALSE(computeTerrainTileLayout(tile, layout).ok());
    }

    // ---- write and parse ------------------------------------------------------------------------------------

    TEST(LodTerrainTile, round_trip_of_tiles_of_every_shape)
    {
        struct Shape
        {
            std::uint32_t mGrid;
            bool mNormals;
            std::uint32_t mAlbedoSide;
            std::uint32_t mMips;
        };
        const Shape shapes[] = {
            { 3, false, 0, 0 },
            { 3, true, 0, 0 },
            { 3, false, 4, 3 },
            { 3, true, 4, 3 },
            { 5, false, 0, 0 },
            { 5, true, 0, 0 },
            { 5, false, 8, 1 },
            { 5, true, 4, 3 },
            { 33, true, 16, 5 },
            { 129, true, 0, 0 },
            { 129, true, 32, 6 },
        };
        std::uint32_t seed = 10;
        for (const Shape& shape : shapes)
        {
            SCOPED_TRACE(testing::Message()
                << "N " << shape.mGrid << " normals " << shape.mNormals << " albedo " << shape.mAlbedoSide);
            const TerrainTile tile = makeTile(shape.mGrid, shape.mNormals, shape.mAlbedoSide, shape.mMips, ++seed);
            const std::vector<std::uint8_t> bytes = payloadOf(tile);
            ASSERT_FALSE(bytes.empty());
            EXPECT_EQ(bytes.size() % 8, 0u);

            TerrainTileLayout layout;
            ASSERT_TRUE(computeTerrainTileLayout(tile, layout).ok());
            EXPECT_EQ(bytes.size(), layout.mTotalSize);

            TerrainTile parsed;
            const LodError error = parseTerrainTile(bytes, parsed);
            ASSERT_TRUE(error.ok()) << error.describe();
            expectSameTile(tile, parsed);

            std::vector<std::uint8_t> again;
            ASSERT_TRUE(writeTerrainTile(parsed, again).ok());
            EXPECT_EQ(again, bytes);
        }
    }

    TEST(LodTerrainTile, equal_height_range_and_zero_error_are_valid)
    {
        TerrainTile tile = makeTile(3, false, 0, 0);
        tile.mMinHeight = 42.f;
        tile.mMaxHeight = 42.f;
        tile.mGeometricError = 0.f;
        std::vector<std::uint8_t> bytes;
        ASSERT_TRUE(writeTerrainTile(tile, bytes).ok());
        TerrainTile parsed;
        EXPECT_TRUE(parseTerrainTile(bytes, parsed).ok());
    }

    TEST(LodTerrainTile, header_bytes_of_a_tiny_tile_derived_by_hand)
    {
        TerrainTile tile;
        tile.mGridSize = 3;
        tile.mLevel = 1;
        tile.mSide = 16384.f; // 0x46800000
        tile.mOriginX = -16384.f; // 0xC6800000
        tile.mOriginY = 0.f;
        tile.mMinHeight = -10.f; // 0xC1200000
        tile.mMaxHeight = 100.f; // 0x42C80000
        tile.mGeometricError = 2.5f; // 0x40200000
        tile.mHeights = { 0x0100, 0x0302, 0x0504, 0x0706, 0x0908, 0x0B0A, 0x0D0C, 0x0F0E, 0xFFFF };

        const std::vector<std::uint8_t> expected = { // 0: magic "ETT1"
            0x45, 0x54, 0x54, 0x31,
            // 4: version 1 (layout freeze, 2026-11-02), 6: flags 0
            0x01, 0x00, 0x00, 0x00,
            // 8: N = 3, 12: level = 1
            0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            // 16: side 16384.0f, 20: origin x -16384.0f, 24: origin y 0.0f
            0x00, 0x00, 0x80, 0x46, 0x00, 0x00, 0x80, 0xC6, 0x00, 0x00, 0x00, 0x00,
            // 28: min -10.0f, 32: max 100.0f, 36: error 2.5f
            0x00, 0x00, 0x20, 0xC1, 0x00, 0x00, 0xC8, 0x42, 0x00, 0x00, 0x20, 0x40,
            // 40: albedo width, height; 44: format; 48: mip count (all zero without albedo)
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            // 52: heights offset 88; 56: normals offset 0; 60: albedo offset 0
            0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            // 64: reserved, 24 bytes of zero
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            // 88: nine heights, little endian
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF,
            // 106: padding to a multiple of 8 (112)
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };
        ASSERT_EQ(expected.size(), 112u);

        std::vector<std::uint8_t> bytes;
        ASSERT_TRUE(writeTerrainTile(tile, bytes).ok());
        EXPECT_EQ(bytes, expected);
        EXPECT_EQ(terrainTileHeaderSize, 88u);
        EXPECT_EQ(terrainTilePayloadVersion, 1);
    }

    TEST(LodTerrainTile, header_words_of_a_tile_with_normals_and_albedo)
    {
        const TerrainTile tile = makeTile(3, true, 4, 3);
        const std::vector<std::uint8_t> bytes = payloadOf(tile);
        ASSERT_EQ(bytes.size(), 224u);
        EXPECT_EQ(getU32(bytes, offMagic), 0x31545445u); // "ETT1" read as a little-endian word
        EXPECT_EQ(bytes[offVersion], 1u);
        EXPECT_EQ(bytes[offVersion + 1], 0u);
        EXPECT_EQ(bytes[offFlags], 0x03u); // normals and albedo
        EXPECT_EQ(bytes[offFlags + 1], 0x00u);
        EXPECT_EQ(getU32(bytes, offGrid), 3u);
        EXPECT_EQ(getU32(bytes, 12), tile.mLevel);
        EXPECT_EQ(getU32(bytes, 16), floatBits(tile.mSide));
        EXPECT_EQ(getU32(bytes, 20), floatBits(tile.mOriginX));
        EXPECT_EQ(getU32(bytes, 24), floatBits(tile.mOriginY));
        EXPECT_EQ(getU32(bytes, offMinHeight), floatBits(tile.mMinHeight));
        EXPECT_EQ(getU32(bytes, offMaxHeight), floatBits(tile.mMaxHeight));
        EXPECT_EQ(getU32(bytes, offError), floatBits(tile.mGeometricError));
        EXPECT_EQ(bytes[offAlbedoWidth], 4u);
        EXPECT_EQ(bytes[offAlbedoWidth + 1], 0u);
        EXPECT_EQ(bytes[offAlbedoHeight], 4u);
        EXPECT_EQ(bytes[offAlbedoHeight + 1], 0u);
        EXPECT_EQ(getU32(bytes, 44), 0u); // RGBA8
        EXPECT_EQ(getU32(bytes, offAlbedoMips), 3u);
        EXPECT_EQ(getU32(bytes, offHeights), 88u);
        EXPECT_EQ(getU32(bytes, offNormals), 112u);
        EXPECT_EQ(getU32(bytes, offAlbedo), 136u);
        for (std::size_t i = offReserved; i < terrainTileHeaderSize; ++i)
        {
            EXPECT_EQ(bytes[i], 0u) << i;
        }
        // Sections at their offsets, and zero padding between and after them.
        for (std::size_t i = 0; i < tile.mHeights.size(); ++i)
        {
            EXPECT_EQ(bytes[88 + 2 * i], tile.mHeights[i] & 0xFF) << i;
            EXPECT_EQ(bytes[88 + 2 * i + 1], tile.mHeights[i] >> 8) << i;
        }
        for (std::size_t i = 0; i < tile.mNormals.size(); ++i)
        {
            EXPECT_EQ(bytes[112 + i], static_cast<std::uint8_t>(tile.mNormals[i])) << i;
        }
        for (std::size_t i = 0; i < tile.mAlbedo.size(); ++i)
        {
            EXPECT_EQ(bytes[136 + i], tile.mAlbedo[i]) << i;
        }
        for (const std::size_t i : { 106, 107, 108, 109, 110, 111, 130, 131, 132, 133, 134, 135, 220, 221, 222, 223 })
        {
            EXPECT_EQ(bytes[i], 0u) << "padding byte " << i;
        }
    }

    TEST(LodTerrainTile, write_is_deterministic)
    {
        const TerrainTile tile = baseTile();
        EXPECT_EQ(payloadOf(tile), payloadOf(tile));
    }

    // ---- header parse ---------------------------------------------------------------------------------------

    TEST(LodTerrainTile, header_parse_fills_the_fields_and_the_layout_but_not_the_arrays)
    {
        const TerrainTile tile = baseTile();
        const std::vector<std::uint8_t> bytes = payloadOf(tile);
        TerrainTile parsed;
        TerrainTileLayout layout;
        const LodError error = parseTerrainTileHeader(
            std::span<const std::uint8_t>(bytes.data(), terrainTileHeaderSize), bytes.size(), parsed, layout);
        ASSERT_TRUE(error.ok()) << error.describe();
        EXPECT_EQ(parsed.mGridSize, tile.mGridSize);
        EXPECT_EQ(parsed.mLevel, tile.mLevel);
        EXPECT_EQ(parsed.mSide, tile.mSide);
        EXPECT_EQ(parsed.mOriginX, tile.mOriginX);
        EXPECT_EQ(parsed.mOriginY, tile.mOriginY);
        EXPECT_EQ(parsed.mMinHeight, tile.mMinHeight);
        EXPECT_EQ(parsed.mMaxHeight, tile.mMaxHeight);
        EXPECT_EQ(parsed.mGeometricError, tile.mGeometricError);
        EXPECT_TRUE(parsed.mHasNormals);
        EXPECT_TRUE(parsed.mHasAlbedo);
        EXPECT_EQ(parsed.mAlbedoWidth, 8u);
        EXPECT_EQ(parsed.mAlbedoHeight, 8u);
        EXPECT_EQ(parsed.mAlbedoMipCount, 4u);
        EXPECT_EQ(parsed.mAlbedoFormat, AlbedoFormat::Rgba8);

        TerrainTileLayout expected;
        ASSERT_TRUE(computeTerrainTileLayout(tile, expected).ok());
        EXPECT_EQ(layout.mHeightsOffset, expected.mHeightsOffset);
        EXPECT_EQ(layout.mHeightsSize, expected.mHeightsSize);
        EXPECT_EQ(layout.mNormalsOffset, expected.mNormalsOffset);
        EXPECT_EQ(layout.mNormalsSize, expected.mNormalsSize);
        EXPECT_EQ(layout.mAlbedoOffset, expected.mAlbedoOffset);
        EXPECT_EQ(layout.mAlbedoSize, expected.mAlbedoSize);
        EXPECT_EQ(layout.mTotalSize, expected.mTotalSize);
        EXPECT_EQ(layout.mTotalSize, bytes.size());
    }

    TEST(LodTerrainTile, header_parse_refuses_a_header_shorter_than_88_bytes)
    {
        const std::vector<std::uint8_t> bytes = payloadOf(baseTile());
        TerrainTile parsed;
        TerrainTileLayout layout;
        for (const std::size_t length :
            { std::size_t{ 0 }, std::size_t{ 1 }, std::size_t{ 4 }, std::size_t{ 50 }, terrainTileHeaderSize - 1 })
        {
            EXPECT_FALSE(parseTerrainTileHeader(
                std::span<const std::uint8_t>(bytes.data(), length), bytes.size(), parsed, layout)
                             .ok())
                << length;
        }
    }

    TEST(LodTerrainTile, header_parse_refuses_an_entry_size_that_does_not_match_exactly)
    {
        const std::vector<std::uint8_t> bytes = payloadOf(baseTile());
        const std::span<const std::uint8_t> header(bytes.data(), terrainTileHeaderSize);
        TerrainTile parsed;
        TerrainTileLayout layout;
        EXPECT_TRUE(parseTerrainTileHeader(header, bytes.size(), parsed, layout).ok());
        EXPECT_FALSE(parseTerrainTileHeader(header, bytes.size() + 8, parsed, layout).ok());
        EXPECT_FALSE(parseTerrainTileHeader(header, bytes.size() + 1, parsed, layout).ok());
        EXPECT_FALSE(parseTerrainTileHeader(header, bytes.size() - 8, parsed, layout).ok());
        EXPECT_FALSE(parseTerrainTileHeader(header, bytes.size() - 1, parsed, layout).ok());
        EXPECT_FALSE(parseTerrainTileHeader(header, 0, parsed, layout).ok());
        EXPECT_FALSE(parseTerrainTileHeader(header, terrainTileHeaderSize, parsed, layout).ok());
        EXPECT_FALSE(parseTerrainTileHeader(header, std::numeric_limits<std::uint64_t>::max(), parsed, layout).ok());
    }

    // ---- malformed input, field by field ----------------------------------------------------------------

    TEST(LodTerrainTile, the_valid_base_payload_parses)
    {
        TerrainTile parsed;
        const LodError error = parseTerrainTile(payloadOf(baseTile()), parsed);
        EXPECT_TRUE(error.ok()) << error.describe();
    }

    TEST(LodTerrainTile, bad_magic_is_refused_at_offset_0)
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            bytes[i] ^= 0x20;
            expectRefusedAt(bytes, offMagic, "magic byte");
        }
    }

    TEST(LodTerrainTile, a_version_other_than_one_is_refused_at_offset_4)
    {
        for (const std::uint16_t version :
            { std::uint16_t{ 0 }, std::uint16_t{ 2 }, std::uint16_t{ 0x100 }, std::uint16_t{ 0xFFFF } })
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            setU16(bytes, offVersion, version);
            expectRefusedAt(bytes, offVersion, "version");
        }
    }

    TEST(LodTerrainTile, unknown_flag_bits_and_the_material_flag_are_refused_at_offset_6)
    {
        for (const std::uint16_t extra :
            { std::uint16_t{ 0x0004 }, std::uint16_t{ 0x0008 }, std::uint16_t{ 0x0100 }, std::uint16_t{ 0x8000 } })
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            setU16(bytes, offFlags, static_cast<std::uint16_t>(0x0003 | extra));
            expectRefusedAt(bytes, offFlags, "flags");
        }
    }

    TEST(LodTerrainTile, a_grid_size_outside_3_to_129_is_refused_at_offset_8)
    {
        for (const std::uint32_t grid : { 0u, 1u, 2u, 130u, 200u, 1000u, 0xFFFFFFFFu })
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            setU32(bytes, offGrid, grid);
            expectRefusedAt(bytes, offGrid, "grid size");
        }
    }

    TEST(LodTerrainTile, nonzero_reserved_bytes_are_refused_inside_the_reserved_field)
    {
        for (const std::size_t position :
            { std::size_t{ 64 }, std::size_t{ 70 }, std::size_t{ 80 }, std::size_t{ 87 } })
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            bytes[position] = 1;
            SCOPED_TRACE(position);
            TerrainTile tile;
            const LodError error = parseTerrainTile(bytes, tile);
            EXPECT_FALSE(error.ok());
            // Either the start of the reserved field or the byte itself.
            EXPECT_GE(error.mOffset, offReserved);
            EXPECT_LT(error.mOffset, terrainTileHeaderSize);
        }
    }

    TEST(LodTerrainTile, a_minimum_above_the_maximum_is_refused_in_the_height_range)
    {
        std::vector<std::uint8_t> bytes = payloadOf(baseTile());
        setU32(bytes, offMinHeight, floatBits(400.f)); // above the maximum 300.25
        SCOPED_TRACE("min above max");
        TerrainTile tile;
        const LodError error = parseTerrainTile(bytes, tile);
        EXPECT_FALSE(error.ok());
        EXPECT_TRUE(error.mOffset == offMinHeight || error.mOffset == offMaxHeight) << error.mOffset;

        bytes = payloadOf(baseTile());
        setU32(bytes, offMaxHeight, floatBits(-50.f)); // below the minimum -12.5
        const LodError second = parseTerrainTile(bytes, tile);
        EXPECT_FALSE(second.ok());
        EXPECT_TRUE(second.mOffset == offMinHeight || second.mOffset == offMaxHeight) << second.mOffset;
    }

    TEST(LodTerrainTile, a_nan_infinite_or_negative_error_is_refused_at_offset_36)
    {
        for (const float value : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                 -std::numeric_limits<float>::infinity(), -1.f, -0.001f })
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            setU32(bytes, offError, floatBits(value));
            expectRefusedAt(bytes, offError, "geometric error");
        }
    }

    TEST(LodTerrainTile, a_bad_albedo_size_is_refused_at_the_width_or_height_field)
    {
        for (const std::uint16_t width : { std::uint16_t{ 0 }, std::uint16_t{ 3 }, std::uint16_t{ 6 },
                 std::uint16_t{ 100 }, std::uint16_t{ 4096 }, std::uint16_t{ 0xFFFF } })
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            setU16(bytes, offAlbedoWidth, width);
            expectRefusedAt(bytes, offAlbedoWidth, "albedo width");
        }
        for (const std::uint16_t height : { std::uint16_t{ 0 }, std::uint16_t{ 3 }, std::uint16_t{ 6 },
                 std::uint16_t{ 100 }, std::uint16_t{ 4096 }, std::uint16_t{ 0xFFFF } })
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            setU16(bytes, offAlbedoHeight, height);
            expectRefusedAt(bytes, offAlbedoHeight, "albedo height");
        }
    }

    TEST(LodTerrainTile, a_bad_mip_count_is_refused_at_offset_48)
    {
        // The base picture is 8 x 8: 1 to 4 mips are valid.
        for (const std::uint32_t mips : { 0u, 5u, 10u, 0xFFFFFFFFu })
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            setU32(bytes, offAlbedoMips, mips);
            expectRefusedAt(bytes, offAlbedoMips, "mip count");
        }
    }

    TEST(LodTerrainTile, an_offset_past_the_end_is_refused_at_that_offset_field)
    {
        for (const std::uint32_t value : { 0x00010000u, 0x7FFFFFF8u, 0xFFFFFFF8u })
        {
            std::vector<std::uint8_t> bytes = payloadOf(baseTile());
            setU32(bytes, offHeights, value);
            expectRefusedAt(bytes, offHeights, "heights offset past the end");
            bytes = payloadOf(baseTile());
            setU32(bytes, offNormals, value);
            expectRefusedAt(bytes, offNormals, "normals offset past the end");
            bytes = payloadOf(baseTile());
            setU32(bytes, offAlbedo, value);
            expectRefusedAt(bytes, offAlbedo, "albedo offset past the end");
        }
    }

    TEST(LodTerrainTile, an_offset_that_is_not_8_aligned_is_refused_at_that_offset_field)
    {
        const std::vector<std::uint8_t> base = payloadOf(baseTile());
        TerrainTileLayout layout;
        ASSERT_TRUE(computeTerrainTileLayout(baseTile(), layout).ok());
        for (const std::uint32_t shift : { 1u, 2u, 4u, 7u })
        {
            std::vector<std::uint8_t> bytes = base;
            setU32(bytes, offHeights, static_cast<std::uint32_t>(layout.mHeightsOffset) + shift);
            expectRefusedAt(bytes, offHeights, "heights offset unaligned");
            bytes = base;
            setU32(bytes, offNormals, static_cast<std::uint32_t>(layout.mNormalsOffset) + shift);
            expectRefusedAt(bytes, offNormals, "normals offset unaligned");
            bytes = base;
            setU32(bytes, offAlbedo, static_cast<std::uint32_t>(layout.mAlbedoOffset) + shift);
            expectRefusedAt(bytes, offAlbedo, "albedo offset unaligned");
        }
    }

    TEST(LodTerrainTile, overlapping_sections_are_refused)
    {
        TerrainTileLayout layout;
        ASSERT_TRUE(computeTerrainTileLayout(baseTile(), layout).ok());
        std::vector<std::uint8_t> bytes = payloadOf(baseTile());
        // The albedo starts where the normals start.
        setU32(bytes, offAlbedo, static_cast<std::uint32_t>(layout.mNormalsOffset));
        expectRefused(bytes, "albedo over normals");

        // The normals start inside the heights (still 8-aligned).
        bytes = payloadOf(baseTile());
        setU32(bytes, offNormals, static_cast<std::uint32_t>(layout.mHeightsOffset) + 8);
        expectRefused(bytes, "normals over heights");

        // The albedo starts inside the normals.
        bytes = payloadOf(baseTile());
        setU32(bytes, offAlbedo, static_cast<std::uint32_t>(layout.mNormalsOffset) + 8);
        expectRefused(bytes, "albedo inside normals");
    }

    TEST(LodTerrainTile, sizes_that_do_not_add_up_exactly_are_refused_when_too_long_and_too_short)
    {
        const std::vector<std::uint8_t> base = payloadOf(baseTile());

        for (const std::size_t extra : { std::size_t{ 1 }, std::size_t{ 7 }, std::size_t{ 8 }, std::size_t{ 64 } })
        {
            std::vector<std::uint8_t> bytes = base;
            bytes.insert(bytes.end(), extra, 0);
            expectRefused(bytes, "extra bytes after the last section");
        }
        for (const std::size_t cut : { std::size_t{ 1 }, std::size_t{ 7 }, std::size_t{ 8 }, std::size_t{ 100 } })
        {
            std::vector<std::uint8_t> bytes = base;
            bytes.resize(bytes.size() - cut);
            expectRefused(bytes, "bytes missing at the end");
        }

        // A header that claims a larger albedo (16 x 16, 5 mips) than the entry holds.
        std::vector<std::uint8_t> bytes = base;
        setU16(bytes, offAlbedoWidth, 16);
        setU16(bytes, offAlbedoHeight, 16);
        setU32(bytes, offAlbedoMips, 5);
        expectRefused(bytes, "albedo larger than the entry");

        // A header that claims a smaller albedo (4 x 4, 3 mips) than the entry holds.
        bytes = base;
        setU16(bytes, offAlbedoWidth, 4);
        setU16(bytes, offAlbedoHeight, 4);
        setU32(bytes, offAlbedoMips, 3);
        expectRefused(bytes, "albedo smaller than the entry");
    }

    TEST(LodTerrainTile, every_truncation_of_a_valid_payload_is_refused)
    {
        const std::vector<std::uint8_t> bytes = payloadOf(makeTile(3, true, 4, 3));
        ASSERT_EQ(bytes.size(), 224u);
        for (std::size_t length = 0; length < bytes.size(); ++length)
        {
            TerrainTile tile;
            const LodError error = parseTerrainTile(std::span<const std::uint8_t>(bytes.data(), length), tile);
            EXPECT_FALSE(error.ok()) << "prefix of length " << length;
        }
        // Sample prefixes of larger payloads.
        const std::vector<std::uint8_t> large = payloadOf(makeTile(33, true, 16, 5));
        for (const std::size_t length : { std::size_t{ 0 }, std::size_t{ 87 }, std::size_t{ 88 }, std::size_t{ 89 },
                 std::size_t{ 1000 }, large.size() / 2, large.size() - 8, large.size() - 1 })
        {
            TerrainTile tile;
            const LodError error = parseTerrainTile(std::span<const std::uint8_t>(large.data(), length), tile);
            EXPECT_FALSE(error.ok()) << "prefix of length " << length;
        }
    }

    TEST(LodTerrainTile, nonzero_padding_is_refused)
    {
        const std::vector<std::uint8_t> base = payloadOf(makeTile(3, true, 4, 3));
        ASSERT_EQ(base.size(), 224u);
        for (const std::size_t position : { 106, 109, 111, 130, 133, 135, 220, 223 })
        {
            std::vector<std::uint8_t> bytes = base;
            bytes[position] = 0x5A;
            SCOPED_TRACE(position);
            expectRefused(bytes, "padding byte");
        }
        // A tile without normals and albedo has padding after the heights.
        std::vector<std::uint8_t> small = payloadOf(makeTile(3, false, 0, 0));
        ASSERT_EQ(small.size(), 112u);
        small[108] = 1;
        expectRefused(small, "trailing padding");
    }

    // ---- write refusals -------------------------------------------------------------------------------------

    void expectWriteRefused(const TerrainTile& tile, const char* what)
    {
        SCOPED_TRACE(what);
        std::vector<std::uint8_t> bytes;
        const LodError error = writeTerrainTile(tile, bytes);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
        EXPECT_TRUE(bytes.empty());
    }

    TEST(LodTerrainTile, write_refuses_an_invalid_grid_size)
    {
        expectWriteRefused(makeTile(2, false, 0, 0), "N = 2");
        expectWriteRefused(makeTile(130, false, 0, 0), "N = 130");
        TerrainTile tile = makeTile(3, false, 0, 0);
        tile.mGridSize = 0;
        tile.mHeights.clear();
        expectWriteRefused(tile, "N = 0");
    }

    TEST(LodTerrainTile, write_refuses_a_minimum_above_the_maximum)
    {
        TerrainTile tile = makeTile(3, false, 0, 0);
        tile.mMinHeight = 10.f;
        tile.mMaxHeight = 9.f;
        expectWriteRefused(tile, "min above max");
    }

    TEST(LodTerrainTile, write_refuses_a_non_finite_or_negative_error)
    {
        for (const float value : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                 -std::numeric_limits<float>::infinity(), -1.f })
        {
            TerrainTile tile = makeTile(3, false, 0, 0);
            tile.mGeometricError = value;
            expectWriteRefused(tile, "error");
        }
    }

    TEST(LodTerrainTile, write_refuses_array_sizes_that_do_not_match)
    {
        TerrainTile tile = makeTile(5, true, 8, 4);
        tile.mHeights.pop_back();
        expectWriteRefused(tile, "heights one short");

        tile = makeTile(5, true, 8, 4);
        tile.mHeights.push_back(0);
        expectWriteRefused(tile, "heights one long");

        tile = makeTile(5, true, 8, 4);
        tile.mNormals.pop_back();
        expectWriteRefused(tile, "normals one short");

        tile = makeTile(5, true, 8, 4);
        tile.mNormals.clear();
        expectWriteRefused(tile, "normals flagged but empty");

        tile = makeTile(5, true, 8, 4);
        tile.mAlbedo.pop_back();
        expectWriteRefused(tile, "albedo one short");

        tile = makeTile(5, true, 8, 4);
        tile.mAlbedo.push_back(0);
        expectWriteRefused(tile, "albedo one long");

        tile = makeTile(5, true, 8, 4);
        tile.mAlbedo.clear();
        expectWriteRefused(tile, "albedo flagged but empty");
    }

    TEST(LodTerrainTile, write_refuses_an_invalid_albedo)
    {
        TerrainTile tile = makeTile(5, false, 8, 4);
        tile.mAlbedoWidth = 6;
        expectWriteRefused(tile, "width not a power of two");

        tile = makeTile(5, false, 8, 4);
        tile.mAlbedoHeight = 0;
        expectWriteRefused(tile, "height zero");

        tile = makeTile(5, false, 8, 4);
        tile.mAlbedoWidth = 4096;
        tile.mAlbedoHeight = 4096;
        expectWriteRefused(tile, "larger than 2048");

        tile = makeTile(5, false, 8, 4);
        tile.mAlbedoMipCount = 0;
        expectWriteRefused(tile, "mip count zero");

        tile = makeTile(5, false, 8, 4);
        tile.mAlbedoMipCount = 5;
        expectWriteRefused(tile, "too many mips");
    }

    // ---- a deterministic mutation fuzz ------------------------------------------------------------------------

    TEST(LodTerrainTile, mutated_payloads_never_crash_and_either_fail_or_rewrite_to_the_same_bytes)
    {
        const std::vector<std::vector<std::uint8_t>> seeds = {
            payloadOf(makeTile(3, false, 0, 0, 1)),
            payloadOf(makeTile(5, true, 0, 0, 2)),
            payloadOf(makeTile(3, true, 4, 3, 3)),
            payloadOf(makeTile(4, false, 8, 4, 4)),
        };
        const std::uint32_t interesting[] = { 0u, 1u, 2u, 3u, 7u, 8u, 88u, 89u, 129u, 130u, 0x7FFFFFFFu, 0x80000000u,
            0xFFFFFFFFu, 0xFFFFFFF8u, 0x7FC00000u, 0x7F800000u, 0xBF800000u };

        Rng rng(0xED3A5EEDC0FFEE11ull);
        std::size_t accepted = 0;
        for (const std::vector<std::uint8_t>& seed : seeds)
        {
            for (unsigned iteration = 0; iteration < 5000; ++iteration)
            {
                std::vector<std::uint8_t> bytes = seed;
                switch (rng.below(6))
                {
                    case 0: // flip a few bytes anywhere
                    {
                        const std::uint64_t count = 1 + rng.below(4);
                        for (std::uint64_t i = 0; i < count; ++i)
                        {
                            bytes[rng.below(bytes.size())] ^= static_cast<std::uint8_t>(1 + rng.below(255));
                        }
                        break;
                    }
                    case 1: // flip a byte in the header
                        bytes[rng.below(terrainTileHeaderSize)] ^= static_cast<std::uint8_t>(1 + rng.below(255));
                        break;
                    case 2: // truncate
                        bytes.resize(rng.below(bytes.size()));
                        break;
                    case 3: // extend
                    {
                        const std::uint64_t count = 1 + rng.below(64);
                        for (std::uint64_t i = 0; i < count; ++i)
                        {
                            bytes.push_back(static_cast<std::uint8_t>(rng.below(256)));
                        }
                        break;
                    }
                    case 4: // an interesting 32-bit value at a 4-aligned header position
                        setU32(bytes, 4 * rng.below(terrainTileHeaderSize / 4),
                            interesting[rng.below(std::size(interesting))]);
                        break;
                    default: // a random byte somewhere
                        bytes[rng.below(bytes.size())] = static_cast<std::uint8_t>(rng.below(256));
                        break;
                }

                TerrainTile tile;
                const LodError error = parseTerrainTile(bytes, tile);
                if (error.ok())
                {
                    ++accepted;
                    std::vector<std::uint8_t> again;
                    const LodError writeError = writeTerrainTile(tile, again);
                    EXPECT_TRUE(writeError.ok()) << writeError.describe();
                    EXPECT_EQ(again, bytes) << "iteration " << iteration;
                }
                else
                {
                    EXPECT_FALSE(error.mField.empty());
                }
            }
        }
        // Some mutations (a changed height or albedo byte) leave a valid payload, so the check above ran.
        EXPECT_GT(accepted, 0u);
    }
}
