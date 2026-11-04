// Task 0055 (stage S1c): the terrain tile baker. Pure code over a generated LandSource; no GPU, window or game data.
#include <gtest/gtest.h>

#include <components/lod/tilebake.hpp>

#include <edenarchive/source.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
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

    constexpr float defaultHeight = -2048.f;
    constexpr std::int64_t verticesIntervals = 64; // intervals per cell
    constexpr std::int64_t coarseStep = 4; // level 1, N 33: every 4th vertex
    constexpr double pi = 3.14159265358979323846;

    std::int64_t cellOf(std::int64_t g)
    {
        return g >= 0 ? g / verticesIntervals : -((-g + verticesIntervals - 1) / verticesIntervals);
    }

    // Land generated from a function of the GLOBAL vertex coordinates (cell * 64 + local index), so that the shared
    // edge vertices of neighbouring cells agree.
    class GridLand final : public LandSource
    {
    public:
        using HeightFn = std::function<float(std::int64_t, std::int64_t)>;

        explicit GridLand(HeightFn fn)
            : mFn(std::move(fn))
        {
        }

        // Every cell has land unless a cell list is given.
        void setLandCells(const std::vector<Cell>& cells)
        {
            mAllLand = false;
            mLandCells = std::set<Cell>(cells.begin(), cells.end());
        }

        void addOffset(std::int64_t gx, std::int64_t gy, float offset) { mOffsets[{ gx, gy }] = offset; }

        // The last bit of the normal's x at this vertex differs (one float step up), as the engine's average at a cell
        // corner does between compilers that fuse multiply-adds and those that do not.
        void addNormalNoise(std::int64_t gx, std::int64_t gy) { mNormalNoise.insert({ gx, gy }); }

        float height(std::int64_t gx, std::int64_t gy) const
        {
            if (!vertexHasLand(gx, gy))
            {
                return defaultHeight;
            }
            float value = mFn(gx, gy);
            const auto found = mOffsets.find({ gx, gy });
            if (found != mOffsets.end())
            {
                value += found->second;
            }
            return value;
        }

        void normal(std::int64_t gx, std::int64_t gy, float& x, float& y, float& z) const
        {
            if (!vertexHasLand(gx, gy))
            {
                x = 0.f;
                y = 0.f;
                z = 1.f;
                return;
            }
            const double nx = 0.4 * std::sin(0.03 * static_cast<double>(gx));
            const double ny = 0.4 * std::cos(0.02 * static_cast<double>(gy));
            const double length = std::sqrt(nx * nx + ny * ny + 1.0);
            x = static_cast<float>(nx / length);
            y = static_cast<float>(ny / length);
            z = static_cast<float>(1.0 / length);
        }

        void sample(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells, std::uint32_t lod,
            std::vector<float>& heights, std::vector<float>& normals) const override
        {
            const std::size_t n = (static_cast<std::size_t>(verticesIntervals) * sizeInCells >> lod) + 1;
            const std::int64_t step = std::int64_t{ 1 } << lod;
            heights.assign(n * n, 0.f);
            normals.assign(3 * n * n, 0.f);
            for (std::size_t j = 0; j < n; ++j)
            {
                for (std::size_t i = 0; i < n; ++i)
                {
                    const std::int64_t gx = cellX * verticesIntervals + static_cast<std::int64_t>(i) * step;
                    const std::int64_t gy = cellY * verticesIntervals + static_cast<std::int64_t>(j) * step;
                    const std::size_t index = j * n + i;
                    heights[index] = height(gx, gy);
                    normal(gx, gy, normals[3 * index], normals[3 * index + 1], normals[3 * index + 2]);
                    if (mNormalNoise.count({ gx, gy }) != 0)
                        normals[3 * index] = std::nextafter(normals[3 * index], 2.f);
                }
            }
        }

    private:
        // A vertex has land when any of the (up to four) cells that touch it has land.
        bool vertexHasLand(std::int64_t gx, std::int64_t gy) const
        {
            if (mAllLand)
            {
                return true;
            }
            for (std::int64_t dy = 0; dy <= 1; ++dy)
            {
                for (std::int64_t dx = 0; dx <= 1; ++dx)
                {
                    const Cell cell{ static_cast<std::int32_t>(cellOf(gx - dx)),
                        static_cast<std::int32_t>(cellOf(gy - dy)) };
                    if (mLandCells.count(cell) != 0)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        HeightFn mFn;
        bool mAllLand = true;
        std::set<Cell> mLandCells;
        std::map<std::pair<std::int64_t, std::int64_t>, float> mOffsets;
        std::set<std::pair<std::int64_t, std::int64_t>> mNormalNoise;
    };

    float hills(std::int64_t gx, std::int64_t gy)
    {
        const double x = static_cast<double>(gx);
        const double y = static_cast<double>(gy);
        return static_cast<float>(100.0 * std::sin(0.05 * x) + 50.0 * std::cos(0.07 * y) + 0.1 * x);
    }

    float ramp(std::int64_t gx, std::int64_t gy)
    {
        return static_cast<float>(10.0 + 2.0 * static_cast<double>(gx) + 3.0 * static_cast<double>(gy));
    }

    float flat(std::int64_t, std::int64_t)
    {
        return 100.f;
    }

    TileBakeSettings defaultSettings()
    {
        return TileBakeSettings{};
    }

    TileId levelOneTile(std::uint8_t worldspace, std::int32_t cellX, std::int32_t cellY)
    {
        return tileContainingCell(worldspace, 1, cellX, cellY).value();
    }

    // The quantisation step of a baked tile in world units.
    float quantStep(const TerrainTile& tile)
    {
        return (tile.mMaxHeight - tile.mMinHeight) / 65535.f;
    }

    float decodeHeight(const TerrainTile& tile, std::size_t index)
    {
        return tile.mMinHeight
            + static_cast<float>(tile.mHeights[index]) / 65535.f * (tile.mMaxHeight - tile.mMinHeight);
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

    BakedTile bakeOne(const LandSource& source, std::int32_t cellX, std::int32_t cellY,
        const TileBakeSettings& settings = defaultSettings())
    {
        BakedTile tile;
        tile.mId = levelOneTile(settings.mWorldspace, cellX, cellY);
        const LodError error = bakeTerrainTile(source, tile.mId, settings, tile);
        EXPECT_TRUE(error.ok()) << error.describe();
        return tile;
    }

    // ---- a small world --------------------------------------------------------------------------------------------

    std::vector<Cell> worldCells()
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

    struct WorldBake
    {
        TileManifest mManifest;
        std::vector<BakedTile> mTiles;
        LodError mError;
    };

    WorldBake bakeWorld(const LandSource& source, const std::vector<Cell>& cells,
        const TileBakeSettings& settings = defaultSettings(), const std::string& name = "Testland")
    {
        WorldBake result;
        result.mError = bakeTerrain(source, cells, settings, name, result.mManifest, result.mTiles);
        EXPECT_TRUE(result.mError.ok()) << result.mError.describe();
        return result;
    }

    std::vector<std::uint8_t> packBytes(const WorldBake& bake, bool reversed)
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

    // ---- (1) vertexStepLod -----------------------------------------------------------------------------------------

    TEST(LodTileBake, vertex_step_lod_values)
    {
        TileBakeSettings settings;
        std::uint32_t lod = 99;

        settings.mLevel = 1;
        settings.mGridSize = 33;
        EXPECT_TRUE(vertexStepLod(settings, lod));
        EXPECT_EQ(lod, 2u);

        settings.mGridSize = 65;
        EXPECT_TRUE(vertexStepLod(settings, lod));
        EXPECT_EQ(lod, 1u);

        settings.mGridSize = 34;
        EXPECT_FALSE(vertexStepLod(settings, lod));

        settings.mLevel = 2;
        settings.mGridSize = 33;
        EXPECT_TRUE(vertexStepLod(settings, lod));
        EXPECT_EQ(lod, 3u);
    }

    // ---- (2) normals -----------------------------------------------------------------------------------------------

    double angleBetween(const double (&a)[3], const float (&b)[3])
    {
        double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        dot = std::clamp(dot, -1.0, 1.0);
        return std::acos(dot);
    }

    constexpr double normalToleranceRadians = 2.0 * pi / 180.0;

    TEST(LodTileBake, normal_axis_vectors_round_trip)
    {
        const double axes[][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        for (const auto& axis : axes)
        {
            std::int8_t u = 0;
            std::int8_t v = 0;
            encodeNormal(static_cast<float>(axis[0]), static_cast<float>(axis[1]), static_cast<float>(axis[2]), u, v);
            float decoded[3] = { 0, 0, 0 };
            decodeNormal(u, v, decoded[0], decoded[1], decoded[2]);
            EXPECT_LE(angleBetween(axis, decoded), normalToleranceRadians)
                << axis[0] << ", " << axis[1] << ", " << axis[2];
        }
    }

    TEST(LodTileBake, normal_up_encodes_to_zero)
    {
        std::int8_t u = 55;
        std::int8_t v = 55;
        encodeNormal(0.f, 0.f, 1.f, u, v);
        EXPECT_EQ(u, 0);
        EXPECT_EQ(v, 0);
    }

    TEST(LodTileBake, normal_sweep_stays_within_tolerance)
    {
        constexpr double stepDegrees = 7.5;
        int checked = 0;
        for (double elevation = -90.0; elevation <= 90.0; elevation += stepDegrees)
        {
            for (double azimuth = 0.0; azimuth < 360.0; azimuth += stepDegrees)
            {
                const double el = elevation * pi / 180.0;
                const double az = azimuth * pi / 180.0;
                const double original[3] = { std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el) };
                std::int8_t u = 0;
                std::int8_t v = 0;
                encodeNormal(static_cast<float>(original[0]), static_cast<float>(original[1]),
                    static_cast<float>(original[2]), u, v);
                float decoded[3] = { 0, 0, 0 };
                decodeNormal(u, v, decoded[0], decoded[1], decoded[2]);
                const double length = std::sqrt(static_cast<double>(decoded[0]) * decoded[0]
                    + static_cast<double>(decoded[1]) * decoded[1] + static_cast<double>(decoded[2]) * decoded[2]);
                EXPECT_NEAR(length, 1.0, 1e-4);
                EXPECT_LE(angleBetween(original, decoded), normalToleranceRadians)
                    << "elevation " << elevation << " azimuth " << azimuth;
                ++checked;
            }
        }
        EXPECT_GT(checked, 1000);
    }

    TEST(LodTileBake, normal_decode_is_unit_length_for_every_byte_pair)
    {
        for (int u = -128; u <= 127; ++u)
        {
            for (int v = -128; v <= 127; ++v)
            {
                float x = 0.f;
                float y = 0.f;
                float z = 0.f;
                decodeNormal(static_cast<std::int8_t>(u), static_cast<std::int8_t>(v), x, y, z);
                const double length
                    = std::sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y + static_cast<double>(z) * z);
                ASSERT_NEAR(length, 1.0, 1e-4) << u << ", " << v;
            }
        }
    }

    TEST(LodTileBake, normal_encode_never_fails_on_degenerate_input)
    {
        const float inputs[][3]
            = { { 0, 0, 0 }, { 2, 0, 0 }, { 0.001f, 0, 0 }, { 3, 4, 5 }, { 1e30f, 1e30f, 1e30f }, { -7, 2, -3 } };
        for (const auto& input : inputs)
        {
            std::int8_t u = 0;
            std::int8_t v = 0;
            encodeNormal(input[0], input[1], input[2], u, v);
            float x = 0.f;
            float y = 0.f;
            float z = 0.f;
            decodeNormal(u, v, x, y, z);
            EXPECT_TRUE(std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
                << input[0] << ", " << input[1] << ", " << input[2];
        }
    }

    // ---- (3) one tile ----------------------------------------------------------------------------------------------

    class LodTileBakeOrigin : public ::testing::TestWithParam<Cell>
    {
    };

    TEST_P(LodTileBakeOrigin, tile_fields_heights_and_normals)
    {
        const Cell origin = GetParam();
        const GridLand source(hills);
        const TileBakeSettings settings = defaultSettings();
        const BakedTile baked = bakeOne(source, origin.first, origin.second, settings);
        const TerrainTile& tile = baked.mData;

        std::int32_t originX = 0;
        std::int32_t originY = 0;
        tileOriginCell(baked.mId, originX, originY);
        EXPECT_EQ(originX, origin.first);
        EXPECT_EQ(originY, origin.second);

        EXPECT_EQ(tile.mGridSize, 33u);
        EXPECT_EQ(tile.mLevel, 1u);
        EXPECT_FLOAT_EQ(tile.mSide, 2.f * static_cast<float>(settings.mCellSize));
        EXPECT_FLOAT_EQ(tile.mOriginX, static_cast<float>(origin.first) * static_cast<float>(settings.mCellSize));
        EXPECT_FLOAT_EQ(tile.mOriginY, static_cast<float>(origin.second) * static_cast<float>(settings.mCellSize));
        EXPECT_FALSE(tile.mHasAlbedo);
        EXPECT_TRUE(tile.mHasNormals);
        ASSERT_EQ(tile.mHeights.size(), 33u * 33u);
        ASSERT_EQ(tile.mNormals.size(), 2u * 33u * 33u);

        float expectedMin = 1e30f;
        float expectedMax = -1e30f;
        for (std::int64_t j = 0; j < 33; ++j)
        {
            for (std::int64_t i = 0; i < 33; ++i)
            {
                const float h = source.height(origin.first * 64 + i * coarseStep, origin.second * 64 + j * coarseStep);
                expectedMin = std::min(expectedMin, h);
                expectedMax = std::max(expectedMax, h);
            }
        }
        EXPECT_FLOAT_EQ(tile.mMinHeight, expectedMin);
        EXPECT_FLOAT_EQ(tile.mMaxHeight, expectedMax);

        const float tolerance = quantStep(tile) * 1.01f + 1e-3f;
        for (std::int64_t j = 0; j < 33; ++j)
        {
            for (std::int64_t i = 0; i < 33; ++i)
            {
                const std::size_t index = static_cast<std::size_t>(j * 33 + i);
                const std::int64_t gx = origin.first * 64 + i * coarseStep;
                const std::int64_t gy = origin.second * 64 + j * coarseStep;
                EXPECT_NEAR(decodeHeight(tile, index), source.height(gx, gy), tolerance) << i << ", " << j;

                float nx = 0.f;
                float ny = 0.f;
                float nz = 0.f;
                source.normal(gx, gy, nx, ny, nz);
                std::int8_t u = 0;
                std::int8_t v = 0;
                encodeNormal(nx, ny, nz, u, v);
                EXPECT_EQ(tile.mNormals[2 * index], u) << i << ", " << j;
                EXPECT_EQ(tile.mNormals[2 * index + 1], v) << i << ", " << j;
            }
        }

        std::vector<std::uint8_t> bytes;
        const LodError written = writeTerrainTile(tile, bytes);
        EXPECT_TRUE(written.ok()) << written.describe();
        EXPECT_FALSE(bytes.empty());
    }

    INSTANTIATE_TEST_SUITE_P(
        Origins, LodTileBakeOrigin, ::testing::Values(Cell{ -2, -2 }, Cell{ 0, 0 }, Cell{ -2, 0 }, Cell{ 4, -6 }));

    TEST(LodTileBake, geometric_error_is_zero_for_a_ramp)
    {
        const GridLand source(ramp);
        for (const Cell& origin : { Cell{ 0, 0 }, Cell{ -2, -2 } })
        {
            const BakedTile baked = bakeOne(source, origin.first, origin.second);
            const float tolerance = quantStep(baked.mData) * 2.f + 0.05f;
            EXPECT_GE(baked.mData.mGeometricError, 0.f);
            EXPECT_LE(baked.mData.mGeometricError, tolerance) << origin.first << ", " << origin.second;
        }
    }

    TEST(LodTileBake, geometric_error_covers_a_spike_between_coarse_vertices)
    {
        constexpr float spike = 100.f;
        for (const Cell& origin : { Cell{ 0, 0 }, Cell{ -2, -2 } })
        {
            GridLand source(flat);
            // Offset 6 is not a multiple of the coarse step 4, so no coarse vertex samples the spike.
            source.addOffset(origin.first * 64 + 6, origin.second * 64 + 6, spike);
            const BakedTile baked = bakeOne(source, origin.first, origin.second);
            EXPECT_GE(baked.mData.mGeometricError, spike * 0.9f);
            EXPECT_LE(baked.mData.mGeometricError, spike * 1.01f);
        }
    }

    TEST(LodTileBake, geometric_error_is_positive_for_hills_and_never_negative)
    {
        const GridLand source(hills);
        for (const Cell& origin : { Cell{ 0, 0 }, Cell{ -2, -2 }, Cell{ 2, 6 } })
        {
            const BakedTile baked = bakeOne(source, origin.first, origin.second);
            EXPECT_TRUE(std::isfinite(baked.mData.mGeometricError));
            EXPECT_GT(baked.mData.mGeometricError, 0.f);
        }
    }

    TEST(LodTileBake, flat_land_has_equal_min_and_max_and_zero_heights)
    {
        const GridLand source(flat);
        const BakedTile baked = bakeOne(source, 0, 0);
        EXPECT_FLOAT_EQ(baked.mData.mMinHeight, 100.f);
        EXPECT_FLOAT_EQ(baked.mData.mMaxHeight, 100.f);
        ASSERT_EQ(baked.mData.mHeights.size(), 33u * 33u);
        for (const std::uint16_t value : baked.mData.mHeights)
        {
            ASSERT_EQ(value, 0u);
        }
        EXPECT_FLOAT_EQ(baked.mData.mGeometricError, 0.f);
    }

    // ---- (4) provenance --------------------------------------------------------------------------------------------

    TEST(LodTileBake, source_digest_changes_when_any_full_detail_height_changes)
    {
        const BakedTile baseline = bakeOne(GridLand(hills), 0, 0);
        // Global vertex coordinates inside the tile (cells 0 and 1: 0 to 128). Most are NOT on the coarse grid.
        const Cell positions[] = { { 1, 0 }, { 6, 6 }, { 127, 5 }, { 128, 128 }, { 0, 0 }, { 64, 64 }, { 3, 125 } };
        for (const Cell& position : positions)
        {
            GridLand changed(hills);
            changed.addOffset(position.first, position.second, 0.25f);
            const BakedTile other = bakeOne(changed, 0, 0);
            EXPECT_NE(other.mSourceDigest, baseline.mSourceDigest) << position.first << ", " << position.second;
        }
    }

    TEST(LodTileBake, the_parts_of_the_digest_are_set_and_a_height_change_shows_in_the_heights_part)
    {
        // The message of a stale pack names the part that differs (review of the far eviction, finding 6).
        const BakedTile baseline = bakeOne(GridLand(hills), 0, 0);
        const BakedTile again = bakeOne(GridLand(hills), 0, 0);
        EXPECT_NE(baseline.mHeightsDigest, EdenArchive::Hash{});
        EXPECT_NE(baseline.mNormalsDigest, EdenArchive::Hash{});
        EXPECT_NE(baseline.mHeightsDigest, baseline.mNormalsDigest);
        EXPECT_EQ(baseline.mHeightsDigest, again.mHeightsDigest);
        EXPECT_EQ(baseline.mNormalsDigest, again.mNormalsDigest);
        GridLand changed(hills);
        changed.addOffset(6, 6, 0.25f);
        const BakedTile other = bakeOne(changed, 0, 0);
        EXPECT_NE(other.mHeightsDigest, baseline.mHeightsDigest);
        EXPECT_TRUE(baseline.mUsedLayers.empty()); // no albedo: no layers
    }

    // The Windows and the Linux bake of Morrowind's land differ in the last bit of the normal at some cell corners
    // (Storage::averageNormal normalises a sum of floats, which compilers that fuse multiply-adds round differently),
    // so a pack baked on one platform was refused as stale on the other (review of the far eviction, finding 6, proved
    // by the digests of the first differing tile: equal heights, different normals). The corner is not an input: the
    // engine computes it from the four neighbours and ignores its own stored normal.
    TEST(LodTileBake, the_digest_ignores_the_last_bit_of_a_normal_at_a_cell_corner)
    {
        const BakedTile baseline = bakeOne(GridLand(hills), 0, 0);
        const std::pair<std::int64_t, std::int64_t> corners[] = { { 0, 0 }, { 64, 0 }, { 128, 0 }, { 0, 64 },
            { 64, 64 }, { 128, 64 }, { 0, 128 }, { 64, 128 }, { 128, 128 } };
        for (const auto& corner : corners)
        {
            GridLand noisy(hills);
            noisy.addNormalNoise(corner.first, corner.second);
            const BakedTile other = bakeOne(noisy, 0, 0);
            EXPECT_EQ(other.mSourceDigest, baseline.mSourceDigest) << corner.first << ", " << corner.second;
            EXPECT_EQ(other.mNormalsDigest, baseline.mNormalsDigest) << corner.first << ", " << corner.second;
        }
        GridLand all(hills);
        for (const auto& corner : corners)
            all.addNormalNoise(corner.first, corner.second);
        EXPECT_EQ(bakeOne(all, 0, 0).mSourceDigest, baseline.mSourceDigest);
    }

    TEST(LodTileBake, the_digest_follows_the_last_bit_of_a_normal_that_is_not_at_a_cell_corner)
    {
        const BakedTile baseline = bakeOne(GridLand(hills), 0, 0);
        // On a cell edge, in a cell, next to a corner, and on the tile's own edge, but not at a corner.
        const std::pair<std::int64_t, std::int64_t> places[]
            = { { 64, 1 }, { 1, 64 }, { 6, 6 }, { 65, 64 }, { 0, 127 }, { 127, 5 } };
        for (const auto& place : places)
        {
            GridLand noisy(hills);
            noisy.addNormalNoise(place.first, place.second);
            const BakedTile other = bakeOne(noisy, 0, 0);
            EXPECT_NE(other.mSourceDigest, baseline.mSourceDigest) << place.first << ", " << place.second;
            EXPECT_NE(other.mNormalsDigest, baseline.mNormalsDigest) << place.first << ", " << place.second;
        }
    }

    TEST(LodTileBake, the_baker_version_says_the_digest_leaves_out_the_corner_normals)
    {
        // Packs of version 1 hashed every normal and are refused by the manifest check; version 2 leaves out the cell
        // corners.
        EXPECT_GE(bakerVersion, 2u);
    }

    TEST(LodTileBake, source_digest_is_stable_and_depends_on_the_id)
    {
        const GridLand source(flat);
        const BakedTile a = bakeOne(source, 0, 0);
        const BakedTile again = bakeOne(source, 0, 0);
        const BakedTile elsewhere = bakeOne(source, 2, 0);
        EXPECT_EQ(a.mSourceDigest, again.mSourceDigest);
        EXPECT_NE(a.mSourceDigest, elsewhere.mSourceDigest); // same heights, different id

        TileBakeSettings other = defaultSettings();
        other.mWorldspace = 1;
        const BakedTile otherWorldspace = bakeOne(source, 0, 0, other);
        EXPECT_NE(a.mSourceDigest, otherWorldspace.mSourceDigest);
    }

    TEST(LodTileBake, parameters_hold_the_settings_only)
    {
        const GridLand source(flat);
        const BakedTile a = bakeOne(source, 0, 0);
        const BakedTile b = bakeOne(source, 2, 4);
        EXPECT_FALSE(a.mParameters.empty());
        EXPECT_EQ(a.mParameters, b.mParameters);

        TileBakeSettings other = defaultSettings();
        other.mCellSize = 4096;
        const BakedTile smallerCells = bakeOne(source, 0, 0, other);
        EXPECT_NE(a.mParameters, smallerCells.mParameters);
    }

    // ---- (5) bad settings ------------------------------------------------------------------------------------------

    TEST(LodTileBake, refuses_a_grid_size_that_is_not_a_power_of_two_step)
    {
        const GridLand source(flat);
        TileBakeSettings settings = defaultSettings();
        settings.mGridSize = 34;
        BakedTile tile;
        const LodError error = bakeTerrainTile(source, levelOneTile(0, 0, 0), settings, tile);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
    }

    TEST(LodTileBake, refuses_a_zero_cell_size)
    {
        const GridLand source(flat);
        TileBakeSettings settings = defaultSettings();
        settings.mCellSize = 0;
        BakedTile tile;
        const LodError error = bakeTerrainTile(source, levelOneTile(0, 0, 0), settings, tile);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());
    }

    TEST(LodTileBake, refuses_an_id_whose_level_differs_from_the_settings)
    {
        const GridLand source(flat);
        TileBakeSettings settings = defaultSettings();
        settings.mLevel = 2; // N 33 is valid at level 2 (every 8th vertex), so only the mismatch is wrong
        BakedTile tile;
        const LodError error = bakeTerrainTile(source, levelOneTile(0, 0, 0), settings, tile);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());

        const TileId levelTwo = tileContainingCell(0, 2, 0, 0).value();
        const LodError reverse = bakeTerrainTile(source, levelTwo, defaultSettings(), tile);
        EXPECT_FALSE(reverse.ok());
    }

    // ---- (6) tilesCoveringCells ------------------------------------------------------------------------------------

    TEST(LodTileBake, covering_cells_are_sorted_unique_and_independent_of_input_order)
    {
        TileBakeSettings settings = defaultSettings();
        settings.mWorldspace = 3;

        std::vector<Cell> cells = worldCells();
        const std::vector<Cell> original = cells;
        cells.insert(cells.end(), original.begin(), original.begin() + 7); // duplicates
        std::mt19937 engine(12345);
        std::shuffle(cells.begin(), cells.end(), engine);

        std::vector<TileId> expected;
        for (const Cell& cell : original)
        {
            const TileId id = levelOneTile(settings.mWorldspace, cell.first, cell.second);
            if (std::find(expected.begin(), expected.end(), id) == expected.end())
            {
                expected.push_back(id);
            }
        }
        std::sort(expected.begin(), expected.end(),
            [](const TileId& a, const TileId& b) { return packTileKey(a).value() < packTileKey(b).value(); });
        ASSERT_EQ(expected.size(), 6u); // x pairs (-4,-3) (-2,-1) (0,1), y pairs (-2,-1) (0,1)

        const std::vector<TileId> tiles = tilesCoveringCells(cells, settings);
        EXPECT_EQ(tiles, expected);
        EXPECT_EQ(tilesCoveringCells(original, settings), expected);
    }

    TEST(LodTileBake, covering_cells_split_negative_and_positive_cells)
    {
        const std::vector<Cell> cells = { { -1, -1 }, { 0, 0 } };
        const std::vector<TileId> tiles = tilesCoveringCells(cells, defaultSettings());
        ASSERT_EQ(tiles.size(), 2u);
        EXPECT_NE(tiles[0], tiles[1]);
        EXPECT_EQ(tiles[0], levelOneTile(0, -1, -1));
        EXPECT_EQ(tiles[1], levelOneTile(0, 0, 0));
        EXPECT_LT(packTileKey(tiles[0]).value(), packTileKey(tiles[1]).value());

        // Cells (-2, -2) and (-1, -1) share a tile.
        EXPECT_EQ(tilesCoveringCells({ { -2, -2 }, { -1, -1 } }, defaultSettings()).size(), 1u);
    }

    TEST(LodTileBake, covering_cells_of_nothing_is_empty)
    {
        EXPECT_TRUE(tilesCoveringCells({}, defaultSettings()).empty());
    }

    // ---- (7) the whole bake ----------------------------------------------------------------------------------------

    TEST(LodTileBake, bake_terrain_returns_tiles_in_key_order_with_a_manifest)
    {
        TileBakeSettings settings = defaultSettings();
        settings.mWorldspace = 2;
        GridLand source(hills);
        source.setLandCells(worldCells());
        const WorldBake bake = bakeWorld(source, worldCells(), settings, "Testland");

        EXPECT_EQ(bake.mManifest.mLayoutVersion, tileKeyLayoutVersion);
        EXPECT_EQ(bake.mManifest.mBakerVersion, bakerVersion);
        EXPECT_EQ(bake.mManifest.mLevels, std::vector<std::uint32_t>{ 1 });
        EXPECT_EQ(bake.mManifest.mGridSize, 33u);
        EXPECT_EQ(bake.mManifest.mAlbedoSize, 0u);
        ASSERT_EQ(bake.mManifest.mWorldspaces.size(), 1u);
        EXPECT_EQ(bake.mManifest.mWorldspaces[0].mIndex, 2u);
        EXPECT_EQ(bake.mManifest.mWorldspaces[0].mName, "Testland");
        EXPECT_EQ(bake.mManifest.mWorldspaces[0].mCellSize, settings.mCellSize);

        const std::vector<TileId> expected = tilesCoveringCells(worldCells(), settings);
        ASSERT_EQ(bake.mTiles.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(bake.mTiles[i].mId, expected[i]);
        }
        for (std::size_t i = 1; i < bake.mTiles.size(); ++i)
        {
            EXPECT_LT(packTileKey(bake.mTiles[i - 1].mId).value(), packTileKey(bake.mTiles[i].mId).value());
        }
    }

    TEST(LodTileBake, bake_terrain_does_not_produce_tiles_without_land)
    {
        // Land in two far apart cells only: two tiles, not the tiles in between.
        const std::vector<Cell> cells = { { 0, 0 }, { 10, 10 } };
        GridLand source(hills);
        source.setLandCells(cells);
        const WorldBake bake = bakeWorld(source, cells);
        ASSERT_EQ(bake.mTiles.size(), 2u);
        EXPECT_EQ(bake.mTiles[0].mId, levelOneTile(0, 0, 0));
        EXPECT_EQ(bake.mTiles[1].mId, levelOneTile(0, 10, 10));

        const WorldBake none = bakeWorld(source, {});
        EXPECT_TRUE(none.mTiles.empty());
    }

    TEST(LodTileBake, bake_terrain_is_independent_of_the_cell_order)
    {
        GridLand source(hills);
        source.setLandCells(worldCells());
        const WorldBake first = bakeWorld(source, worldCells());

        std::vector<Cell> shuffled = worldCells();
        shuffled.insert(shuffled.end(), shuffled.begin(), shuffled.begin() + 5);
        std::mt19937 engine(777);
        std::shuffle(shuffled.begin(), shuffled.end(), engine);
        const WorldBake second = bakeWorld(source, shuffled);

        EXPECT_EQ(first.mManifest, second.mManifest);
        EXPECT_TRUE(sameTiles(first.mTiles, second.mTiles));
        EXPECT_EQ(contentDigest(first.mManifest, first.mTiles), contentDigest(second.mManifest, second.mTiles));
    }

    TEST(LodTileBake, pack_bytes_are_identical_across_runs_and_tile_order)
    {
        GridLand source(hills);
        source.setLandCells(worldCells());
        const WorldBake first = bakeWorld(source, worldCells());
        const WorldBake second = bakeWorld(source, worldCells());

        const std::vector<std::uint8_t> a = packBytes(first, false);
        const std::vector<std::uint8_t> b = packBytes(second, false);
        const std::vector<std::uint8_t> reversed = packBytes(first, true);
        EXPECT_FALSE(a.empty());
        EXPECT_EQ(a, b);
        EXPECT_EQ(a, reversed);
    }

    TEST(LodTileBake, pack_reads_every_baked_tile_back)
    {
        GridLand source(hills);
        source.setLandCells(worldCells());
        const WorldBake bake = bakeWorld(source, worldCells());
        const std::vector<std::uint8_t> bytes = packBytes(bake, false);

        LodError error;
        const std::unique_ptr<TilePack> pack = TilePack::open(EdenArchive::makeMemorySource(bytes), error);
        ASSERT_NE(pack, nullptr) << error.describe();
        EXPECT_EQ(pack->manifest(), bake.mManifest);
        EXPECT_EQ(pack->sourceDigest(), contentDigest(bake.mManifest, bake.mTiles));
        ASSERT_EQ(pack->tileIds().size(), bake.mTiles.size());
        for (std::size_t i = 0; i < bake.mTiles.size(); ++i)
        {
            EXPECT_EQ(pack->tileIds()[i], bake.mTiles[i].mId);
            TerrainTile back;
            const LodError readError = pack->readTile(bake.mTiles[i].mId, back);
            ASSERT_TRUE(readError.ok()) << readError.describe();
            EXPECT_EQ(payloadBytes(back), payloadBytes(bake.mTiles[i].mData)) << i;
        }
    }

    // ---- (8) contentDigest -----------------------------------------------------------------------------------------

    TEST(LodTileBake, content_digest_is_equal_for_equal_bakes)
    {
        GridLand source(hills);
        source.setLandCells(worldCells());
        const WorldBake a = bakeWorld(source, worldCells());
        const WorldBake b = bakeWorld(source, worldCells());
        EXPECT_EQ(contentDigest(a.mManifest, a.mTiles), contentDigest(b.mManifest, b.mTiles));
    }

    TEST(LodTileBake, content_digest_changes_when_one_height_changes)
    {
        GridLand source(hills);
        source.setLandCells(worldCells());
        const WorldBake baseline = bakeWorld(source, worldCells());

        GridLand changed(hills);
        changed.setLandCells(worldCells());
        changed.addOffset(-100, -60, 1.f); // inside cell (-2, -1), off the coarse grid
        const WorldBake other = bakeWorld(changed, worldCells());
        EXPECT_NE(contentDigest(baseline.mManifest, baseline.mTiles), contentDigest(other.mManifest, other.mTiles));
    }

    TEST(LodTileBake, content_digest_changes_with_the_manifest)
    {
        GridLand source(hills);
        source.setLandCells(worldCells());
        const WorldBake baseline = bakeWorld(source, worldCells(), defaultSettings(), "Testland");
        TileManifest renamed = baseline.mManifest;
        ASSERT_EQ(renamed.mWorldspaces.size(), 1u);
        renamed.mWorldspaces[0].mName = "Otherland";
        EXPECT_NE(contentDigest(baseline.mManifest, baseline.mTiles), contentDigest(renamed, baseline.mTiles));
    }
}
