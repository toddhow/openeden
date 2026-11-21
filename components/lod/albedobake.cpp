#include "albedobake.hpp"

#include <algorithm>
#include <cmath>

namespace Lod
{
    namespace
    {
        constexpr std::uint32_t maxSizeInCells = 256;
        constexpr std::uint32_t minAlbedoSize = 4;
        constexpr std::uint32_t maxAlbedoSize = 2048;
        // What a layer that could not be read looks like.
        constexpr float missingLayerGrey = 128.f;

        LodError fail(const char* field, const char* message)
        {
            LodError error;
            error.mField = field;
            error.mMessage = message;
            return error;
        }

        bool isPowerOfTwo(std::uint32_t value)
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        LodError validate(std::uint32_t sizeInCells, std::uint32_t albedoSize)
        {
            if (sizeInCells == 0 || sizeInCells > maxSizeInCells)
                return fail("sizeInCells", "the square must be 1 to 256 cells a side");
            if (!isPowerOfTwo(albedoSize) || albedoSize < minAlbedoSize || albedoSize > maxAlbedoSize)
                return fail("albedoSize", "the albedo size must be a power of two from 4 to 2048");
            return {};
        }

        LodError fetch(const AlbedoSource& source, std::int32_t cellX, std::int32_t cellY, std::uint32_t sizeInCells,
            std::uint32_t verticesPerCell, std::vector<std::uint8_t>& blend, std::vector<std::uint8_t>& colours)
        {
            blend.clear();
            colours.clear();
            source.blendMap(cellX, cellY, sizeInCells, blend);
            source.vertexColours(cellX, cellY, sizeInCells, colours);
            const std::size_t blendTexels = std::size_t{ blendTexelsPerCell } * sizeInCells;
            const std::size_t vertices = std::size_t{ verticesPerCell - 1 } * sizeInCells + 1;
            const std::size_t splatSide = source.splatSide(sizeInCells);
            if (splatSide != 0)
            {
                const std::size_t k = std::size_t{ 2 } * sizeInCells;
                if (splatSide < 2 || splatSide > 4096 || blend.size() != k * splatSide * (k * splatSide + k) * 4)
                    return fail("blendMap", "the source returned a splat blend map of the wrong size");
            }
            else if (blend.size() != blendTexels * blendTexels * 4)
                return fail("blendMap", "the source returned a blend map of the wrong size");
            if (colours.size() != vertices * vertices * 3)
                return fail("vertexColours", "the source returned vertex colours of the wrong size");
            return {};
        }

        // A layer with its mip chain (box filtered), as floats 0 to 255.
        struct LayerMips
        {
            struct Level
            {
                std::uint32_t mWidth = 1;
                std::uint32_t mHeight = 1;
                std::vector<float> mRgb;
            };
            std::vector<Level> mLevels;
            bool mMissing = true;
        };

        LayerMips makeMips(const Rgba8Image* image)
        {
            LayerMips mips;
            if (image == nullptr || image->mWidth == 0 || image->mHeight == 0
                || image->mPixels.size() < std::size_t{ image->mWidth } * image->mHeight * 4)
                return mips;
            mips.mMissing = false;
            LayerMips::Level base;
            base.mWidth = image->mWidth;
            base.mHeight = image->mHeight;
            base.mRgb.resize(std::size_t{ base.mWidth } * base.mHeight * 3);
            for (std::size_t i = 0; i < std::size_t{ base.mWidth } * base.mHeight; ++i)
            {
                for (std::size_t c = 0; c < 3; ++c)
                    base.mRgb[3 * i + c] = static_cast<float>(image->mPixels[4 * i + c]);
            }
            mips.mLevels.push_back(std::move(base));
            while (mips.mLevels.back().mWidth > 1 || mips.mLevels.back().mHeight > 1)
            {
                const LayerMips::Level& previous = mips.mLevels.back();
                LayerMips::Level next;
                next.mWidth = std::max(1u, previous.mWidth / 2);
                next.mHeight = std::max(1u, previous.mHeight / 2);
                next.mRgb.resize(std::size_t{ next.mWidth } * next.mHeight * 3);
                for (std::uint32_t y = 0; y < next.mHeight; ++y)
                {
                    for (std::uint32_t x = 0; x < next.mWidth; ++x)
                    {
                        // The (up to) 2 x 2 texels of the finer level that this one covers.
                        const std::uint32_t x0 = std::min(2 * x, previous.mWidth - 1);
                        const std::uint32_t x1 = std::min(2 * x + 1, previous.mWidth - 1);
                        const std::uint32_t y0 = std::min(2 * y, previous.mHeight - 1);
                        const std::uint32_t y1 = std::min(2 * y + 1, previous.mHeight - 1);
                        for (std::size_t c = 0; c < 3; ++c)
                        {
                            const float sum = previous.mRgb[(std::size_t{ y0 } * previous.mWidth + x0) * 3 + c]
                                + previous.mRgb[(std::size_t{ y0 } * previous.mWidth + x1) * 3 + c]
                                + previous.mRgb[(std::size_t{ y1 } * previous.mWidth + x0) * 3 + c]
                                + previous.mRgb[(std::size_t{ y1 } * previous.mWidth + x1) * 3 + c];
                            next.mRgb[(std::size_t{ y } * next.mWidth + x) * 3 + c] = sum * 0.25f;
                        }
                    }
                }
                mips.mLevels.push_back(std::move(next));
            }
            return mips;
        }

        // Bilinear read with wrap-around (the sampler repeats) at texture coordinates in repeats.
        void sampleLevel(const LayerMips::Level& level, double u, double v, float (&rgb)[3])
        {
            const double x = (u - std::floor(u)) * level.mWidth - 0.5;
            const double y = (v - std::floor(v)) * level.mHeight - 0.5;
            const double fx = std::floor(x);
            const double fy = std::floor(y);
            const float tx = static_cast<float>(x - fx);
            const float ty = static_cast<float>(y - fy);
            const auto wrap = [](double value, std::uint32_t size) {
                const long long n = static_cast<long long>(size);
                long long i = static_cast<long long>(value) % n;
                if (i < 0)
                    i += n;
                return static_cast<std::size_t>(i);
            };
            const std::size_t x0 = wrap(fx, level.mWidth);
            const std::size_t x1 = wrap(fx + 1, level.mWidth);
            const std::size_t y0 = wrap(fy, level.mHeight);
            const std::size_t y1 = wrap(fy + 1, level.mHeight);
            for (std::size_t c = 0; c < 3; ++c)
            {
                const float a = level.mRgb[(y0 * level.mWidth + x0) * 3 + c];
                const float b = level.mRgb[(y0 * level.mWidth + x1) * 3 + c];
                const float d = level.mRgb[(y1 * level.mWidth + x0) * 3 + c];
                const float e = level.mRgb[(y1 * level.mWidth + x1) * 3 + c];
                rgb[c] = (a * (1 - tx) + b * tx) * (1 - ty) + (d * (1 - tx) + e * tx) * ty;
            }
        }

        // Trilinear: `lod` is the mip level the footprint calls for (0 or more).
        void sampleLayer(const LayerMips& mips, double u, double v, double lod, float (&rgb)[3])
        {
            if (mips.mMissing)
            {
                rgb[0] = rgb[1] = rgb[2] = missingLayerGrey;
                return;
            }
            const double top = static_cast<double>(mips.mLevels.size() - 1);
            const double clamped = std::clamp(lod, 0.0, top);
            const std::size_t lower = static_cast<std::size_t>(std::floor(clamped));
            const std::size_t upper = std::min(lower + 1, mips.mLevels.size() - 1);
            const float blend = static_cast<float>(clamped - static_cast<double>(lower));
            float a[3];
            sampleLevel(mips.mLevels[lower], u, v, a);
            if (blend <= 0.f || upper == lower)
            {
                std::copy(a, a + 3, rgb);
                return;
            }
            float b[3];
            sampleLevel(mips.mLevels[upper], u, v, b);
            for (std::size_t c = 0; c < 3; ++c)
                rgb[c] = a[c] * (1 - blend) + b[c] * blend;
        }
    }

    namespace
    {
        // Task 0361: the weights and layer indices of the picture point (u, v) in [0, 1] of a splat blend map, the
        // maths of terrain/main.frag's splat branch (Splat::lookupFor): four texel reads blended bilinearly inside one
        // quadrant slot, never across two slots.
        void splatTexel(const std::vector<std::uint8_t>& blend, std::uint32_t k, std::uint32_t s, double u, double v,
            double (&weights)[4], std::size_t (&ids)[4])
        {
            const std::size_t width = std::size_t{ k } * s;
            const double slotX = std::clamp(u, 0.0, 1.0) * k;
            const double slotY = std::clamp(v, 0.0, 1.0) * k;
            const std::uint32_t sx = std::min(static_cast<std::uint32_t>(slotX), k - 1);
            const std::uint32_t sy = std::min(static_cast<std::uint32_t>(slotY), k - 1);
            const double vertexX = (slotX - sx) * (s - 1);
            const double vertexY = (slotY - sy) * (s - 1);
            const std::uint32_t x0 = std::min(static_cast<std::uint32_t>(vertexX), s - 1);
            const std::uint32_t y0 = std::min(static_cast<std::uint32_t>(vertexY), s - 1);
            const std::uint32_t x1 = std::min(x0 + 1, s - 1);
            const std::uint32_t y1 = std::min(y0 + 1, s - 1);
            const double fx = vertexX - x0;
            const double fy = vertexY - y0;
            const auto texel = [&](std::uint32_t x, std::uint32_t y, int channel) {
                return static_cast<double>(blend[((std::size_t{ sy } * s + y) * width + std::size_t{ sx } * s + x) * 4
                           + static_cast<std::size_t>(channel)])
                    / 255.0;
            };
            for (int channel = 0; channel < 4; ++channel)
            {
                const double low = texel(x0, y0, channel) * (1 - fx) + texel(x1, y0, channel) * fx;
                const double high = texel(x0, y1, channel) * (1 - fx) + texel(x1, y1, channel) * fx;
                weights[channel] = low * (1 - fy) + high * fy;
                ids[channel] = blend[((width + sy) * width + sx) * 4 + static_cast<std::size_t>(channel)];
            }
        }
    }

    LodError bakeAlbedoImage(const AlbedoSource& source, std::int32_t cellX, std::int32_t cellY,
        std::uint32_t sizeInCells, std::uint32_t albedoSize, std::vector<std::uint8_t>& rgba,
        std::uint32_t verticesPerCell)
    {
        rgba.clear();
        if (LodError error = validate(sizeInCells, albedoSize))
            return error;
        if (verticesPerCell < 3)
            return fail("verticesPerCell", "a cell needs at least 3 vertices a side");
        std::vector<std::uint8_t> blend;
        std::vector<std::uint8_t> colours;
        if (LodError error = fetch(source, cellX, cellY, sizeInCells, verticesPerCell, blend, colours))
            return error;

        const std::size_t layerCount = source.layerCount();
        std::vector<LayerMips> layers(layerCount);
        std::vector<bool> prepared(layerCount, false);
        const std::uint32_t blendSize = blendTexelsPerCell * sizeInCells;
        const std::uint32_t vertices = (verticesPerCell - 1) * sizeInCells + 1;
        const double repeatsPerTexel = static_cast<double>(blendSize) / albedoSize;
        const std::uint32_t splatSide = source.splatSide(sizeInCells);
        const std::uint32_t splatK = 2 * sizeInCells;

        std::vector<std::uint8_t> out(std::size_t{ albedoSize } * albedoSize * 4, 255);
        for (std::uint32_t j = 0; j < albedoSize; ++j)
        {
            const double v = (j + 0.5) / albedoSize;
            for (std::uint32_t i = 0; i < albedoSize; ++i)
            {
                const double u = (i + 0.5) / albedoSize;
                const double coordX = u * blendSize;
                const double coordY = v * blendSize;
                double weights[4];
                std::size_t ids[4];
                if (splatSide != 0)
                    splatTexel(blend, splatK, splatSide, u, v, weights, ids);
                else
                {
                    const std::uint32_t texelX = std::min(static_cast<std::uint32_t>(coordX), blendSize - 1);
                    const std::uint32_t texelY = std::min(static_cast<std::uint32_t>(coordY), blendSize - 1);
                    const double px = std::clamp((coordX - texelX - 0.5) * 2 + 0.5, 0.0, 1.0);
                    const double py = std::clamp((coordY - texelY - 0.5) * 2 + 0.5, 0.0, 1.0);
                    weights[0] = (1 - px) * (1 - py);
                    weights[1] = px * (1 - py);
                    weights[2] = (1 - px) * py;
                    weights[3] = px * py;
                    const std::uint8_t* indices = &blend[(std::size_t{ texelY } * blendSize + texelX) * 4];
                    for (int corner = 0; corner < 4; ++corner)
                        ids[corner] = indices[corner];
                }

                double sum[3] = { 0, 0, 0 };
                for (int corner = 0; corner < 4; ++corner)
                {
                    if (weights[corner] <= 0.0)
                        continue;
                    const std::size_t index = ids[corner];
                    float rgb[3];
                    if (index >= layerCount)
                    {
                        rgb[0] = rgb[1] = rgb[2] = missingLayerGrey;
                    }
                    else
                    {
                        if (!prepared[index])
                        {
                            layers[index] = makeMips(source.layerImage(index));
                            prepared[index] = true;
                        }
                        const LayerMips& mips = layers[index];
                        double lod = 0;
                        if (!mips.mMissing)
                        {
                            const double footprint
                                = repeatsPerTexel * std::max(mips.mLevels[0].mWidth, mips.mLevels[0].mHeight);
                            lod = footprint > 1.0 ? std::log2(footprint) : 0.0;
                            // Task 0361: a splat (neutral world) picture holds each layer as its mean colour. With 4 or
                            // fewer picture texels to a repeat of the ground texture the mip level the footprint calls
                            // for still has a pattern a texel or two wide, which the picture cannot hold: it showed as
                            // a regular lattice of dots on snow slopes.
                            if (splatSide != 0)
                                lod = static_cast<double>(mips.mLevels.size() - 1);
                        }
                        sampleLayer(mips, coordX, coordY, lod, rgb);
                    }
                    for (int c = 0; c < 3; ++c)
                        sum[c] += weights[corner] * rgb[c];
                }

                // The vertex colour, bilinear over the vertex grid.
                const double gx = u * (vertices - 1);
                const double gy = v * (vertices - 1);
                const std::uint32_t vx0 = std::min(static_cast<std::uint32_t>(gx), vertices - 2);
                const std::uint32_t vy0 = std::min(static_cast<std::uint32_t>(gy), vertices - 2);
                const double tx = gx - vx0;
                const double ty = gy - vy0;
                std::uint8_t* pixel = &out[(std::size_t{ j } * albedoSize + i) * 4];
                for (std::size_t c = 0; c < 3; ++c)
                {
                    const auto vertex = [&](std::uint32_t x, std::uint32_t y) {
                        return static_cast<double>(colours[(std::size_t{ y } * vertices + x) * 3 + c]);
                    };
                    const double colour = (vertex(vx0, vy0) * (1 - tx) + vertex(vx0 + 1, vy0) * tx) * (1 - ty)
                        + (vertex(vx0, vy0 + 1) * (1 - tx) + vertex(vx0 + 1, vy0 + 1) * tx) * ty;
                    const double value = sum[c] * colour / 255.0;
                    pixel[c] = static_cast<std::uint8_t>(std::clamp(std::lround(value), 0l, 255l));
                }
            }
        }
        rgba = std::move(out);
        return {};
    }

    LodError albedoInputsDigest(const AlbedoSource& source, std::int32_t cellX, std::int32_t cellY,
        std::uint32_t sizeInCells, std::uint32_t albedoSize, EdenArchive::Hash& digest,
        std::vector<std::uint32_t>* usedLayers, std::uint32_t verticesPerCell)
    {
        digest = {};
        if (usedLayers != nullptr)
            usedLayers->clear();
        if (LodError error = validate(sizeInCells, albedoSize))
            return error;
        if (verticesPerCell < 3)
            return fail("verticesPerCell", "a cell needs at least 3 vertices a side");
        std::vector<std::uint8_t> blend;
        std::vector<std::uint8_t> colours;
        if (LodError error = fetch(source, cellX, cellY, sizeInCells, verticesPerCell, blend, colours))
            return error;

        const auto putU32 = [](EdenArchive::Hasher& hasher, std::uint32_t value) {
            std::uint8_t bytes[4];
            for (int i = 0; i < 4; ++i)
                bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
            hasher.update(bytes, 4);
        };
        EdenArchive::Hasher hasher;
        putU32(hasher, albedoAlgorithmVersion);
        putU32(hasher, sizeInCells);
        putU32(hasher, albedoSize);
        const std::uint32_t splatSide = source.splatSide(sizeInCells);
        if (splatSide != 0)
        {
            putU32(hasher, 0x53504c54u); // "SPLT": only a splat bake adds these words
            putU32(hasher, splatSide);
            putU32(hasher, splatAlgorithmVersion);
        }
        hasher.update(blend.data(), blend.size());
        hasher.update(colours.data(), colours.size());

        const std::size_t layerCount = source.layerCount();
        std::vector<bool> used(layerCount, false);
        // In a splat map only the rows under the weight slots hold layer indices (the weights are not indices).
        const std::size_t firstIndexByte = splatSide != 0
            ? std::size_t{ 2 } * sizeInCells * splatSide * std::size_t{ 2 } * sizeInCells * splatSide * 4
            : 0;
        for (std::size_t i = firstIndexByte; i < blend.size(); ++i)
        {
            if (blend[i] < layerCount)
                used[blend[i]] = true;
        }
        for (std::size_t index = 0; index < layerCount; ++index)
        {
            if (!used[index])
                continue;
            putU32(hasher, static_cast<std::uint32_t>(index));
            if (usedLayers != nullptr)
                usedLayers->push_back(static_cast<std::uint32_t>(index));
            const EdenArchive::Hash layer = source.layerDigest(index);
            hasher.update(layer.data(), layer.size());
        }
        digest = hasher.finalize();
        return {};
    }
}
