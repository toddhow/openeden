// Task 0055 (albedo bake): the CPU copy of the terrain shader's layer blend. Pure code over a generated AlbedoSource.
#include <gtest/gtest.h>

#include <components/lod/albedobake.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace
{
    using namespace Lod;

    using Bytes = std::vector<std::uint8_t>;
    using Hash = EdenArchive::Hash;

    Hash digestOf(std::uint8_t value)
    {
        Hash hash{};
        hash.fill(value);
        return hash;
    }

    Rgba8Image solidImage(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint32_t side = 2)
    {
        Rgba8Image image;
        image.mWidth = side;
        image.mHeight = side;
        for (std::uint32_t i = 0; i < side * side; ++i)
        {
            image.mPixels.insert(image.mPixels.end(), { r, g, b, 255 });
        }
        return image;
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

    // A square picture of pseudo random colours (deterministic).
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

    // Black and white pixels, one pixel a square of a checkerboard.
    Rgba8Image checkerImage(std::uint32_t side)
    {
        Rgba8Image image;
        image.mWidth = side;
        image.mHeight = side;
        for (std::uint32_t y = 0; y < side; ++y)
        {
            for (std::uint32_t x = 0; x < side; ++x)
            {
                const std::uint8_t value = ((x + y) & 1u) != 0 ? 255 : 0;
                image.mPixels.insert(image.mPixels.end(), { value, value, value, 255 });
            }
        }
        return image;
    }

    // Blend texels and vertices come from functions of the GLOBAL texel / vertex coordinates (cell * 16 + local,
    // cell * 64 + local), so that the data depends on which square is asked for.
    class TestAlbedo final : public AlbedoSource
    {
    public:
        using BlendFn = std::function<std::array<std::uint8_t, 4>(std::int64_t, std::int64_t)>;
        using VertexFn = std::function<std::array<std::uint8_t, 3>(std::int64_t, std::int64_t)>;

        TestAlbedo()
            : mBlend([](std::int64_t, std::int64_t) {
                return std::array<std::uint8_t, 4>{ 0, 0, 0, 0 };
            })
            , mVertex([](std::int64_t, std::int64_t) {
                return std::array<std::uint8_t, 3>{ 255, 255, 255 };
            })
        {
        }

        void addLayer(Rgba8Image image)
        {
            mImages.push_back(std::move(image));
            mPresent.push_back(true);
            mDigests.push_back(digestOf(static_cast<std::uint8_t>(mImages.size())));
        }

        void addMissingLayer()
        {
            mImages.emplace_back();
            mPresent.push_back(false);
            mDigests.push_back(digestOf(static_cast<std::uint8_t>(mImages.size())));
        }

        std::size_t layerCount() const override { return mImages.size(); }

        const Rgba8Image* layerImage(std::size_t index) const override
        {
            return index < mImages.size() && mPresent[index] ? &mImages[index] : nullptr;
        }

        Hash layerDigest(std::size_t index) const override
        {
            return index < mDigests.size() ? mDigests[index] : Hash{};
        }

        void blendMap(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
            std::vector<std::uint8_t>& indices) const override
        {
            const std::size_t n = static_cast<std::size_t>(blendTexelsPerCell) * sizeInCells;
            indices.assign(mBadBlendSize ? n * n * 4 - 1 : n * n * 4, 0);
            for (std::size_t ty = 0; ty < n; ++ty)
            {
                for (std::size_t tx = 0; tx < n; ++tx)
                {
                    const std::int64_t gtx = std::int64_t{ cellX } * blendTexelsPerCell + static_cast<std::int64_t>(tx);
                    const std::int64_t gty = std::int64_t{ cellY } * blendTexelsPerCell + static_cast<std::int64_t>(ty);
                    std::array<std::uint8_t, 4> texel = mBlend(gtx, gty);
                    for (std::int64_t k = 0; k < 4; ++k)
                    {
                        const auto found = mBlendOverrides.find({ gtx, gty, k });
                        if (found != mBlendOverrides.end())
                        {
                            texel[static_cast<std::size_t>(k)] = found->second;
                        }
                        const std::size_t offset = (ty * n + tx) * 4 + static_cast<std::size_t>(k);
                        if (offset < indices.size())
                        {
                            indices[offset] = texel[static_cast<std::size_t>(k)];
                        }
                    }
                }
            }
        }

        void vertexColours(std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
            std::vector<std::uint8_t>& rgb) const override
        {
            const std::size_t n = 64 * static_cast<std::size_t>(sizeInCells) + 1;
            rgb.assign(mBadVertexSize ? n * n * 3 - 1 : n * n * 3, 0);
            for (std::size_t vy = 0; vy < n; ++vy)
            {
                for (std::size_t vx = 0; vx < n; ++vx)
                {
                    const std::int64_t gvx = std::int64_t{ cellX } * 64 + static_cast<std::int64_t>(vx);
                    const std::int64_t gvy = std::int64_t{ cellY } * 64 + static_cast<std::int64_t>(vy);
                    std::array<std::uint8_t, 3> colour = mVertex(gvx, gvy);
                    for (std::int64_t k = 0; k < 3; ++k)
                    {
                        const auto found = mVertexOverrides.find({ gvx, gvy, k });
                        if (found != mVertexOverrides.end())
                        {
                            colour[static_cast<std::size_t>(k)] = found->second;
                        }
                        const std::size_t offset = (vy * n + vx) * 3 + static_cast<std::size_t>(k);
                        if (offset < rgb.size())
                        {
                            rgb[offset] = colour[static_cast<std::size_t>(k)];
                        }
                    }
                }
            }
        }

        std::vector<Rgba8Image> mImages;
        std::vector<bool> mPresent;
        std::vector<Hash> mDigests;
        BlendFn mBlend;
        VertexFn mVertex;
        bool mBadBlendSize = false;
        bool mBadVertexSize = false;
        std::map<std::array<std::int64_t, 3>, std::uint8_t> mBlendOverrides;
        std::map<std::array<std::int64_t, 3>, std::uint8_t> mVertexOverrides;
    };

    struct BakeResult
    {
        LodError mError;
        Bytes mRgba;
    };

    BakeResult bake(const AlbedoSource& source, std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
        std::uint32_t albedoSize)
    {
        BakeResult result;
        result.mRgba.assign(7, 0xAB); // must be replaced or cleared
        result.mError = bakeAlbedoImage(source, cellX, cellY, sizeInCells, albedoSize, result.mRgba);
        return result;
    }

    // Bakes a square that must succeed and checks the size and the alpha.
    Bytes bakeOk(const AlbedoSource& source, std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
        std::uint32_t albedoSize)
    {
        BakeResult result = bake(source, cellX, cellY, sizeInCells, albedoSize);
        EXPECT_TRUE(result.mError.ok()) << result.mError.describe();
        EXPECT_EQ(result.mRgba.size(), static_cast<std::size_t>(albedoSize) * albedoSize * 4);
        for (std::size_t i = 3; i < result.mRgba.size(); i += 4)
        {
            if (result.mRgba[i] != 255)
            {
                ADD_FAILURE() << "alpha " << static_cast<int>(result.mRgba[i]) << " at byte " << i;
                break;
            }
        }
        return result.mRgba;
    }

    int channel(const Bytes& rgba, std::uint32_t size, std::uint32_t x, std::uint32_t y, std::size_t which)
    {
        return rgba[(static_cast<std::size_t>(y) * size + x) * 4 + which];
    }

    // The shader's sharpened blend fraction.
    double sharpen(double fraction)
    {
        return std::clamp((fraction - 0.5) * 2.0 + 0.5, 0.0, 1.0);
    }

    // ---- basic output --------------------------------------------------------------------------------------------

    TEST(LodAlbedoBake, a_solid_layer_with_white_vertex_colours_gives_its_colour_everywhere)
    {
        TestAlbedo source;
        source.addLayer(solidImage(10, 200, 90));
        const Bytes rgba = bakeOk(source, 0, 0, 1, 16);
        for (std::uint32_t y = 0; y < 16; ++y)
        {
            for (std::uint32_t x = 0; x < 16; ++x)
            {
                EXPECT_NEAR(channel(rgba, 16, x, y, 0), 10, 1);
                EXPECT_NEAR(channel(rgba, 16, x, y, 1), 200, 1);
                EXPECT_NEAR(channel(rgba, 16, x, y, 2), 90, 1);
            }
        }
    }

    TEST(LodAlbedoBake, the_smallest_picture_is_four_texels_a_side)
    {
        TestAlbedo source;
        source.addLayer(solidImage(10, 200, 90));
        const Bytes rgba = bakeOk(source, 0, 0, 1, 4);
        EXPECT_EQ(rgba.size(), 64u);
    }

    TEST(LodAlbedoBake, vertex_colours_multiply_the_layer_colour)
    {
        TestAlbedo grey;
        grey.addLayer(solidImage(200, 100, 50));
        grey.mVertex = [](std::int64_t, std::int64_t) { return std::array<std::uint8_t, 3>{ 128, 128, 128 }; };
        const Bytes half = bakeOk(grey, 0, 0, 1, 16);

        TestAlbedo mixed;
        mixed.addLayer(solidImage(200, 100, 50));
        mixed.mVertex = [](std::int64_t, std::int64_t) { return std::array<std::uint8_t, 3>{ 255, 128, 0 }; };
        const Bytes perChannel = bakeOk(mixed, 0, 0, 1, 16);

        for (std::uint32_t y = 0; y < 16; ++y)
        {
            for (std::uint32_t x = 0; x < 16; ++x)
            {
                EXPECT_NEAR(channel(half, 16, x, y, 0), 100, 2);
                EXPECT_NEAR(channel(half, 16, x, y, 1), 50, 2);
                EXPECT_NEAR(channel(half, 16, x, y, 2), 25, 2);
                EXPECT_NEAR(channel(perChannel, 16, x, y, 0), 200, 2);
                EXPECT_NEAR(channel(perChannel, 16, x, y, 1), 50, 2);
                EXPECT_NEAR(channel(perChannel, 16, x, y, 2), 0, 2);
            }
        }
    }

    // ---- the blend rule --------------------------------------------------------------------------------------------

    TEST(LodAlbedoBake, four_corner_layers_follow_the_sharpened_bilinear_weights)
    {
        // 64 texels over 16 blend texels: four picture texels a blend texel, centres at fractions 0.125, 0.375,
        // 0.625, 0.875, which sharpen to 0, 0.25, 0.75, 1. Every blend texel holds {0, 1, 2, 3}: corner i has offset
        // (i & 1, i >> 1), so w1 is the east corner and w2 the north corner.
        const int colours[4][3] = { { 240, 0, 0 }, { 0, 240, 0 }, { 0, 0, 240 }, { 240, 240, 120 } };
        TestAlbedo source;
        for (const auto& colour : colours)
        {
            source.addLayer(solidImage(static_cast<std::uint8_t>(colour[0]), static_cast<std::uint8_t>(colour[1]),
                static_cast<std::uint8_t>(colour[2])));
        }
        source.mBlend = [](std::int64_t, std::int64_t) { return std::array<std::uint8_t, 4>{ 0, 1, 2, 3 }; };

        const Bytes rgba = bakeOk(source, 0, 0, 1, 64);
        for (std::uint32_t j = 0; j < 64; ++j)
        {
            for (std::uint32_t i = 0; i < 64; ++i)
            {
                const double px = sharpen((static_cast<double>(i % 4) + 0.5) / 4.0);
                const double py = sharpen((static_cast<double>(j % 4) + 0.5) / 4.0);
                const double weights[4] = { (1 - px) * (1 - py), px * (1 - py), (1 - px) * py, px * py };
                for (std::size_t which = 0; which < 3; ++which)
                {
                    double expected = 0;
                    for (std::size_t corner = 0; corner < 4; ++corner)
                    {
                        expected += weights[corner] * colours[corner][which];
                    }
                    ASSERT_NEAR(channel(rgba, 64, i, j, which), expected, 2.5)
                        << i << ", " << j << " channel " << which;
                }
            }
        }
    }

    TEST(LodAlbedoBake, the_west_half_is_one_layer_the_east_half_another_with_a_mix_at_the_border)
    {
        TestAlbedo source;
        source.addLayer(solidImage(200, 50, 0));
        source.addLayer(solidImage(0, 50, 200));
        // Blend texels 0..6 are layer 0, 8..15 layer 1; texel 7 has layer 0 on its west corners, 1 on its east ones.
        source.mBlend = [](std::int64_t gtx, std::int64_t) {
            if (gtx < 7)
            {
                return std::array<std::uint8_t, 4>{ 0, 0, 0, 0 };
            }
            if (gtx == 7)
            {
                return std::array<std::uint8_t, 4>{ 0, 1, 0, 1 };
            }
            return std::array<std::uint8_t, 4>{ 1, 1, 1, 1 };
        };

        const Bytes rgba = bakeOk(source, 0, 0, 1, 64);
        const double mixOfTexel[4] = { 0.0, 0.25, 0.75, 1.0 }; // layer 1's share at the four centres of blend texel 7
        for (std::uint32_t j = 0; j < 64; ++j)
        {
            for (std::uint32_t i = 0; i < 64; ++i)
            {
                double share = 0;
                if (i >= 32)
                {
                    share = 1;
                }
                else if (i >= 28)
                {
                    share = mixOfTexel[i - 28];
                }
                ASSERT_NEAR(channel(rgba, 64, i, j, 0), 200.0 * (1 - share), 2.5) << i << ", " << j;
                ASSERT_NEAR(channel(rgba, 64, i, j, 1), 50, 1.5) << i << ", " << j;
                ASSERT_NEAR(channel(rgba, 64, i, j, 2), 200.0 * share, 2.5) << i << ", " << j;
            }
        }
        // Deep inside each half.
        EXPECT_NEAR(channel(rgba, 64, 2, 10, 0), 200, 1);
        EXPECT_NEAR(channel(rgba, 64, 61, 10, 2), 200, 1);
    }

    TEST(LodAlbedoBake, row_zero_is_the_south_row_in_the_blend_map)
    {
        TestAlbedo source;
        source.addLayer(solidImage(200, 50, 0));
        source.addLayer(solidImage(0, 50, 200));
        // Blend rows 0..6 (the south) are layer 0, 8..15 (the north) layer 1; row 7's north corners are layer 1.
        source.mBlend = [](std::int64_t, std::int64_t gty) {
            if (gty < 7)
            {
                return std::array<std::uint8_t, 4>{ 0, 0, 0, 0 };
            }
            if (gty == 7)
            {
                return std::array<std::uint8_t, 4>{ 0, 0, 1, 1 };
            }
            return std::array<std::uint8_t, 4>{ 1, 1, 1, 1 };
        };

        const Bytes rgba = bakeOk(source, 0, 0, 1, 64);
        EXPECT_NEAR(channel(rgba, 64, 5, 0, 0), 200, 1); // the first row is the south
        EXPECT_NEAR(channel(rgba, 64, 5, 63, 2), 200, 1); // the last row is the north
        const double mixOfTexel[4] = { 0.0, 0.25, 0.75, 1.0 };
        for (std::uint32_t j = 28; j < 32; ++j)
        {
            const double share = mixOfTexel[j - 28];
            EXPECT_NEAR(channel(rgba, 64, 40, j, 0), 200.0 * (1 - share), 2.5) << j;
            EXPECT_NEAR(channel(rgba, 64, 40, j, 2), 200.0 * share, 2.5) << j;
        }
    }

    TEST(LodAlbedoBake, a_missing_layer_or_an_index_past_the_layer_count_is_mid_grey)
    {
        TestAlbedo outOfRange;
        outOfRange.addLayer(solidImage(250, 10, 10));
        outOfRange.mBlend = [](std::int64_t, std::int64_t) { return std::array<std::uint8_t, 4>{ 5, 5, 5, 5 }; };
        const Bytes pastTheEnd = bakeOk(outOfRange, 0, 0, 1, 16);

        TestAlbedo nullImage;
        nullImage.addLayer(solidImage(250, 10, 10));
        nullImage.addMissingLayer();
        nullImage.mBlend = [](std::int64_t, std::int64_t) { return std::array<std::uint8_t, 4>{ 1, 1, 1, 1 }; };
        const Bytes missing = bakeOk(nullImage, 0, 0, 1, 16);

        for (std::uint32_t y = 0; y < 16; ++y)
        {
            for (std::uint32_t x = 0; x < 16; ++x)
            {
                for (std::size_t which = 0; which < 3; ++which)
                {
                    EXPECT_NEAR(channel(pastTheEnd, 16, x, y, which), 128, 1);
                    EXPECT_NEAR(channel(missing, 16, x, y, which), 128, 1);
                }
            }
        }

        // Layer 0 on the west corners, a missing index on the east corners: pure layer 0 at the first centre of each
        // blend texel (sharpened fraction 0) and pure grey at the last one (1).
        TestAlbedo halfway;
        halfway.addLayer(solidImage(250, 10, 10));
        halfway.mBlend = [](std::int64_t, std::int64_t) { return std::array<std::uint8_t, 4>{ 0, 9, 0, 9 }; };
        const Bytes mixed = bakeOk(halfway, 0, 0, 1, 64);
        EXPECT_NEAR(channel(mixed, 64, 0, 3, 0), 250, 1);
        EXPECT_NEAR(channel(mixed, 64, 3, 3, 0), 128, 1);
        EXPECT_NEAR(channel(mixed, 64, 3, 3, 1), 128, 1);
    }

    TEST(LodAlbedoBake, row_zero_and_column_zero_are_south_and_west_in_the_vertex_colours)
    {
        TestAlbedo source;
        source.addLayer(solidImage(255, 255, 255));
        // Red grows to the north, green to the east: 3 a vertex, so 0 to 192 over the 65 vertices.
        source.mVertex = [](std::int64_t gvx, std::int64_t gvy) {
            return std::array<std::uint8_t, 3>{ static_cast<std::uint8_t>(3 * gvy), static_cast<std::uint8_t>(3 * gvx),
                255 };
        };
        const Bytes rgba = bakeOk(source, 0, 0, 1, 32);
        for (std::uint32_t j = 0; j < 32; ++j)
        {
            for (std::uint32_t i = 0; i < 32; ++i)
            {
                // The vertex coordinate of a texel centre is 64 (i + 0.5) / 32 = 2 (i + 0.5).
                ASSERT_NEAR(channel(rgba, 32, i, j, 0), 3.0 * 2.0 * (j + 0.5), 2.5) << i << ", " << j;
                ASSERT_NEAR(channel(rgba, 32, i, j, 1), 3.0 * 2.0 * (i + 0.5), 2.5) << i << ", " << j;
                ASSERT_NEAR(channel(rgba, 32, i, j, 2), 255, 1) << i << ", " << j;
            }
        }
        EXPECT_LT(channel(rgba, 32, 5, 0, 0), channel(rgba, 32, 5, 31, 0));
        EXPECT_LT(channel(rgba, 32, 0, 5, 1), channel(rgba, 32, 31, 5, 1));
    }

    // ---- minification ----------------------------------------------------------------------------------------------

    TEST(LodAlbedoBake, many_repeats_in_one_texel_average_to_grey)
    {
        // An 8 x 8 checkerboard of black and white pixels repeats 16 times over the blend map; a 4, 8 or 16 texel
        // picture puts 4, 2 or 1 repeats into each of its texels. Sampling only the texel centre would give black or
        // white; the footprint average gives about 127.5.
        for (const std::uint32_t size : { 4u, 8u, 16u })
        {
            TestAlbedo source;
            source.addLayer(checkerImage(8));
            const Bytes rgba = bakeOk(source, 0, 0, 1, size);
            for (std::uint32_t y = 0; y < size; ++y)
            {
                for (std::uint32_t x = 0; x < size; ++x)
                {
                    for (std::size_t which = 0; which < 3; ++which)
                    {
                        EXPECT_NEAR(channel(rgba, size, x, y, which), 128, 10)
                            << "size " << size << " texel " << x << ", " << y;
                    }
                }
            }
        }
    }

    TEST(LodAlbedoBake, one_layer_texel_a_picture_texel_keeps_its_contrast)
    {
        // 128 texels over 16 repeats of 8 texels: one layer texel a picture texel, centres on centres.
        TestAlbedo source;
        source.addLayer(checkerImage(8));
        const Bytes rgba = bakeOk(source, 0, 0, 1, 128);
        int largestStep = 0;
        for (std::uint32_t x = 0; x + 1 < 128; ++x)
        {
            const int step = std::abs(channel(rgba, 128, x, 0, 0) - channel(rgba, 128, x + 1, 0, 0));
            largestStep = std::max(largestStep, step);
        }
        EXPECT_GE(largestStep, 100);
    }

    // ---- bigger squares --------------------------------------------------------------------------------------------

    TEST(LodAlbedoBake, two_by_two_cells_use_a_32_texel_blend_map_and_129_vertices)
    {
        TestAlbedo source;
        source.addLayer(solidImage(200, 50, 0));
        source.addLayer(solidImage(0, 50, 200));
        // The square's south-west cell is (-1, -1): global blend texels -16 to 15. The border is at 0 (picture
        // texel 32).
        source.mBlend = [](std::int64_t gtx, std::int64_t) {
            return gtx < 0 ? std::array<std::uint8_t, 4>{ 0, 0, 0, 0 } : std::array<std::uint8_t, 4>{ 1, 1, 1, 1 };
        };
        // Global vertices -64 to 64: the value grows by one a vertex, 0 to 128.
        source.mVertex = [](std::int64_t gvx, std::int64_t gvy) {
            return std::array<std::uint8_t, 3>{ static_cast<std::uint8_t>(gvx + 64),
                static_cast<std::uint8_t>(gvy + 64), 255 };
        };

        const Bytes rgba = bakeOk(source, -1, -1, 2, 64);
        // Vertex coordinate of a texel centre: 128 (i + 0.5) / 64 = 2 (i + 0.5).
        EXPECT_NEAR(channel(rgba, 64, 2, 10, 0), 200.0 * (2.0 * 2.5 / 255.0), 2.5);
        EXPECT_NEAR(channel(rgba, 64, 2, 10, 2), 0, 1);
        EXPECT_NEAR(channel(rgba, 64, 61, 10, 2), 200, 1); // the vertex blue is 255 everywhere
        EXPECT_NEAR(channel(rgba, 64, 61, 10, 0), 0, 1);
        EXPECT_NEAR(channel(rgba, 64, 40, 3, 1), 50.0 * (2.0 * 3.5 / 255.0), 2.5);
        EXPECT_NEAR(channel(rgba, 64, 40, 60, 1), 50.0 * (2.0 * 60.5 / 255.0), 2.5);
    }

    TEST(LodAlbedoBake, four_by_four_cells_work)
    {
        TestAlbedo source;
        source.addLayer(solidImage(255, 255, 255));
        source.mVertex = [](std::int64_t, std::int64_t) { return std::array<std::uint8_t, 3>{ 200, 100, 50 }; };
        const Bytes rgba = bakeOk(source, 3, -7, 4, 128);
        EXPECT_NEAR(channel(rgba, 128, 0, 0, 0), 200, 1);
        EXPECT_NEAR(channel(rgba, 128, 77, 100, 1), 100, 1);
        EXPECT_NEAR(channel(rgba, 128, 127, 127, 2), 50, 1);
    }

    // ---- invalid input ---------------------------------------------------------------------------------------------

    TEST(LodAlbedoBake, invalid_sizes_are_refused_with_an_error_and_no_output)
    {
        TestAlbedo source;
        source.addLayer(solidImage(10, 20, 30));
        const std::uint32_t badSizes[] = { 0, 1, 2, 3, 5, 6, 100, 4096, 8192 };
        for (const std::uint32_t size : badSizes)
        {
            const BakeResult result = bake(source, 0, 0, 1, size);
            EXPECT_FALSE(result.mError.ok()) << "albedoSize " << size;
            EXPECT_FALSE(result.mError.mField.empty()) << "albedoSize " << size;
            EXPECT_TRUE(result.mRgba.empty()) << "albedoSize " << size;
        }
        const BakeResult noCells = bake(source, 0, 0, 0, 16);
        EXPECT_FALSE(noCells.mError.ok());
        EXPECT_FALSE(noCells.mError.mField.empty());
        EXPECT_TRUE(noCells.mRgba.empty());
    }

    TEST(LodAlbedoBake, a_source_that_returns_wrongly_sized_arrays_is_refused)
    {
        TestAlbedo shortBlend;
        shortBlend.addLayer(solidImage(10, 20, 30));
        shortBlend.mBadBlendSize = true;
        const BakeResult first = bake(shortBlend, 0, 0, 1, 16);
        EXPECT_FALSE(first.mError.ok());
        EXPECT_FALSE(first.mError.mField.empty());
        EXPECT_TRUE(first.mRgba.empty());

        TestAlbedo shortVertices;
        shortVertices.addLayer(solidImage(10, 20, 30));
        shortVertices.mBadVertexSize = true;
        const BakeResult second = bake(shortVertices, 0, 0, 1, 16);
        EXPECT_FALSE(second.mError.ok());
        EXPECT_FALSE(second.mError.mField.empty());
        EXPECT_TRUE(second.mRgba.empty());
    }

    // ---- determinism -----------------------------------------------------------------------------------------------

    // Hash driven blend map and vertex colours, four layers of which only 0 and 1 are used by the blend map.
    TestAlbedo hashedSource()
    {
        TestAlbedo source;
        source.addLayer(patternedImage(4, 1));
        source.addLayer(patternedImage(8, 2));
        source.addLayer(solidImage(1, 2, 3));
        source.addLayer(solidImage(4, 5, 6));
        source.mBlend = [](std::int64_t gtx, std::int64_t gty) {
            const std::uint64_t bits = mixBits(gtx, gty, 11);
            return std::array<std::uint8_t, 4>{ static_cast<std::uint8_t>(bits & 1),
                static_cast<std::uint8_t>((bits >> 1) & 1), static_cast<std::uint8_t>((bits >> 2) & 1),
                static_cast<std::uint8_t>((bits >> 3) & 1) };
        };
        source.mVertex = [](std::int64_t gvx, std::int64_t gvy) {
            const std::uint64_t bits = mixBits(gvx, gvy, 22);
            return std::array<std::uint8_t, 3>{ static_cast<std::uint8_t>(128 + (bits & 127)),
                static_cast<std::uint8_t>(128 + ((bits >> 8) & 127)),
                static_cast<std::uint8_t>(128 + ((bits >> 16) & 127)) };
        };
        return source;
    }

    TEST(LodAlbedoBake, two_bakes_give_identical_bytes)
    {
        const TestAlbedo first = hashedSource();
        const TestAlbedo second = hashedSource();
        const Bytes a = bakeOk(first, -2, 3, 2, 32);
        const Bytes b = bakeOk(first, -2, 3, 2, 32);
        const Bytes c = bakeOk(second, -2, 3, 2, 32);
        EXPECT_EQ(a, b);
        EXPECT_EQ(a, c);
        // The picture is not flat.
        EXPECT_NE(std::count(a.begin(), a.end(), a.front()), static_cast<std::ptrdiff_t>(a.size()));
    }

    // ---- the inputs digest -----------------------------------------------------------------------------------------

    LodError digestOfSquare(const AlbedoSource& source, std::int32_t cellX, std::int32_t cellY,
        std::uint32_t sizeInCells, std::uint32_t albedoSize, Hash& digest)
    {
        return albedoInputsDigest(source, cellX, cellY, sizeInCells, albedoSize, digest);
    }

    Hash digestOk(const AlbedoSource& source, std::int32_t cellX = 0, std::int32_t cellY = 0,
        std::uint32_t sizeInCells = 1, std::uint32_t albedoSize = 32)
    {
        Hash digest{};
        const LodError error = digestOfSquare(source, cellX, cellY, sizeInCells, albedoSize, digest);
        EXPECT_TRUE(error.ok()) << error.describe();
        return digest;
    }

    TEST(LodAlbedoBake, the_same_input_gives_the_same_digest)
    {
        const TestAlbedo first = hashedSource();
        const TestAlbedo second = hashedSource();
        EXPECT_EQ(digestOk(first), digestOk(first));
        EXPECT_EQ(digestOk(first), digestOk(second));
        EXPECT_NE(digestOk(first), Hash{});
    }

    TEST(LodAlbedoBake, the_digest_changes_with_one_blend_byte)
    {
        const TestAlbedo baseline = hashedSource();
        TestAlbedo changed = hashedSource();
        // Global blend texel (5, 9) of the square at cell (0, 0): flip byte 2.
        const std::array<std::int64_t, 3> key = { 5, 9, 2 };
        const std::uint8_t original = baseline.mBlend(5, 9)[2];
        changed.mBlendOverrides[key] = static_cast<std::uint8_t>(1 - original);
        EXPECT_NE(digestOk(baseline), digestOk(changed));
    }

    TEST(LodAlbedoBake, the_digest_changes_with_one_vertex_colour_byte)
    {
        const TestAlbedo baseline = hashedSource();
        TestAlbedo changed = hashedSource();
        const std::array<std::int64_t, 3> key = { 17, 40, 1 };
        changed.mVertexOverrides[key] = static_cast<std::uint8_t>(baseline.mVertex(17, 40)[1] ^ 1u);
        EXPECT_NE(digestOk(baseline), digestOk(changed));
    }

    TEST(LodAlbedoBake, the_digest_follows_used_layers_and_ignores_unused_ones)
    {
        const TestAlbedo baseline = hashedSource();

        TestAlbedo usedChanged = hashedSource();
        usedChanged.mDigests[0] = digestOf(200);
        EXPECT_NE(digestOk(baseline), digestOk(usedChanged));

        TestAlbedo otherUsedChanged = hashedSource();
        otherUsedChanged.mDigests[1] = digestOf(201);
        EXPECT_NE(digestOk(baseline), digestOk(otherUsedChanged));

        TestAlbedo unusedChanged = hashedSource();
        unusedChanged.mDigests[2] = digestOf(202);
        unusedChanged.mDigests[3] = digestOf(203);
        EXPECT_EQ(digestOk(baseline), digestOk(unusedChanged));
    }

    TEST(LodAlbedoBake, the_used_layers_are_reported_in_index_order_without_changing_the_digest)
    {
        // For the message of a stale pack: which layers went into the digest (unused ones are left out).
        const TestAlbedo source = hashedSource();
        Hash plain{};
        ASSERT_TRUE(albedoInputsDigest(source, 0, 0, 1, 32, plain).ok());
        Hash withList{};
        std::vector<std::uint32_t> used = { 99 }; // cleared first
        ASSERT_TRUE(albedoInputsDigest(source, 0, 0, 1, 32, withList, &used).ok());
        EXPECT_EQ(plain, withList);
        ASSERT_FALSE(used.empty());
        EXPECT_TRUE(std::is_sorted(used.begin(), used.end()));
        EXPECT_EQ(std::adjacent_find(used.begin(), used.end()), used.end());
        EXPECT_LT(used.back(), source.layerCount());
        for (const std::uint32_t index : used)
            EXPECT_NE(index, 99u);
        // The layers the previous test shows to be used (0 and 1) are in it; 2 and 3 are not.
        EXPECT_NE(std::find(used.begin(), used.end(), 0u), used.end());
        EXPECT_NE(std::find(used.begin(), used.end(), 1u), used.end());
        EXPECT_EQ(std::find(used.begin(), used.end(), 2u), used.end());
        EXPECT_EQ(std::find(used.begin(), used.end(), 3u), used.end());
    }

    TEST(LodAlbedoBake, the_digest_changes_with_the_sizes)
    {
        const TestAlbedo source = hashedSource();
        EXPECT_NE(digestOk(source, 0, 0, 1, 32), digestOk(source, 0, 0, 1, 64));
        EXPECT_NE(digestOk(source, 0, 0, 1, 32), digestOk(source, 0, 0, 2, 32));
    }

    TEST(LodAlbedoBake, the_digest_follows_the_data_of_the_square_not_its_origin)
    {
        // Data that depends on the cell: another square gives another digest.
        const TestAlbedo hashed = hashedSource();
        EXPECT_NE(digestOk(hashed, 0, 0), digestOk(hashed, 1, 0));
        EXPECT_NE(digestOk(hashed, 0, 0), digestOk(hashed, 0, 1));
        EXPECT_NE(digestOk(hashed, 0, 0), digestOk(hashed, -1, -1));

        // Identical data in two places: the same digest.
        TestAlbedo constant;
        constant.addLayer(solidImage(9, 9, 9));
        EXPECT_EQ(digestOk(constant, 0, 0), digestOk(constant, 5, -3));
    }

    TEST(LodAlbedoBake, the_digest_refuses_the_same_bad_input_as_the_bake)
    {
        TestAlbedo source;
        source.addLayer(solidImage(10, 20, 30));
        Hash digest{};
        EXPECT_FALSE(digestOfSquare(source, 0, 0, 0, 16, digest).ok());
        EXPECT_FALSE(digestOfSquare(source, 0, 0, 1, 3, digest).ok());
        EXPECT_FALSE(digestOfSquare(source, 0, 0, 1, 0, digest).ok());
        EXPECT_FALSE(digestOfSquare(source, 0, 0, 1, 4096, digest).ok());

        TestAlbedo shortBlend;
        shortBlend.addLayer(solidImage(10, 20, 30));
        shortBlend.mBadBlendSize = true;
        const LodError error = digestOfSquare(shortBlend, 0, 0, 1, 16, digest);
        EXPECT_FALSE(error.ok());
        EXPECT_FALSE(error.mField.empty());

        TestAlbedo shortVertices;
        shortVertices.addLayer(solidImage(10, 20, 30));
        shortVertices.mBadVertexSize = true;
        EXPECT_FALSE(digestOfSquare(shortVertices, 0, 0, 1, 16, digest).ok());
    }
}
