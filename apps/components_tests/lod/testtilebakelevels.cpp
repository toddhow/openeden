// Task 0055 (albedo bake): tiles with a baked albedo, and the baker for several levels at once. Pure code over a
// generated LandSource and AlbedoSource; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/tilebake.hpp>

#include <edenarchive/source.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace Lod;

    using Cell = std::pair<std::int32_t, std::int32_t>;
    using Bytes = std::vector<std::uint8_t>;
    using Hash = EdenArchive::Hash;

    constexpr std::int64_t intervalsPerCell = 64;
    constexpr std::uint32_t planAlbedoSize = 64;

    // Land generated from a function of the global vertex coordinates; every cell has land.
    class HillLand final : public LandSource
    {
    public:
        static float height(std::int64_t gx, std::int64_t gy)
        {
            const double x = static_cast<double>(gx);
            const double y = static_cast<double>(gy);
            return static_cast<float>(100.0 * std::sin(0.05 * x) + 50.0 * std::cos(0.07 * y) + 0.1 * x);
        }

        void sample(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells, std::uint32_t lod,
            std::vector<float>& heights, std::vector<float>& normals) const override
        {
            const std::size_t n = (static_cast<std::size_t>(intervalsPerCell) * sizeInCells >> lod) + 1;
            const std::int64_t step = std::int64_t{ 1 } << lod;
            heights.assign(n * n, 0.f);
            normals.assign(3 * n * n, 0.f);
            for (std::size_t j = 0; j < n; ++j)
            {
                for (std::size_t i = 0; i < n; ++i)
                {
                    const std::int64_t gx = cellX * intervalsPerCell + static_cast<std::int64_t>(i) * step;
                    const std::int64_t gy = cellY * intervalsPerCell + static_cast<std::int64_t>(j) * step;
                    const std::size_t index = j * n + i;
                    heights[index] = height(gx, gy);
                    normals[3 * index] = 0.f;
                    normals[3 * index + 1] = 0.f;
                    normals[3 * index + 2] = 1.f;
                }
            }
        }
    };

    Hash digestOf(std::uint8_t value)
    {
        Hash hash{};
        hash.fill(value);
        return hash;
    }

    std::uint64_t mixBits(std::int64_t a, std::int64_t b, std::int64_t c)
    {
        std::uint64_t value = static_cast<std::uint64_t>(a) * 0x9E3779B97F4A7C15ull
            ^ (static_cast<std::uint64_t>(b) + 0x7F4A7C15ull) * 0xBF58476D1CE4E5B9ull
            ^ (static_cast<std::uint64_t>(c) + 0x1234567ull) * 0x94D049BB133111EBull;
        value ^= value >> 31;
        value *= 0xD6E8FEB86659FD93ull;
        value ^= value >> 29;
        return value;
    }

    Rgba8Image patternedImage(std::uint32_t side, std::int64_t seed)
    {
        Rgba8Image image;
        image.mWidth = side;
        image.mHeight = side;
        for (std::uint32_t y = 0; y < side; ++y)
        {
            for (std::uint32_t x = 0; x < side; ++x)
            {
                const std::uint64_t bits = mixBits(x, y, seed);
                image.mPixels.insert(image.mPixels.end(),
                    { static_cast<std::uint8_t>(bits & 0xFF), static_cast<std::uint8_t>((bits >> 8) & 0xFF),
                        static_cast<std::uint8_t>((bits >> 16) & 0xFF), static_cast<std::uint8_t>(255) });
            }
        }
        return image;
    }

    // Three layers; the blend map only uses layers 0 and 1. Data from hashes of the global texel and vertex numbers.
    class HashedAlbedo final : public AlbedoSource
    {
    public:
        HashedAlbedo()
        {
            for (std::int64_t seed = 0; seed < 3; ++seed)
            {
                mImages.push_back(patternedImage(4, seed));
                mDigests.push_back(digestOf(static_cast<std::uint8_t>(seed + 1)));
            }
        }

        std::size_t layerCount() const override { return mImages.size(); }
        const Rgba8Image* layerImage(std::size_t index) const override { return &mImages.at(index); }
        Hash layerDigest(std::size_t index) const override { return mDigests.at(index); }

        void blendMap(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
            std::vector<std::uint8_t>& indices) const override
        {
            const std::size_t n = static_cast<std::size_t>(blendTexelsPerCell) * sizeInCells;
            indices.assign(n * n * 4, 0);
            for (std::size_t ty = 0; ty < n; ++ty)
            {
                for (std::size_t tx = 0; tx < n; ++tx)
                {
                    const std::int64_t gtx = std::int64_t{ cellX } * blendTexelsPerCell + static_cast<std::int64_t>(tx);
                    const std::int64_t gty = std::int64_t{ cellY } * blendTexelsPerCell + static_cast<std::int64_t>(ty);
                    const std::uint64_t bits = mixBits(gtx, gty, 11);
                    for (std::int64_t k = 0; k < 4; ++k)
                    {
                        std::uint8_t value = static_cast<std::uint8_t>((bits >> k) & 1);
                        const auto found = mBlendOverrides.find({ gtx, gty, k });
                        if (found != mBlendOverrides.end())
                        {
                            value = found->second;
                        }
                        indices[(ty * n + tx) * 4 + static_cast<std::size_t>(k)] = value;
                    }
                }
            }
        }

        void vertexColours(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
            std::vector<std::uint8_t>& rgb) const override
        {
            const std::size_t n = 64 * static_cast<std::size_t>(sizeInCells) + 1;
            rgb.assign(n * n * 3, 0);
            for (std::size_t vy = 0; vy < n; ++vy)
            {
                for (std::size_t vx = 0; vx < n; ++vx)
                {
                    const std::int64_t gvx = std::int64_t{ cellX } * 64 + static_cast<std::int64_t>(vx);
                    const std::int64_t gvy = std::int64_t{ cellY } * 64 + static_cast<std::int64_t>(vy);
                    const std::uint64_t bits = mixBits(gvx, gvy, 22);
                    for (std::int64_t k = 0; k < 3; ++k)
                    {
                        std::uint8_t value = static_cast<std::uint8_t>(128 + ((bits >> (8 * k)) & 127));
                        const auto found = mVertexOverrides.find({ gvx, gvy, k });
                        if (found != mVertexOverrides.end())
                        {
                            value = found->second;
                        }
                        rgb[(vy * n + vx) * 3 + static_cast<std::size_t>(k)] = value;
                    }
                }
            }
        }

        std::vector<Rgba8Image> mImages;
        std::vector<Hash> mDigests;
        std::map<std::array<std::int64_t, 3>, std::uint8_t> mBlendOverrides;
        std::map<std::array<std::int64_t, 3>, std::uint8_t> mVertexOverrides;
    };

    TileId levelTile(std::uint32_t level, std::int32_t cellX, std::int32_t cellY)
    {
        return tileContainingCell(0, level, cellX, cellY).value();
    }

    std::vector<std::uint8_t> payloadBytes(const TerrainTile& tile)
    {
        std::vector<std::uint8_t> bytes;
        const LodError error = writeTerrainTile(tile, bytes);
        EXPECT_TRUE(error.ok()) << error.describe();
        return bytes;
    }

    bool sameBakedTile(const BakedTile& a, const BakedTile& b)
    {
        return a.mId == b.mId && a.mSourceDigest == b.mSourceDigest && a.mParameters == b.mParameters
            && payloadBytes(a.mData) == payloadBytes(b.mData);
    }

    bool sameTiles(const std::vector<BakedTile>& a, const std::vector<BakedTile>& b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (!sameBakedTile(a[i], b[i]))
            {
                return false;
            }
        }
        return true;
    }

    TileBakeSettings albedoSettings(std::uint32_t albedoSize)
    {
        TileBakeSettings settings;
        settings.mLevel = 1;
        settings.mGridSize = 33;
        settings.mAlbedoSize = albedoSize;
        return settings;
    }

    BakedTile bakeWithAlbedo(const LandSource& land, const AlbedoSource* albedo, std::int32_t cellX, std::int32_t cellY,
        const TileBakeSettings& settings)
    {
        BakedTile tile;
        tile.mId = levelTile(settings.mLevel, cellX, cellY);
        const LodError error = bakeTerrainTile(land, albedo, tile.mId, settings, tile);
        EXPECT_TRUE(error.ok()) << error.describe();
        return tile;
    }

    // ---- one tile with an albedo -----------------------------------------------------------------------------------

    TEST(LodTileBakeLevels, a_tile_gets_the_baked_picture_and_it_survives_writing_and_parsing)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const BakedTile baked = bakeWithAlbedo(land, &albedo, -2, 4, albedoSettings(64));
        const TerrainTile& tile = baked.mData;

        EXPECT_TRUE(tile.mHasAlbedo);
        EXPECT_EQ(tile.mAlbedoWidth, 64u);
        EXPECT_EQ(tile.mAlbedoHeight, 64u);
        EXPECT_EQ(tile.mAlbedoFormat, AlbedoFormat::Rgba8);
        EXPECT_EQ(tile.mAlbedoMipCount, 1u);
        EXPECT_EQ(tile.mAlbedo.size(), 64u * 64u * 4u);

        // It is the picture of the tile's own square: 2 x 2 cells from the tile's south-west cell.
        std::int32_t originX = 0;
        std::int32_t originY = 0;
        tileOriginCell(baked.mId, originX, originY);
        Bytes direct;
        const LodError bakeError = bakeAlbedoImage(albedo, originX, originY, 2, 64, direct);
        ASSERT_TRUE(bakeError.ok()) << bakeError.describe();
        EXPECT_EQ(tile.mAlbedo, direct);

        std::vector<std::uint8_t> bytes;
        const LodError written = writeTerrainTile(tile, bytes);
        ASSERT_TRUE(written.ok()) << written.describe();
        TerrainTile parsed;
        const LodError parseError = parseTerrainTile(bytes, parsed);
        ASSERT_TRUE(parseError.ok()) << parseError.describe();
        EXPECT_TRUE(parsed.mHasAlbedo);
        EXPECT_EQ(parsed.mAlbedoWidth, 64u);
        EXPECT_EQ(parsed.mAlbedoHeight, 64u);
        EXPECT_EQ(parsed.mAlbedoMipCount, 1u);
        EXPECT_EQ(parsed.mAlbedo, tile.mAlbedo);
        EXPECT_EQ(parsed.mHeights, tile.mHeights);
        EXPECT_EQ(payloadBytes(parsed), bytes);
    }

    TEST(LodTileBakeLevels, without_a_source_or_a_size_the_tile_is_the_four_argument_bake)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const TileId id = levelTile(1, 0, 0);

        TileBakeSettings withSize = albedoSettings(64);
        BakedTile plain;
        plain.mId = id;
        ASSERT_TRUE(bakeTerrainTile(land, id, withSize, plain).ok());
        BakedTile noSource;
        noSource.mId = id;
        const LodError first = bakeTerrainTile(land, nullptr, id, withSize, noSource);
        ASSERT_TRUE(first.ok()) << first.describe();
        EXPECT_FALSE(noSource.mData.mHasAlbedo);
        EXPECT_TRUE(noSource.mData.mAlbedo.empty());
        EXPECT_TRUE(sameBakedTile(plain, noSource));

        TileBakeSettings noSize = albedoSettings(0);
        BakedTile plainNoSize;
        plainNoSize.mId = id;
        ASSERT_TRUE(bakeTerrainTile(land, id, noSize, plainNoSize).ok());
        BakedTile noSizeWithSource;
        noSizeWithSource.mId = id;
        const LodError second = bakeTerrainTile(land, &albedo, id, noSize, noSizeWithSource);
        ASSERT_TRUE(second.ok()) << second.describe();
        EXPECT_FALSE(noSizeWithSource.mData.mHasAlbedo);
        EXPECT_TRUE(sameBakedTile(plainNoSize, noSizeWithSource));
    }

    TEST(LodTileBakeLevels, digest_only_has_the_digest_of_the_full_bake_and_no_picture)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const BakedTile full = bakeWithAlbedo(land, &albedo, 0, 0, albedoSettings(32));
        ASSERT_TRUE(full.mData.mHasAlbedo);

        TileBakeSettings digestSettings = albedoSettings(32);
        digestSettings.mDigestOnly = true;
        const BakedTile digestOnly = bakeWithAlbedo(land, &albedo, 0, 0, digestSettings);
        EXPECT_TRUE(digestOnly.mData.mAlbedo.empty());
        EXPECT_EQ(digestOnly.mSourceDigest, full.mSourceDigest);
        EXPECT_EQ(digestOnly.mId, full.mId);

        // The albedo inputs are part of the digest: a tile without them has another one.
        BakedTile plain;
        plain.mId = full.mId;
        ASSERT_TRUE(bakeTerrainTile(land, full.mId, albedoSettings(0), plain).ok());
        EXPECT_NE(plain.mSourceDigest, full.mSourceDigest);
    }

    Hash digestOnlyDigest(const LandSource& land, const AlbedoSource& albedo, std::uint32_t albedoSize)
    {
        TileBakeSettings settings = albedoSettings(albedoSize);
        settings.mDigestOnly = true;
        return bakeWithAlbedo(land, &albedo, 0, 0, settings).mSourceDigest;
    }

    TEST(LodTileBakeLevels, the_digest_follows_the_textures_blend_map_and_vertex_colours_the_tile_uses)
    {
        const HillLand land;
        const HashedAlbedo baseline;
        const Hash baselineDigest = digestOnlyDigest(land, baseline, 16);
        EXPECT_EQ(baselineDigest, digestOnlyDigest(land, HashedAlbedo(), 16));

        HashedAlbedo usedLayer;
        usedLayer.mDigests[0] = digestOf(200);
        EXPECT_NE(digestOnlyDigest(land, usedLayer, 16), baselineDigest);

        HashedAlbedo unusedLayer;
        unusedLayer.mDigests[2] = digestOf(201);
        EXPECT_EQ(digestOnlyDigest(land, unusedLayer, 16), baselineDigest);

        // Global blend texel (20, 3) and vertex (70, 100) both lie inside the tile at cell (0, 0) (32 texels, 129
        // vertices a side).
        HashedAlbedo blendByte;
        const std::uint64_t bits = mixBits(20, 3, 11);
        blendByte.mBlendOverrides[{ 20, 3, 0 }] = static_cast<std::uint8_t>(1 - (bits & 1));
        EXPECT_NE(digestOnlyDigest(land, blendByte, 16), baselineDigest);

        HashedAlbedo vertexByte;
        vertexByte.mVertexOverrides[{ 70, 100, 2 }]
            = static_cast<std::uint8_t>((((mixBits(70, 100, 22) >> 16) & 127) + 128) ^ 1u);
        EXPECT_NE(digestOnlyDigest(land, vertexByte, 16), baselineDigest);

        EXPECT_NE(digestOnlyDigest(land, baseline, 32), baselineDigest);
    }

    TEST(LodTileBakeLevels, a_bad_albedo_size_is_an_error)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        for (const std::uint32_t size : { 2u, 3u, 5u, 100u, 4096u })
        {
            BakedTile tile;
            const LodError error = bakeTerrainTile(land, &albedo, levelTile(1, 0, 0), albedoSettings(size), tile);
            EXPECT_FALSE(error.ok()) << size;
            EXPECT_FALSE(error.mField.empty()) << size;
        }
    }

    // ---- several levels --------------------------------------------------------------------------------------------

    std::vector<Cell> landCells()
    {
        std::vector<Cell> cells;
        for (std::int32_t x = -3; x <= 1; ++x)
        {
            for (std::int32_t y = -2; y <= 1; ++y)
            {
                cells.emplace_back(x, y);
            }
        }
        return cells;
    }

    TerrainBakePlan thePlan()
    {
        TerrainBakePlan plan;
        plan.mLevels = { 1, 2, 3 };
        plan.mAlbedoFromLevel = 2;
        plan.mAlbedoSize = planAlbedoSize;
        return plan;
    }

    struct LevelsBake
    {
        TileManifest mManifest;
        std::vector<BakedTile> mTiles;
        LodError mError;
    };

    LevelsBake bakeLevels(const LandSource& land, const AlbedoSource* albedo, const std::vector<Cell>& cells,
        const TileBakeSettings& base = TileBakeSettings{}, const TerrainBakePlan& plan = thePlan())
    {
        LevelsBake result;
        result.mError = bakeTerrainLevels(land, albedo, cells, base, plan, "Testland", result.mManifest, result.mTiles);
        EXPECT_TRUE(result.mError.ok()) << result.mError.describe();
        return result;
    }

    std::uint32_t levelOf(const BakedTile& tile)
    {
        return tile.mId.mLevel;
    }

    TEST(LodTileBakeLevels, the_manifest_and_the_tiles_of_every_level)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const LevelsBake bake = bakeLevels(land, &albedo, landCells());

        EXPECT_EQ(bake.mManifest.mLevels, (std::vector<std::uint32_t>{ 1, 2, 3 }));
        EXPECT_EQ(bake.mManifest.mGridSize, 33u);
        EXPECT_EQ(bake.mManifest.mAlbedoSize, planAlbedoSize);
        EXPECT_EQ(bake.mManifest.mBakerVersion, bakerVersion);
        EXPECT_EQ(bake.mManifest.mLayoutVersion, tileKeyLayoutVersion);
        ASSERT_EQ(bake.mManifest.mWorldspaces.size(), 1u);
        EXPECT_EQ(bake.mManifest.mWorldspaces[0].mName, "Testland");

        // Tiles come back in key order, and they are the tiles covering the cells at each of the three levels.
        for (std::size_t i = 1; i < bake.mTiles.size(); ++i)
        {
            EXPECT_LT(packTileKey(bake.mTiles[i - 1].mId).value(), packTileKey(bake.mTiles[i].mId).value()) << i;
        }
        std::set<std::uint64_t> expectedKeys;
        std::size_t perLevel[4] = { 0, 0, 0, 0 };
        for (std::uint32_t level = 1; level <= 3; ++level)
        {
            TileBakeSettings settings;
            settings.mLevel = level;
            const std::vector<TileId> covering = tilesCoveringCells(landCells(), settings);
            perLevel[level] = covering.size();
            for (const TileId& id : covering)
            {
                expectedKeys.insert(packTileKey(id).value());
            }
        }
        EXPECT_EQ(perLevel[1], 6u);
        EXPECT_EQ(perLevel[2], 4u);
        EXPECT_EQ(perLevel[3], 4u);
        std::set<std::uint64_t> keys;
        for (const BakedTile& tile : bake.mTiles)
        {
            keys.insert(packTileKey(tile.mId).value());
        }
        EXPECT_EQ(keys, expectedKeys);
        EXPECT_EQ(bake.mTiles.size(), 14u);

        // Level 1 uses the live blend maps, levels 2 and 3 carry the picture.
        for (const BakedTile& tile : bake.mTiles)
        {
            const std::uint32_t level = levelOf(tile);
            EXPECT_EQ(tile.mData.mLevel, level);
            EXPECT_EQ(tile.mData.mGridSize, 33u);
            if (level == 1)
            {
                EXPECT_FALSE(tile.mData.mHasAlbedo);
                EXPECT_TRUE(tile.mData.mAlbedo.empty());
            }
            else
            {
                EXPECT_TRUE(tile.mData.mHasAlbedo) << level;
                EXPECT_EQ(tile.mData.mAlbedoWidth, planAlbedoSize);
                EXPECT_EQ(tile.mData.mAlbedoHeight, planAlbedoSize);
                EXPECT_EQ(tile.mData.mAlbedoMipCount, 1u);
                EXPECT_EQ(tile.mData.mAlbedo.size(), static_cast<std::size_t>(planAlbedoSize) * planAlbedoSize * 4);
            }
        }
    }

    TEST(LodTileBakeLevels, a_tile_of_a_level_equals_the_single_tile_bake_at_that_level)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const LevelsBake bake = bakeLevels(land, &albedo, landCells());

        std::size_t checked = 0;
        for (const BakedTile& tile : bake.mTiles)
        {
            if (levelOf(tile) != 2)
            {
                continue;
            }
            std::int32_t originX = 0;
            std::int32_t originY = 0;
            tileOriginCell(tile.mId, originX, originY);

            TileBakeSettings settings;
            settings.mLevel = 2;
            BakedTile heightsOnly;
            heightsOnly.mId = tile.mId;
            ASSERT_TRUE(bakeTerrainTile(land, tile.mId, settings, heightsOnly).ok());
            EXPECT_EQ(tile.mData.mHeights, heightsOnly.mData.mHeights);
            EXPECT_EQ(tile.mData.mNormals, heightsOnly.mData.mNormals);
            EXPECT_EQ(tile.mData.mMinHeight, heightsOnly.mData.mMinHeight);
            EXPECT_EQ(tile.mData.mMaxHeight, heightsOnly.mData.mMaxHeight);
            EXPECT_EQ(tile.mData.mGeometricError, heightsOnly.mData.mGeometricError);

            settings.mAlbedoSize = planAlbedoSize;
            const BakedTile single = bakeWithAlbedo(land, &albedo, originX, originY, settings);
            EXPECT_EQ(payloadBytes(tile.mData), payloadBytes(single.mData));
            ++checked;
        }
        EXPECT_EQ(checked, 4u);
    }

    TEST(LodTileBakeLevels, without_an_albedo_source_no_tile_has_a_picture_and_the_manifest_says_zero)
    {
        const HillLand land;
        const LevelsBake bake = bakeLevels(land, nullptr, landCells());
        EXPECT_EQ(bake.mManifest.mAlbedoSize, 0u);
        EXPECT_EQ(bake.mManifest.mLevels, (std::vector<std::uint32_t>{ 1, 2, 3 }));
        EXPECT_EQ(bake.mTiles.size(), 14u);
        for (const BakedTile& tile : bake.mTiles)
        {
            EXPECT_FALSE(tile.mData.mHasAlbedo) << levelOf(tile);
            EXPECT_TRUE(tile.mData.mAlbedo.empty());
        }
    }

    TEST(LodTileBakeLevels, the_result_does_not_depend_on_the_order_of_the_land_cells)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const LevelsBake first = bakeLevels(land, &albedo, landCells());

        std::vector<Cell> shuffled = landCells();
        shuffled.insert(shuffled.end(), shuffled.begin(), shuffled.begin() + 6); // duplicates
        std::mt19937 engine(4242);
        std::shuffle(shuffled.begin(), shuffled.end(), engine);
        const LevelsBake second = bakeLevels(land, &albedo, shuffled);

        EXPECT_EQ(first.mManifest, second.mManifest);
        EXPECT_TRUE(sameTiles(first.mTiles, second.mTiles));
        EXPECT_EQ(contentDigest(first.mManifest, first.mTiles), contentDigest(second.mManifest, second.mTiles));
    }

    TEST(LodTileBakeLevels, digest_only_gives_the_digests_of_the_full_bake_tile_by_tile)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const LevelsBake full = bakeLevels(land, &albedo, landCells());

        TileBakeSettings base;
        base.mDigestOnly = true;
        const LevelsBake digests = bakeLevels(land, &albedo, landCells(), base);
        EXPECT_EQ(digests.mManifest, full.mManifest);
        ASSERT_EQ(digests.mTiles.size(), full.mTiles.size());
        for (std::size_t i = 0; i < full.mTiles.size(); ++i)
        {
            EXPECT_EQ(digests.mTiles[i].mId, full.mTiles[i].mId) << i;
            EXPECT_EQ(digests.mTiles[i].mSourceDigest, full.mTiles[i].mSourceDigest) << i;
            EXPECT_TRUE(digests.mTiles[i].mData.mAlbedo.empty()) << i;
        }
        EXPECT_EQ(contentDigest(digests.mManifest, digests.mTiles), contentDigest(full.mManifest, full.mTiles));
    }

    TEST(LodTileBakeLevels, the_content_digest_follows_the_albedo_inputs)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const LevelsBake baseline = bakeLevels(land, &albedo, landCells());

        HashedAlbedo changed;
        changed.mDigests[1] = digestOf(99); // a used layer
        const LevelsBake other = bakeLevels(land, &changed, landCells());
        EXPECT_NE(contentDigest(baseline.mManifest, baseline.mTiles), contentDigest(other.mManifest, other.mTiles));
    }

    std::vector<std::uint8_t> packOf(const LevelsBake& bake, bool reversed)
    {
        std::vector<BakedTile> tiles = bake.mTiles;
        if (reversed)
        {
            std::reverse(tiles.begin(), tiles.end());
        }
        TilePackOptions options;
        options.mGeneration = 1;
        options.mSourceDigest = contentDigest(bake.mManifest, bake.mTiles);
        options.mStoreOnly = true;
        std::vector<std::uint8_t> bytes;
        const LodError error = buildTilePack(bake.mManifest, tiles, options, bytes);
        EXPECT_TRUE(error.ok()) << error.describe();
        return bytes;
    }

    TEST(LodTileBakeLevels, the_pack_is_byte_identical_for_any_tile_order_and_reads_every_tile_back)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const LevelsBake bake = bakeLevels(land, &albedo, landCells());

        const std::vector<std::uint8_t> forward = packOf(bake, false);
        const std::vector<std::uint8_t> reversed = packOf(bake, true);
        EXPECT_FALSE(forward.empty());
        EXPECT_EQ(forward, reversed);

        LodError error;
        const std::unique_ptr<TilePack> pack = TilePack::open(EdenArchive::makeMemorySource(forward), error);
        ASSERT_NE(pack, nullptr) << error.describe();
        EXPECT_EQ(pack->manifest(), bake.mManifest);
        ASSERT_EQ(pack->tileIds().size(), bake.mTiles.size());
        for (std::size_t i = 0; i < bake.mTiles.size(); ++i)
        {
            EXPECT_EQ(pack->tileIds()[i], bake.mTiles[i].mId) << i;
            TerrainTile back;
            const LodError readError = pack->readTile(bake.mTiles[i].mId, back);
            ASSERT_TRUE(readError.ok()) << readError.describe();
            EXPECT_EQ(payloadBytes(back), payloadBytes(bake.mTiles[i].mData)) << i;
        }
    }

    // ---- invalid plans ---------------------------------------------------------------------------------------------

    TEST(LodTileBakeLevels, a_level_that_does_not_fit_the_grid_size_is_an_error)
    {
        const HillLand land;
        const HashedAlbedo albedo;
        const std::vector<Cell> oneCell = { { 0, 0 } };

        // 129 vertices a side needs a vertex every half unit of a cell's 64 intervals at level 0: not a whole step.
        TileBakeSettings fineGrid;
        fineGrid.mGridSize = 129;
        TerrainBakePlan levelZero = thePlan();
        levelZero.mLevels = { 0, 1 };
        TileManifest manifest;
        std::vector<BakedTile> tiles;
        LodError error = bakeTerrainLevels(land, &albedo, oneCell, fineGrid, levelZero, "Testland", manifest, tiles);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());

        // A grid size that is no power of two step at any level.
        TileBakeSettings oddGrid;
        oddGrid.mGridSize = 34;
        error = bakeTerrainLevels(land, &albedo, oneCell, oddGrid, thePlan(), "Testland", manifest, tiles);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());

        // A level above the highest one a key can hold.
        TerrainBakePlan tooHigh = thePlan();
        tooHigh.mLevels = { 1, 16 };
        error = bakeTerrainLevels(land, &albedo, oneCell, TileBakeSettings{}, tooHigh, "Testland", manifest, tiles);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());

        // A picture size no tile can have.
        TerrainBakePlan badPicture = thePlan();
        badPicture.mAlbedoSize = 3;
        error = bakeTerrainLevels(land, &albedo, oneCell, TileBakeSettings{}, badPicture, "Testland", manifest, tiles);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
    }
}
