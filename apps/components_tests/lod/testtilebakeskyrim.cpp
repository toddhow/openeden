// Task 0133 (milestone M-S2): the terrain tile baker over land with 33 vertices a cell (Skyrim SE) instead of
// Morrowind's 65. Pure code over a generated LandSource; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/albedobake.hpp>
#include <components/lod/tilebake.hpp>
#include <components/lod/tilepack.hpp>

#include <edenarchive/hash.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    using namespace Lod;

    // Height of the generated land at a GLOBAL vertex (cell * intervals + local index), so shared edges agree.
    float landHeight(std::int64_t gx, std::int64_t gy)
    {
        return static_cast<float>(3 * gx + (gy % 5) - 7 * (gy / 3));
    }

    class SmallCellLand final : public LandSource
    {
    public:
        explicit SmallCellLand(std::int64_t intervals)
            : mIntervals(intervals)
        {
        }

        void sample(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells, std::uint32_t lod,
            std::vector<float>& heights, std::vector<float>& normals) const override
        {
            const std::size_t n = (static_cast<std::size_t>(mIntervals) * sizeInCells >> lod) + 1;
            const std::int64_t step = std::int64_t{ 1 } << lod;
            heights.assign(n * n, 0.f);
            normals.assign(3 * n * n, 0.f);
            for (std::size_t j = 0; j < n; ++j)
            {
                for (std::size_t i = 0; i < n; ++i)
                {
                    const std::int64_t gx = cellX * mIntervals + static_cast<std::int64_t>(i) * step;
                    const std::int64_t gy = cellY * mIntervals + static_cast<std::int64_t>(j) * step;
                    heights[j * n + i] = landHeight(gx, gy);
                    normals[3 * (j * n + i) + 2] = 1.f;
                }
            }
        }

    private:
        std::int64_t mIntervals;
    };

    TileBakeSettings skyrimSettings(std::uint32_t level)
    {
        TileBakeSettings settings;
        settings.mLevel = level;
        settings.mGridSize = 33;
        settings.mCellSize = 4096;
        settings.mVerticesPerCell = 33;
        return settings;
    }

    float decodedHeight(const TerrainTile& tile, std::size_t i, std::size_t j)
    {
        const double range = static_cast<double>(tile.mMaxHeight) - tile.mMinHeight;
        return static_cast<float>(tile.mMinHeight + tile.mHeights[j * tile.mGridSize + i] / 65535.0 * range);
    }

    TEST(TileBakeSmallCells, the_default_is_morrowinds_sixty_five_vertices)
    {
        EXPECT_EQ(TileBakeSettings{}.mVerticesPerCell, 65u);
    }

    TEST(TileBakeSmallCells, the_vertex_step_follows_the_vertices_of_a_cell)
    {
        std::uint32_t lod = 99;
        // Morrowind: 64 intervals a cell, 2 cells and 32 intervals a side: every 4th vertex, lod 2 (unchanged).
        TileBakeSettings morrowind;
        morrowind.mLevel = 1;
        ASSERT_TRUE(vertexStepLod(morrowind, lod));
        EXPECT_EQ(lod, 2u);
        // Skyrim SE: 32 intervals a cell: 2 cells are 64 intervals, every 2nd vertex.
        ASSERT_TRUE(vertexStepLod(skyrimSettings(1), lod));
        EXPECT_EQ(lod, 1u);
        ASSERT_TRUE(vertexStepLod(skyrimSettings(2), lod));
        EXPECT_EQ(lod, 2u);
        ASSERT_TRUE(vertexStepLod(skyrimSettings(6), lod));
        EXPECT_EQ(lod, 6u);
    }

    TEST(TileBakeSmallCells, a_cell_that_is_not_a_power_of_two_intervals_is_refused)
    {
        std::uint32_t lod = 0;
        TileBakeSettings settings = skyrimSettings(1);
        settings.mVerticesPerCell = 34; // 33 intervals
        EXPECT_FALSE(vertexStepLod(settings, lod));
        settings.mVerticesPerCell = 0;
        EXPECT_FALSE(vertexStepLod(settings, lod));
        settings.mVerticesPerCell = 1;
        EXPECT_FALSE(vertexStepLod(settings, lod));
    }

    TEST(TileBakeSmallCells, a_level_one_tile_holds_the_heights_of_every_second_vertex)
    {
        const SmallCellLand land(32);
        BakedTile tile;
        const TileId id = *tileContainingCell(0, 1, 6, 10); // the level 1 tile whose south-west cell is (6, 10)
        ASSERT_FALSE(bakeTerrainTile(land, id, skyrimSettings(1), tile));
        EXPECT_EQ(tile.mData.mGridSize, 33u);
        EXPECT_FLOAT_EQ(tile.mData.mSide, 2.f * 4096.f);
        EXPECT_FLOAT_EQ(tile.mData.mOriginX, 6.f * 4096.f);
        EXPECT_FLOAT_EQ(tile.mData.mOriginY, 10.f * 4096.f);
        const double range = static_cast<double>(tile.mData.mMaxHeight) - tile.mData.mMinHeight;
        for (std::size_t j = 0; j < 33; ++j)
        {
            for (std::size_t i = 0; i < 33; ++i)
            {
                const float expected
                    = landHeight(6 * 32 + 2 * static_cast<std::int64_t>(i), 10 * 32 + 2 * static_cast<std::int64_t>(j));
                EXPECT_NEAR(decodedHeight(tile.mData, i, j), expected, range / 65535.0 + 1e-3) << i << "," << j;
            }
        }
    }

    TEST(TileBakeSmallCells, a_level_six_tile_reads_the_whole_land_it_covers)
    {
        const SmallCellLand land(32);
        BakedTile tile;
        const TileId id = *tileContainingCell(0, 6, 0, 0);
        ASSERT_FALSE(bakeTerrainTile(land, id, skyrimSettings(6), tile));
        EXPECT_FLOAT_EQ(tile.mData.mSide, 64.f * 4096.f);
        // The level 6 tile holding cell (0, 0) starts at a multiple of 64 cells; its first sample is the land there.
        const std::int64_t first = static_cast<std::int64_t>(std::lround(tile.mData.mOriginX / 4096.f)) * 32;
        // Every 64th vertex of a 2048-interval side: corner and the next sample.
        EXPECT_NEAR(decodedHeight(tile.mData, 1, 0),
            landHeight(first + 64, static_cast<std::int64_t>(std::lround(tile.mData.mOriginY / 4096.f)) * 32),
            (static_cast<double>(tile.mData.mMaxHeight) - tile.mData.mMinHeight) / 65535.0 + 1e-3);
    }

    TEST(TileBakeSmallCells, a_source_with_the_wrong_cell_resolution_is_refused)
    {
        const SmallCellLand morrowindLand(64); // 65 vertices a cell, settings say 33
        BakedTile tile;
        EXPECT_TRUE(bool(bakeTerrainTile(morrowindLand, TileId{ 0, 1, 0, 0 }, skyrimSettings(1), tile)));
    }

    TEST(TileBakeSmallCells, the_parameters_and_digest_tell_the_resolutions_apart_and_keep_morrowinds_as_before)
    {
        const SmallCellLand land33(32);
        const SmallCellLand land65(64);
        BakedTile skyrim;
        BakedTile morrowind;
        ASSERT_FALSE(bakeTerrainTile(land33, TileId{ 0, 1, 0, 0 }, skyrimSettings(1), skyrim));
        TileBakeSettings m;
        m.mLevel = 1;
        ASSERT_FALSE(bakeTerrainTile(land65, TileId{ 0, 1, 0, 0 }, m, morrowind));
        EXPECT_NE(skyrim.mSourceDigest, morrowind.mSourceDigest);
        // Morrowind's parameters are the five words they always were (20 bytes); a smaller cell adds one.
        EXPECT_EQ(morrowind.mParameters.size(), 20u);
        EXPECT_EQ(skyrim.mParameters.size(), 24u);
        // The same Skyrim bake twice is the same bytes.
        BakedTile again;
        ASSERT_FALSE(bakeTerrainTile(land33, TileId{ 0, 1, 0, 0 }, skyrimSettings(1), again));
        EXPECT_EQ(again.mSourceDigest, skyrim.mSourceDigest);
        EXPECT_EQ(again.mParameters, skyrim.mParameters);
    }

    TEST(TileBakeSmallCells, the_corner_normals_skipped_by_the_digest_are_the_cells_corners)
    {
        // A different last bit of a normal at a cell corner (every 32nd vertex) must not change the digest, one at any
        // other vertex must (the rule that makes a Windows and a Linux bake agree, at the resolution of the cell).
        class NoisyLand final : public LandSource
        {
        public:
            NoisyLand(std::int32_t noiseI, std::int32_t noiseJ)
                : mI(noiseI)
                , mJ(noiseJ)
                , mBase(32)
            {
            }
            void sample(std::int32_t cx, std::int32_t cy, std::uint32_t size, std::uint32_t lod,
                std::vector<float>& heights, std::vector<float>& normals) const override
            {
                mBase.sample(cx, cy, size, lod, heights, normals);
                if (lod == 0 && mI >= 0)
                {
                    const std::size_t n = (32u * size) + 1;
                    normals[3 * (static_cast<std::size_t>(mJ) * n + static_cast<std::size_t>(mI))] = 1e-3f;
                }
            }

        private:
            std::int32_t mI, mJ;
            SmallCellLand mBase;
        };
        BakedTile plain;
        BakedTile corner;
        BakedTile inside;
        ASSERT_FALSE(bakeTerrainTile(NoisyLand(-1, -1), TileId{ 0, 1, 0, 0 }, skyrimSettings(1), plain));
        ASSERT_FALSE(bakeTerrainTile(NoisyLand(32, 32), TileId{ 0, 1, 0, 0 }, skyrimSettings(1), corner));
        ASSERT_FALSE(bakeTerrainTile(NoisyLand(33, 32), TileId{ 0, 1, 0, 0 }, skyrimSettings(1), inside));
        EXPECT_EQ(plain.mSourceDigest, corner.mSourceDigest);
        EXPECT_NE(plain.mSourceDigest, inside.mSourceDigest);
    }

    TEST(TileBakeSmallCells, all_six_levels_bake_and_the_manifest_names_the_cell_size)
    {
        const SmallCellLand land(32);
        TileBakeSettings base = skyrimSettings(1);
        const TerrainBakePlan plan;
        TileManifest manifest;
        std::vector<BakedTile> tiles;
        ASSERT_FALSE(bakeTerrainLevels(
            land, nullptr, { { 0, 0 }, { 1, 0 }, { 5, 7 } }, base, plan, "skyrimse:X:y", manifest, tiles));
        ASSERT_EQ(manifest.mWorldspaces.size(), 1u);
        EXPECT_EQ(manifest.mWorldspaces[0].mName, "skyrimse:X:y");
        EXPECT_EQ(manifest.mWorldspaces[0].mCellSize, 4096u);
        EXPECT_EQ(manifest.mLevels, plan.mLevels);
        EXPECT_FALSE(tiles.empty());
    }

    std::string hexOf(const std::vector<std::uint8_t>& bytes)
    {
        static const char digits[] = "0123456789abcdef";
        std::string text;
        for (const std::uint8_t b : bytes)
        {
            text += digits[b >> 4];
            text += digits[b & 15];
        }
        return text;
    }

    // Morrowind's bake with the default settings must stay exactly what it was before mVerticesPerCell existed: the
    // pinned values below were recorded by the code BEFORE the change (task 0133), on this generated land, and must
    // never be edited to make this test pass.
    TEST(TileBakeMorrowindUnchanged, a_default_bake_has_the_digest_parameters_and_manifest_it_always_had)
    {
        const SmallCellLand land(64);
        TileBakeSettings m;
        m.mLevel = 1;
        BakedTile tile;
        ASSERT_FALSE(bakeTerrainTile(land, TileId{ 0, 1, 0, 0 }, m, tile));
        EXPECT_EQ(
            EdenArchive::toHex(tile.mSourceDigest), "a571d8f8a4bb70032fbbed248ea57de4c28b95e3b42b7fe80c26ca903888c606");
        EXPECT_EQ(hexOf(tile.mParameters), "0200000001000000210000000020000000000000");

        const TerrainBakePlan plan{ { 1, 2 }, 2, 256 };
        TileManifest manifest;
        std::vector<BakedTile> tiles;
        ASSERT_FALSE(
            bakeTerrainLevels(land, nullptr, { { 0, 0 }, { 1, 1 }, { 3, 2 } }, m, plan, "Morrowind", manifest, tiles));
        EXPECT_EQ(writeManifestJson(manifest),
            "{\"albedoSize\":0,\"bakerVersion\":2,\"gridSize\":33,\"layoutVersion\":1,\"levels\":[1,2],\"worldspaces\":"
            "[{\"cellSize\":8192,\"index\":0,\"name\":\"Morrowind\"}]}");
        EXPECT_EQ(EdenArchive::toHex(contentDigest(manifest, tiles)),
            "46844f588232fe7d968b7fa8b312eb895aa1c9ecd4dc4fd92003e9bb46ab61ed");
        EXPECT_EQ(tiles.size(), 3u);
    }

    // A ground made of one flat grey layer, with vertex colours sized for `verticesPerCell` (the cell's resolution).
    class FlatGround final : public AlbedoSource
    {
    public:
        explicit FlatGround(std::uint32_t verticesPerCell)
            : mVerts(verticesPerCell)
        {
            mImage.mWidth = 8;
            mImage.mHeight = 8;
            mImage.mPixels.assign(8 * 8 * 4, 100);
        }
        std::size_t layerCount() const override { return 1; }
        const Rgba8Image* layerImage(std::size_t) const override { return &mImage; }
        EdenArchive::Hash layerDigest(std::size_t) const override { return {}; }
        void blendMap(std::int32_t, std::int32_t, std::uint32_t size, std::vector<std::uint8_t>& out) const override
        {
            out.assign(std::size_t{ 16 } * size * 16 * size * 4, 0);
        }
        void vertexColours(
            std::int32_t, std::int32_t, std::uint32_t size, std::vector<std::uint8_t>& out) const override
        {
            const std::size_t n = std::size_t{ mVerts - 1 } * size + 1;
            out.assign(n * n * 3, 255);
        }

    private:
        std::uint32_t mVerts;
        Rgba8Image mImage;
    };

    TEST(TileBakeSmallCells, a_ground_picture_is_baked_from_vertex_colours_of_a_small_cell)
    {
        const SmallCellLand land(32);
        const FlatGround ground(33);
        TileBakeSettings settings = skyrimSettings(2);
        settings.mAlbedoSize = 64;
        BakedTile tile;
        ASSERT_FALSE(bakeTerrainTile(land, &ground, *tileContainingCell(0, 2, 0, 0), settings, tile));
        ASSERT_TRUE(tile.mData.mHasAlbedo);
        ASSERT_EQ(tile.mData.mAlbedo.size(), 64u * 64u * 4u);
        EXPECT_NEAR(tile.mData.mAlbedo[(32 * 64 + 32) * 4], 100, 1);
    }

    TEST(TileBakeSmallCells, vertex_colours_of_the_wrong_resolution_are_refused)
    {
        const SmallCellLand land(32);
        const FlatGround morrowindSized(65);
        TileBakeSettings settings = skyrimSettings(2);
        settings.mAlbedoSize = 64;
        BakedTile tile;
        const LodError error = bakeTerrainTile(land, &morrowindSized, *tileContainingCell(0, 2, 0, 0), settings, tile);
        EXPECT_TRUE(bool(error));
        EXPECT_EQ(error.mField, "vertexColours");
    }

    TEST(TileBakeSmallCells, the_albedo_inputs_digest_follows_the_resolution)
    {
        const FlatGround ground33(33);
        EdenArchive::Hash digest;
        EXPECT_FALSE(bool(albedoInputsDigest(ground33, 0, 0, 2, 64, digest, nullptr, 33)));
        EXPECT_TRUE(bool(albedoInputsDigest(ground33, 0, 0, 2, 64, digest, nullptr, 65)));
    }
}
