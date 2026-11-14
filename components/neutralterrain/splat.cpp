#include "splat.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace NeutralTerrain::Splat
{
    namespace
    {
        constexpr double kEpsilon = 1e-6;

        // Whole-number weights summing to 255 from float weights summing to 1 (largest remainder, ties to the lower
        // slot).
        std::array<std::uint8_t, kSlots> quantise(const std::array<double, kSlots>& w, std::size_t used)
        {
            std::array<int, kSlots> whole{};
            std::array<double, kSlots> fraction{};
            int total = 0;
            for (std::size_t i = 0; i < used; ++i)
            {
                const double scaled = w[i] * 255.0;
                whole[i] = static_cast<int>(std::floor(scaled + kEpsilon));
                fraction[i] = scaled - whole[i];
                total += whole[i];
            }
            int remaining = 255 - total;
            while (remaining > 0 && used > 0)
            {
                std::size_t best = 0;
                for (std::size_t i = 1; i < used; ++i)
                    if (fraction[i] > fraction[best] + kEpsilon)
                        best = i;
                ++whole[best];
                fraction[best] = -1.0;
                --remaining;
                if (std::all_of(fraction.begin(), fraction.begin() + used, [](double f) { return f < 0.0; }))
                    std::fill(fraction.begin(), fraction.begin() + used, 0.5);
            }
            std::array<std::uint8_t, kSlots> out{};
            for (std::size_t i = 0; i < used; ++i)
                out[i] = static_cast<std::uint8_t>(std::clamp(whole[i], 0, 255));
            return out;
        }
    }

    Quadrant composeQuadrant(std::uint8_t baseIndex, std::span<const LayerIn> layers)
    {
        Quadrant out;
        // The usable layers, in draw order; entry 0 is the base.
        std::vector<const LayerIn*> used;
        for (const LayerIn& layer : layers)
            if (layer.mOpacity != nullptr && layer.mOpacity->size() >= static_cast<std::size_t>(kVertices))
                used.push_back(&layer);
        const std::size_t m = used.size() + 1;

        const auto weightsAt = [&](int vertex, std::vector<double>& w) {
            w.assign(m, 0.0);
            double rest = 1.0;
            for (std::size_t i = m - 1; i >= 1; --i)
            {
                const double o
                    = static_cast<double>((*used[i - 1]->mOpacity)[static_cast<std::size_t>(vertex)]) / 255.0;
                w[i] = o * rest;
                rest *= 1.0 - o;
            }
            w[0] = rest;
        };

        std::vector<std::size_t> kept(m);
        std::iota(kept.begin(), kept.end(), std::size_t{ 0 });
        std::vector<double> w;
        if (m > static_cast<std::size_t>(kSlots))
        {
            std::vector<double> peak(m, 0.0);
            for (int v = 0; v < kVertices; ++v)
            {
                weightsAt(v, w);
                for (std::size_t i = 0; i < m; ++i)
                    peak[i] = std::max(peak[i], w[i]);
            }
            std::vector<std::size_t> order(m);
            std::iota(order.begin(), order.end(), std::size_t{ 0 });
            std::stable_sort(
                order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return peak[a] > peak[b]; });
            kept.assign(order.begin(), order.begin() + kSlots);
            std::sort(kept.begin(), kept.end());
        }

        out.mIds.fill(baseIndex);
        for (std::size_t s = 0; s < kept.size(); ++s)
            out.mIds[s] = kept[s] == 0 ? baseIndex : used[kept[s] - 1]->mIndex;

        out.mWeights.resize(static_cast<std::size_t>(kVertices));
        for (int v = 0; v < kVertices; ++v)
        {
            weightsAt(v, w);
            std::array<double, kSlots> kw{};
            double sum = 0.0;
            for (std::size_t s = 0; s < kept.size(); ++s)
            {
                kw[s] = w[kept[s]];
                sum += kw[s];
            }
            if (sum <= kEpsilon)
            {
                kw.fill(0.0);
                kw[0] = 1.0;
            }
            else
                for (std::size_t s = 0; s < kept.size(); ++s)
                    kw[s] /= sum;
            out.mWeights[static_cast<std::size_t>(v)] = quantise(kw, kept.size());
        }
        return out;
    }

    Layout layoutFor(float sizeInCells)
    {
        Layout l;
        const float size = std::isfinite(sizeInCells) && sizeInCells > 0.f ? std::min(sizeInCells, 1024.f) : 1.f;
        l.mK = std::max(1, static_cast<int>(std::lround(size * 2.f)));
        l.mS = l.mK == 1 ? std::max(2, static_cast<int>(std::lround(size * 32.f)) + 1) : kSide;
        l.mWidth = l.mK * l.mS;
        l.mHeight = l.mWidth + l.mK;
        return l;
    }

    Layout layoutOfSize(int width, int height)
    {
        Layout l;
        l.mK = std::max(1, height - width);
        l.mS = std::max(1, width / l.mK);
        l.mWidth = width;
        l.mHeight = height;
        return l;
    }

    Lookup lookupFor(const Layout& layout, float texCoordX, float texCoordY)
    {
        Lookup out;
        const float k = static_cast<float>(layout.mK);
        const float px = std::clamp(texCoordX, 0.f, 1.f) * k;
        const float py = std::clamp(texCoordY, 0.f, 1.f) * k;
        out.mSlotX = std::min(static_cast<int>(px), layout.mK - 1);
        out.mSlotY = std::min(static_cast<int>(py), layout.mK - 1);
        const float span = static_cast<float>(layout.mS - 1);
        out.mVertexX = (px - static_cast<float>(out.mSlotX)) * span;
        out.mVertexY = (py - static_cast<float>(out.mSlotY)) * span;
        return out;
    }

    float tilesAcross(const Layout& layout)
    {
        return static_cast<float>(layout.mS - 1) * static_cast<float>(layout.mK) * 0.5f;
    }
}
