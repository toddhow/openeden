#ifndef OPENEDEN_RENDER_BLUENOISE_H
#define OPENEDEN_RENDER_BLUENOISE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

/*
 * OpenEden (task 0042, stage log 2026-11-04): a blue noise texture made at start-up, and the maths that uses it.
 *
 * Blue noise has no low frequencies: neighbouring texels differ as much as they can, so a value that shifts a ray's
 * samples (the volumetric clouds' march) turns banding into a fine grain the eye and the temporal filter average out,
 * where interleaved gradient noise leaves a lattice. The texture is built by the void-and-cluster method (Ulichney
 * 1993, "Void-and-cluster method for dither array generation"): start from a sparse random pattern, move its tightest
 * cluster into its largest void until stable, then give every texel a rank by removing ones from the tightest cluster
 * (ranks below the start count), then adding ones to the largest void (up to half), then the same on the zeros. A rank
 * is the texel's value, so the histogram is exactly flat. The distance between texels is on a torus (the texture
 * repeats), the kernel is a Gaussian of sigma 1.5 texels held as integers, and every tie goes to the lowest index, so
 * the result depends on the seed alone and is the same on every compiler. No file is read or checked in.
 *
 * The shader repeats `ditherOffset` and the lookup (files/shaders/comp/cloud/cloud.slang): change them together.
 */
namespace Render::BlueNoise
{
    constexpr std::uint32_t textureSize = 64;
    // A chosen seed (the clouds' noise seed's neighbour); any value gives a valid texture.
    constexpr std::uint64_t defaultSeed = 0x0E0E1043ull;

    // The share of texels that are set in the starting pattern, per cent of the texture (Ulichney: about one in ten).
    constexpr std::uint32_t initialPercent = 10;
    // The Gaussian's width in texels and the half width of the window it is summed over (3 sigma, rounded up).
    constexpr double kernelSigma = 1.5;
    constexpr int kernelRadius = 5;
    constexpr double kernelScale = 65536.0;

    // The golden ratio's fraction: adding frame * this to a value (mod 1) spreads the values of one texel over the
    // frames as evenly as a sequence can (a low discrepancy sequence).
    constexpr float goldenRatioFraction = 0.6180339887f;
    // The frame counter is reduced by this mask before it is multiplied, to keep the float's precision.
    constexpr std::uint32_t frameMask = 1023u;

    namespace Detail
    {
        // A small, platform independent generator (splitmix64).
        inline std::uint64_t next(std::uint64_t& state)
        {
            state += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        // The energy field of a set of texels on a torus: the sum of the Gaussian over the set's members.
        class Field
        {
        public:
            explicit Field(std::uint32_t size)
                : mSize(static_cast<int>(size))
                , mEnergy(static_cast<std::size_t>(size) * size, 0)
            {
                const int w = 2 * kernelRadius + 1;
                mKernel.resize(static_cast<std::size_t>(w) * w);
                for (int y = -kernelRadius; y <= kernelRadius; ++y)
                    for (int x = -kernelRadius; x <= kernelRadius; ++x)
                        mKernel[static_cast<std::size_t>(y + kernelRadius) * w + (x + kernelRadius)]
                            = static_cast<std::int32_t>(std::floor(
                                std::exp(-(x * x + y * y) / (2.0 * kernelSigma * kernelSigma)) * kernelScale + 0.5));
            }

            // Adds (sign 1) or removes (sign -1) the texel `index` to or from the set.
            void update(std::size_t index, int sign)
            {
                const int w = 2 * kernelRadius + 1;
                const int cx = static_cast<int>(index % static_cast<std::size_t>(mSize));
                const int cy = static_cast<int>(index / static_cast<std::size_t>(mSize));
                for (int y = -kernelRadius; y <= kernelRadius; ++y)
                {
                    const int py = ((cy + y) % mSize + mSize) % mSize;
                    for (int x = -kernelRadius; x <= kernelRadius; ++x)
                    {
                        const int px = ((cx + x) % mSize + mSize) % mSize;
                        mEnergy[static_cast<std::size_t>(py) * mSize + px]
                            += sign * mKernel[static_cast<std::size_t>(y + kernelRadius) * w + (x + kernelRadius)];
                    }
                }
            }

            std::int32_t energy(std::size_t index) const { return mEnergy[index]; }
            std::size_t count() const { return mEnergy.size(); }

        private:
            int mSize;
            std::vector<std::int32_t> mKernel;
            std::vector<std::int32_t> mEnergy;
        };

        // The member of the set (`inSet` true) or of its complement with the highest energy (tightest cluster) or the
        // lowest (largest void); the lowest index wins a tie.
        inline std::size_t extreme(
            const Field& field, const std::vector<std::uint8_t>& pattern, bool wantSet, bool highest)
        {
            std::size_t best = field.count();
            std::int32_t bestValue = 0;
            for (std::size_t i = 0; i < field.count(); ++i)
            {
                if ((pattern[i] != 0) != wantSet)
                    continue;
                const std::int32_t e = field.energy(i);
                if (best == field.count() || (highest ? e > bestValue : e < bestValue))
                {
                    best = i;
                    bestValue = e;
                }
            }
            return best;
        }
    }

    // The rank of every texel, 0 to size*size - 1, each rank used once (a permutation), row by row.
    inline std::vector<std::uint32_t> generateRanks(std::uint32_t size = textureSize, std::uint64_t seed = defaultSeed)
    {
        using namespace Detail;
        const std::size_t n = static_cast<std::size_t>(size) * size;
        std::vector<std::uint32_t> ranks(n, 0);
        if (size == 0)
            return ranks;

        // The starting pattern: a seeded shuffle of the indices, the first `ones` are set.
        const std::size_t ones = std::max<std::size_t>(1, n * initialPercent / 100);
        std::vector<std::size_t> order(n);
        std::iota(order.begin(), order.end(), std::size_t{ 0 });
        std::uint64_t state = seed;
        for (std::size_t i = n - 1; i > 0; --i)
            std::swap(order[i], order[static_cast<std::size_t>(next(state) % (i + 1))]);
        std::vector<std::uint8_t> pattern(n, 0);
        Field field(size);
        for (std::size_t i = 0; i < ones; ++i)
        {
            pattern[order[i]] = 1;
            field.update(order[i], 1);
        }

        // Relax: move the tightest cluster into the largest void until that is the same texel.
        for (std::size_t guard = 0; guard < 4 * n; ++guard)
        {
            const std::size_t cluster = extreme(field, pattern, true, true);
            pattern[cluster] = 0;
            field.update(cluster, -1);
            const std::size_t gap = extreme(field, pattern, false, false);
            pattern[gap] = 1;
            field.update(gap, 1);
            if (gap == cluster)
                break;
        }
        const std::vector<std::uint8_t> start = pattern;
        const Field startField = field;

        // Phase 1: remove ones from the tightest cluster; the last removed has rank 0.
        std::size_t setCount = 0;
        for (std::uint8_t v : pattern)
            setCount += v;
        for (std::size_t rank = setCount; rank-- > 0;)
        {
            const std::size_t cluster = extreme(field, pattern, true, true);
            pattern[cluster] = 0;
            field.update(cluster, -1);
            ranks[cluster] = static_cast<std::uint32_t>(rank);
        }

        // Phase 2: from the starting pattern add ones to the largest void, up to half of the texels.
        pattern = start;
        Field fill = startField;
        std::size_t rank = setCount;
        for (; rank < n / 2; ++rank)
        {
            const std::size_t gap = extreme(fill, pattern, false, false);
            pattern[gap] = 1;
            fill.update(gap, 1);
            ranks[gap] = static_cast<std::uint32_t>(rank);
        }

        // Phase 3: the zeros that are left: take the one in the tightest cluster of zeros.
        Field zeros(size);
        for (std::size_t i = 0; i < n; ++i)
            if (pattern[i] == 0)
                zeros.update(i, 1);
        for (; rank < n; ++rank)
        {
            const std::size_t cluster = extreme(zeros, pattern, false, true);
            pattern[cluster] = 1;
            zeros.update(cluster, -1);
            ranks[cluster] = static_cast<std::uint32_t>(rank);
        }
        return ranks;
    }

    // The 8-bit texture (row by row): the rank scaled to 0 to 255, so every value appears size*size/256 times when the
    // size is a multiple of 16.
    inline std::vector<std::uint8_t> generate(std::uint32_t size = textureSize, std::uint64_t seed = defaultSeed)
    {
        const std::vector<std::uint32_t> ranks = generateRanks(size, seed);
        std::vector<std::uint8_t> texels(ranks.size());
        const std::uint64_t n = ranks.size();
        for (std::size_t i = 0; i < ranks.size(); ++i)
            texels[i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(ranks[i]) * 256u / n);
        return texels;
    }

    // The shift of a ray's samples inside one step, in [0, 1): the texel value (0 to 255, read as its middle) plus the
    // golden ratio sequence over the frames. `frame` is the frame counter.
    inline float ditherOffset(std::uint32_t texel, std::uint32_t frame)
    {
        const float base = (static_cast<float>(texel & 255u) + 0.5f) / 256.f;
        const float shifted = base + static_cast<float>(frame & frameMask) * goldenRatioFraction;
        const float f = shifted - std::floor(shifted);
        return f >= 1.f ? 0.f : f;
    }
}

#endif
