// Tests for the blue noise texture the volumetric clouds' march reads (task 0042, stage log 2026-11-04),
// Render::BlueNoise: it is made at start-up from a seed, so it must be a pure function (the same every time), a
// permutation of the ranks (a flat histogram), and have far less low-frequency energy than white noise or interleaved
// gradient noise. Pure CPU: no device, window or game data.

#include <components/render/bluenoise.hpp>
#include <components/render/cloudvolumemath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace
{
    using namespace Render::BlueNoise;

    // The share of a texture's non-DC spectral energy that lies at spatial frequencies of 1 to maxK cycles per
    // texture (a naive 2D DFT: the texture is 64 by 64, so this is a few hundred thousand operations).
    double lowFrequencyShare(const std::vector<double>& values, std::uint32_t size, int maxK)
    {
        const double pi = 3.14159265358979323846;
        std::vector<double> centred(values);
        double mean = 0.0;
        for (double v : values)
            mean += v;
        mean /= static_cast<double>(values.size());
        for (double& v : centred)
            v -= mean;
        double low = 0.0;
        double total = 0.0;
        for (int ky = 0; ky < static_cast<int>(size); ++ky)
            for (int kx = 0; kx < static_cast<int>(size); ++kx)
            {
                if (kx == 0 && ky == 0)
                    continue;
                double re = 0.0;
                double im = 0.0;
                for (std::uint32_t y = 0; y < size; ++y)
                    for (std::uint32_t x = 0; x < size; ++x)
                    {
                        const double angle = -2.0 * pi
                            * (static_cast<double>(kx * static_cast<int>(x))
                                + static_cast<double>(ky * static_cast<int>(y)))
                            / static_cast<double>(size);
                        re += centred[y * size + x] * std::cos(angle);
                        im += centred[y * size + x] * std::sin(angle);
                    }
                const double power = re * re + im * im;
                total += power;
                const int fx = std::min(kx, static_cast<int>(size) - kx);
                const int fy = std::min(ky, static_cast<int>(size) - ky);
                if (fx <= maxK && fy <= maxK)
                    low += power;
            }
        return total > 0.0 ? low / total : 0.0;
    }

    std::vector<double> asDoubles(const std::vector<std::uint8_t>& texels)
    {
        return std::vector<double>(texels.begin(), texels.end());
    }

    std::vector<double> whiteNoise(std::uint32_t size, std::uint64_t seed)
    {
        std::vector<double> v(static_cast<std::size_t>(size) * size);
        std::uint64_t state = seed;
        for (double& x : v)
            x = static_cast<double>(Detail::next(state) % 256u);
        return v;
    }

    std::vector<double> gradientNoise(std::uint32_t size)
    {
        std::vector<double> v(static_cast<std::size_t>(size) * size);
        for (std::uint32_t y = 0; y < size; ++y)
            for (std::uint32_t x = 0; x < size; ++x)
                v[y * size + x] = 255.0
                    * Render::CloudVolume::interleavedGradientNoise(static_cast<float>(x), static_cast<float>(y));
        return v;
    }

    TEST(RenderBlueNoise, theSizeIsASixtyFourSquare)
    {
        EXPECT_EQ(textureSize, 64u);
        EXPECT_EQ(generate().size(), 64u * 64u);
    }

    TEST(RenderBlueNoise, theSameSeedGivesTheSameTexture)
    {
        EXPECT_EQ(generate(32, 7), generate(32, 7));
        EXPECT_EQ(generateRanks(32, 7), generateRanks(32, 7));
    }

    TEST(RenderBlueNoise, anotherSeedGivesAnotherTexture)
    {
        EXPECT_NE(generate(32, 7), generate(32, 8));
    }

    TEST(RenderBlueNoise, theRanksAreAPermutation)
    {
        const auto ranks = generateRanks(32, 3);
        ASSERT_EQ(ranks.size(), 32u * 32u);
        std::set<std::uint32_t> seen(ranks.begin(), ranks.end());
        EXPECT_EQ(seen.size(), ranks.size());
        EXPECT_EQ(*seen.begin(), 0u);
        EXPECT_EQ(*seen.rbegin(), ranks.size() - 1);
    }

    TEST(RenderBlueNoise, theHistogramIsFlat)
    {
        const auto texels = generate(64, defaultSeed);
        std::array<std::uint32_t, 256> counts{};
        for (std::uint8_t v : texels)
            ++counts[v];
        for (std::uint32_t c : counts)
            EXPECT_EQ(c, 64u * 64u / 256u);
    }

    TEST(RenderBlueNoise, aTinyTextureStillWorks)
    {
        EXPECT_TRUE(generate(0).empty());
        const auto one = generateRanks(1);
        ASSERT_EQ(one.size(), 1u);
        EXPECT_EQ(one[0], 0u);
        const auto two = generateRanks(2);
        std::set<std::uint32_t> seen(two.begin(), two.end());
        EXPECT_EQ(seen.size(), 4u);
    }

    TEST(RenderBlueNoise, itHasFarLessLowFrequencyEnergyThanWhiteNoise)
    {
        const auto blue = asDoubles(generate(64, defaultSeed));
        const double blueShare = lowFrequencyShare(blue, 64, 4);
        const double whiteShare = lowFrequencyShare(whiteNoise(64, 11), 64, 4);
        // White noise has about 80 of 4095 bins here (2 percent); blue noise should have a small part of that.
        EXPECT_GT(whiteShare, 0.01);
        EXPECT_LT(blueShare, 0.2 * whiteShare) << "blue " << blueShare << " white " << whiteShare;
    }

    TEST(RenderBlueNoise, itHasLessLowFrequencyEnergyThanInterleavedGradientNoise)
    {
        const auto blue = asDoubles(generate(64, defaultSeed));
        const double blueShare = lowFrequencyShare(blue, 64, 4);
        const double ignShare = lowFrequencyShare(gradientNoise(64), 64, 4);
        EXPECT_LT(blueShare, ignShare) << "blue " << blueShare << " ign " << ignShare;
    }

    TEST(RenderBlueNoise, neighbouringTexelsDifferMoreThanChance)
    {
        // Blue noise pushes close values apart: the mean step to the right neighbour is larger than white noise's
        // (about 85 of 256 for independent uniform values).
        const auto texels = generate(64, defaultSeed);
        double sum = 0.0;
        for (std::uint32_t y = 0; y < 64; ++y)
            for (std::uint32_t x = 0; x < 64; ++x)
                sum += std::abs(static_cast<int>(texels[y * 64 + x]) - static_cast<int>(texels[y * 64 + (x + 1) % 64]));
        EXPECT_GT(sum / (64.0 * 64.0), 85.0);
    }

    TEST(RenderBlueNoise, theTextureRepeatsWithoutASeam)
    {
        // The energy is summed on a torus, so a cyclic shift by half the size changes no spectrum magnitude.
        const auto texels = generate(64, defaultSeed);
        std::vector<double> shifted(texels.size());
        for (std::uint32_t y = 0; y < 64; ++y)
            for (std::uint32_t x = 0; x < 64; ++x)
                shifted[y * 64 + x] = texels[((y + 32) % 64) * 64 + ((x + 32) % 64)];
        EXPECT_NEAR(lowFrequencyShare(asDoubles(texels), 64, 4), lowFrequencyShare(shifted, 64, 4), 1e-9);
    }

    // --- The offset ------------------------------------------------------------------------------------------------

    TEST(RenderBlueNoise, ditherOffsetStaysInZeroToOne)
    {
        for (std::uint32_t texel : { 0u, 1u, 127u, 255u, 256u, 100000u })
            for (std::uint32_t frame : { 0u, 1u, 2u, 63u, 1023u, 1024u, 65535u, 4000000000u })
            {
                const float v = ditherOffset(texel, frame);
                EXPECT_GE(v, 0.f);
                EXPECT_LT(v, 1.f);
            }
    }

    TEST(RenderBlueNoise, ditherOffsetFrameZeroIsTheTexelValue)
    {
        for (std::uint32_t texel : { 0u, 17u, 128u, 255u })
            EXPECT_NEAR(ditherOffset(texel, 0), (static_cast<float>(texel) + 0.5f) / 256.f, 1e-6f);
    }

    TEST(RenderBlueNoise, ditherOffsetSpreadsOneTexelOverTheFrames)
    {
        // The golden ratio sequence: after 64 frames the values of one texel leave no gap larger than 0.05.
        std::vector<float> values;
        for (std::uint32_t frame = 0; frame < 64; ++frame)
            values.push_back(ditherOffset(40, frame));
        std::sort(values.begin(), values.end());
        float gap = values.front() + 1.f - values.back();
        for (std::size_t i = 1; i < values.size(); ++i)
            gap = std::max(gap, values[i] - values[i - 1]);
        EXPECT_LT(gap, 0.05f);
        float mean = 0.f;
        for (float v : values)
            mean += v;
        EXPECT_NEAR(mean / 64.f, 0.5f, 0.03f);
    }

    TEST(RenderBlueNoise, ditherOffsetDependsOnlyOnTheLowBitsOfTheFrame)
    {
        EXPECT_FLOAT_EQ(ditherOffset(5, 3), ditherOffset(5, 3 + 1024));
    }
}
