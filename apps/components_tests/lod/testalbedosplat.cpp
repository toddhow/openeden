// Task 0361: the albedo bake blends the neutral world's alpha layers (the splat blend map of
// components/neutralterrain/splat.hpp), as terrain/main.frag does, instead of only the base layer of each quadrant.
#include <gtest/gtest.h>

#include <components/lod/albedobake.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace
{
    using namespace Lod;
    using Hash = EdenArchive::Hash;

    constexpr std::uint32_t side = 17; // texels (vertices) a quadrant slot side

    Rgba8Image solid(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        Rgba8Image image;
        image.mWidth = 2;
        image.mHeight = 2;
        for (int i = 0; i < 4; ++i)
            image.mPixels.insert(image.mPixels.end(), { r, g, b, 255 });
        return image;
    }

    // One cell (2 x 2 quadrant slots of 17 x 17 texels). The weights: in quadrant 0 (south-west) layer 0 falls from 255
    // to 0 and layer 1 rises from 0 to 255 from the west edge to the east edge; every other slot is layer 0 alone. The
    // layer index row: (0, 1, 0, 0) for every quadrant. Layer 2 appears only as a weight byte value.
    class SplatSource : public AlbedoSource
    {
    public:
        SplatSource() { mImages = { solid(255, 0, 0), solid(0, 0, 255), solid(0, 255, 0) }; }
        std::size_t layerCount() const override { return mImages.size(); }
        const Rgba8Image* layerImage(std::size_t index) const override
        {
            return index < mImages.size() ? &mImages[index] : nullptr;
        }
        Hash layerDigest(std::size_t index) const override
        {
            Hash h{};
            h.fill(static_cast<std::uint8_t>(index + 1 + mDigestShift[index < 3 ? index : 0]));
            return h;
        }
        std::uint32_t splatSide(std::uint32_t) const override { return mSplat ? side : 0; }
        void blendMap(
            std::int32_t, std::int32_t, std::uint32_t sizeInCells, std::vector<std::uint8_t>& out) const override
        {
            const std::uint32_t k = 2 * sizeInCells;
            const std::size_t width = std::size_t{ k } * side;
            out.assign(width * (width + k) * 4, 0);
            for (std::uint32_t sy = 0; sy < k; ++sy)
                for (std::uint32_t sx = 0; sx < k; ++sx)
                    for (std::uint32_t y = 0; y < side; ++y)
                        for (std::uint32_t x = 0; x < side; ++x)
                        {
                            std::uint8_t* t
                                = &out[((std::size_t{ sy } * side + y) * width + std::size_t{ sx } * side + x) * 4];
                            const bool ramp = sx == 0 && sy == 0;
                            const int w1 = ramp ? static_cast<int>(x * 255 / (side - 1)) : 0;
                            t[0] = static_cast<std::uint8_t>(255 - w1);
                            t[1] = static_cast<std::uint8_t>(w1);
                            t[2] = mWeightTwo ? 2 : 0; // a weight byte equal to layer index 2, which no quadrant names
                            t[3] = 0;
                        }
            for (std::uint32_t sy = 0; sy < k; ++sy)
                for (std::uint32_t sx = 0; sx < k; ++sx)
                {
                    std::uint8_t* t = &out[((width + sy) * width + sx) * 4];
                    t[0] = 0;
                    t[1] = 1;
                    t[2] = 0;
                    t[3] = 0;
                }
        }
        void vertexColours(
            std::int32_t, std::int32_t, std::uint32_t sizeInCells, std::vector<std::uint8_t>& rgb) const override
        {
            const std::size_t n = 32 * std::size_t{ sizeInCells } + 1;
            rgb.assign(n * n * 3, 255);
        }

        std::vector<Rgba8Image> mImages;
        bool mSplat = true;
        bool mWeightTwo = false;
        std::array<std::uint8_t, 3> mDigestShift{ 0, 0, 0 };
    };

    std::vector<std::uint8_t> bake(const SplatSource& source, std::uint32_t size = 64)
    {
        std::vector<std::uint8_t> rgba;
        EXPECT_FALSE(bool(bakeAlbedoImage(source, 0, 0, 1, size, rgba, 33)));
        return rgba;
    }
}

TEST(LodAlbedoSplat, a_weight_ramp_inside_a_quadrant_is_blended_smoothly_from_red_to_blue)
{
    SplatSource source;
    const std::vector<std::uint8_t> rgba = bake(source);
    ASSERT_EQ(rgba.size(), 64u * 64u * 4u);
    // Row 4 lies in quadrant 0 (south-west); its columns 0..31 are the ramp.
    const auto red = [&](int x) { return rgba[(4 * 64 + x) * 4]; };
    const auto blue = [&](int x) { return rgba[(4 * 64 + x) * 4 + 2]; };
    EXPECT_GT(red(1), 230);
    EXPECT_LT(blue(1), 25);
    EXPECT_LT(red(30), 25);
    EXPECT_GT(blue(30), 230);
    for (int x = 1; x < 31; ++x)
    {
        EXPECT_LE(red(x), red(x - 1) + 1) << x;
        EXPECT_GE(blue(x), blue(x - 1) - 1) << x;
    }
    // The ramp passes through a real mix: a corner-index map cannot make one.
    EXPECT_GT(red(16), 60);
    EXPECT_GT(blue(16), 60);
}

TEST(LodAlbedoSplat, the_other_quadrants_stay_layer_zero)
{
    SplatSource source;
    const std::vector<std::uint8_t> rgba = bake(source);
    // North-east quadrant, centre.
    const std::size_t i = (48 * 64 + 48) * 4;
    EXPECT_GT(rgba[i], 250);
    EXPECT_LT(rgba[i + 2], 5);
}

TEST(LodAlbedoSplat, a_splat_map_of_the_wrong_size_is_refused)
{
    class Short final : public SplatSource
    {
    public:
        void blendMap(std::int32_t x, std::int32_t y, std::uint32_t n, std::vector<std::uint8_t>& out) const override
        {
            SplatSource::blendMap(x, y, n, out);
            out.pop_back();
        }
    } source;
    std::vector<std::uint8_t> rgba;
    EXPECT_TRUE(bool(bakeAlbedoImage(source, 0, 0, 1, 64, rgba, 33)));
    EXPECT_TRUE(rgba.empty());
}

TEST(LodAlbedoSplat, the_inputs_digest_reads_only_the_index_rows_for_the_used_layers)
{
    SplatSource a;
    a.mWeightTwo = true; // weight bytes of value 2 are not layer indices
    SplatSource b = a;
    b.mDigestShift[2] = 7; // layer 2 (no quadrant names it) changes its file
    Hash da{}, db{};
    ASSERT_FALSE(bool(albedoInputsDigest(a, 0, 0, 1, 64, da, nullptr, 33)));
    ASSERT_FALSE(bool(albedoInputsDigest(b, 0, 0, 1, 64, db, nullptr, 33)));
    EXPECT_EQ(da, db);
    SplatSource c = a;
    c.mDigestShift[1] = 7; // layer 1 is named by every quadrant
    Hash dc{};
    ASSERT_FALSE(bool(albedoInputsDigest(c, 0, 0, 1, 64, dc, nullptr, 33)));
    EXPECT_NE(da, dc);
}

namespace
{
    // Black and white blocks as the only layer of a splat map: a bake that kept the mip level of the footprint (2 x 2)
    // would show them.
    class CheckerSource final : public SplatSource
    {
    public:
        CheckerSource()
        {
            Rgba8Image image;
            image.mWidth = 64;
            image.mHeight = 64;
            for (std::uint32_t y = 0; y < 64; ++y)
                for (std::uint32_t x = 0; x < 64; ++x)
                {
                    // Four blocks of 32 texels: at size 32 (half a repeat a texel) the footprint's mip level is 2 x 2.
                    const std::uint8_t v = (((x / 32) + (y / 32)) & 1u) != 0 ? 255 : 0;
                    image.mPixels.insert(image.mPixels.end(), { v, v, v, 255 });
                }
            mImages = { image, image, image };
        }
    };
}

TEST(LodAlbedoSplat, a_splat_picture_holds_each_layer_as_its_mean_colour_so_no_lattice_is_left)
{
    CheckerSource source;
    const std::vector<std::uint8_t> rgba = bake(source, 32);
    ASSERT_EQ(rgba.size(), 32u * 32u * 4u);
    int lowest = 255, highest = 0;
    for (std::size_t i = 0; i < rgba.size(); i += 4)
    {
        lowest = std::min<int>(lowest, rgba[i]);
        highest = std::max<int>(highest, rgba[i]);
    }
    EXPECT_GE(lowest, 120);
    EXPECT_LE(highest, 135);
}
