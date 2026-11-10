#ifndef OPENEDEN_TESTS_PUDDLETESTGRIDS_H
#define OPENEDEN_TESTS_PUDDLETESTGRIDS_H

// Helpers shared by the puddle tests (task 0095, stage W-a): synthetic height grids with known answers, a seeded
// generator, a brute-force reference for the filled height and the eight ways to turn a grid over.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PuddleTest
{
    struct Grid
    {
        int width = 0;
        int height = 0;
        std::vector<float> heights;
        std::vector<std::uint8_t> sinks;

        Grid(int w, int h, float fill = 0.f)
            : width(w)
            , height(h)
            , heights(static_cast<std::size_t>(w) * h, fill)
            , sinks(static_cast<std::size_t>(w) * h, 0)
        {
        }
        std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * width + x; }
        float& at(int x, int y) { return heights[index(x, y)]; }
        float at(int x, int y) const { return heights[index(x, y)]; }
    };

    // A small deterministic generator (not std::, whose distributions differ between libraries).
    struct Lcg
    {
        std::uint64_t state;
        explicit Lcg(std::uint64_t seed)
            : state(seed * 2862933555777941757ull + 3037000493ull)
        {
        }
        std::uint32_t next()
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<std::uint32_t>(state >> 33);
        }
        std::uint32_t below(std::uint32_t n) { return next() % n; }
    };

    // Random heights that are multiples of 0.25 below `limit` (so ties are common) and about `sinkShare` per mille of
    // interior sinks.
    inline Grid randomGrid(int w, int h, std::uint64_t seed, int limit = 20, int sinkPerMille = 20)
    {
        Lcg rng(seed);
        Grid g(w, h);
        for (float& v : g.heights)
            v = static_cast<float>(rng.below(static_cast<std::uint32_t>(limit * 4))) / 4.f;
        for (std::uint8_t& s : g.sinks)
            s = rng.below(1000) < static_cast<std::uint32_t>(sinkPerMille) ? 1 : 0;
        return g;
    }

    // The filled height by brute force: F = H on sinks and the border, F = max(H, lowest neighbouring F) elsewhere,
    // iterated until nothing changes. Exact and independent of the Priority-Flood.
    inline std::vector<float> referenceFilled(const Grid& g)
    {
        const std::size_t count = g.heights.size();
        std::vector<float> f(count, std::numeric_limits<float>::infinity());
        std::vector<bool> fixed(count, false);
        for (int y = 0; y < g.height; ++y)
            for (int x = 0; x < g.width; ++x)
            {
                const std::size_t i = g.index(x, y);
                if (x == 0 || y == 0 || x == g.width - 1 || y == g.height - 1 || g.sinks[i] != 0)
                {
                    fixed[i] = true;
                    f[i] = g.heights[i];
                }
            }
        for (bool changed = true; changed;)
        {
            changed = false;
            for (int y = 0; y < g.height; ++y)
                for (int x = 0; x < g.width; ++x)
                {
                    const std::size_t i = g.index(x, y);
                    if (fixed[i])
                        continue;
                    float best = std::numeric_limits<float>::infinity();
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if ((dx == 0 && dy == 0) || x + dx < 0 || y + dy < 0 || x + dx >= g.width
                                || y + dy >= g.height)
                                continue;
                            best = std::min(best, f[g.index(x + dx, y + dy)]);
                        }
                    const float candidate = std::max(g.heights[i], best);
                    if (candidate < f[i])
                    {
                        f[i] = candidate;
                        changed = true;
                    }
                }
        }
        return f;
    }

    // The eight symmetries of a square grid: kind 0 to 3 turn it by 90 degrees each, 4 to 7 mirror it first. Returns
    // for each source index the destination index, and the new size.
    struct Symmetry
    {
        int width = 0;
        int height = 0;
        std::vector<std::size_t> destination;
    };

    inline Symmetry symmetry(int w, int h, int kind)
    {
        Symmetry s;
        const bool swap = (kind & 1) != 0;
        s.width = swap ? h : w;
        s.height = swap ? w : h;
        s.destination.resize(static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                int mx = kind >= 4 ? w - 1 - x : x;
                int my = y;
                int dx = mx;
                int dy = my;
                switch (kind & 3)
                {
                    case 0:
                        dx = mx;
                        dy = my;
                        break;
                    case 1:
                        dx = h - 1 - my;
                        dy = mx;
                        break; // turn by 90 degrees
                    case 2:
                        dx = w - 1 - mx;
                        dy = h - 1 - my;
                        break;
                    case 3:
                        dx = my;
                        dy = w - 1 - mx;
                        break;
                }
                s.destination[static_cast<std::size_t>(y) * w + x] = static_cast<std::size_t>(dy) * s.width + dx;
            }
        return s;
    }

    template <class T>
    std::vector<T> permute(const std::vector<T>& source, const Symmetry& s)
    {
        std::vector<T> out(source.size());
        for (std::size_t i = 0; i < source.size(); ++i)
            out[s.destination[i]] = source[i];
        return out;
    }

    inline std::uint64_t fnv1a(const void* data, std::size_t bytes, std::uint64_t hash = 1469598103934665603ull)
    {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < bytes; ++i)
        {
            hash ^= p[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }
}

#endif
