#include "storage.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include <components/terrain/bounds.hpp>

#include "splat.hpp"

namespace NeutralTerrain
{
    namespace sdk = openeden::sdk::game;

    namespace
    {
        float cellSizeOf(const std::shared_ptr<CellCache>& cache)
        {
            return cache && cache->hasWorld() ? cache->world().cellSizeUnits : 4096.f;
        }
        std::uint32_t vertsOf(const std::shared_ptr<CellCache>& cache)
        {
            return cache && cache->hasWorld() ? cache->world().vertsPerSide : 33u;
        }

        std::int64_t floorDiv(std::int64_t a, std::int64_t b)
        {
            std::int64_t q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0)))
                --q;
            return q;
        }

        // The cells one call reads, looked up once each (the cache takes a lock per lookup).
        class Local
        {
        public:
            explicit Local(const CellCache& cache)
                : mCache(cache)
            {
            }
            const sdk::TerrainCell* at(std::int32_t cx, std::int32_t cy)
            {
                const auto key = std::make_pair(cx, cy);
                const auto found = mCells.find(key);
                if (found != mCells.end())
                    return found->second.get();
                auto cell = mCache.get(cx, cy);
                const sdk::TerrainCell* raw = cell.get();
                mCells.emplace(key, std::move(cell));
                return raw;
            }

        private:
            const CellCache& mCache;
            std::map<std::pair<std::int32_t, std::int32_t>, std::shared_ptr<const sdk::TerrainCell>> mCells;
        };

        struct Sample
        {
            float mHeight = 0.f;
            vsg::vec3 mNormal{ 0.f, 0.f, 1.f };
            vsg::ubvec4 mColour{ 255, 255, 255, 255 };
        };

        // The vertex at global fine index (gx, gy): the cell it is local 0 of, or, where that cell has no land, the
        // neighbour towards the south-west that has (its last row or column).
        Sample sampleVertex(Local& local, const CellCache& cache, std::int64_t gx, std::int64_t gy)
        {
            const std::int64_t intervals = static_cast<std::int64_t>(cache.world().vertsPerSide) - 1;
            const std::uint32_t verts = cache.world().vertsPerSide;
            const std::int64_t cx = floorDiv(gx, intervals);
            const std::int64_t cy = floorDiv(gy, intervals);
            const std::int64_t lx = gx - cx * intervals;
            const std::int64_t ly = gy - cy * intervals;
            Sample out;
            out.mHeight = cache.world().defaultLandHeight;
            if (cx < INT32_MIN + 1 || cx > INT32_MAX - 1 || cy < INT32_MIN + 1 || cy > INT32_MAX - 1)
                return out;
            for (int step = 0; step < 4; ++step)
            {
                const int dx = (step & 1) ? -1 : 0;
                const int dy = (step & 2) ? -1 : 0;
                if ((dx != 0 && lx != 0) || (dy != 0 && ly != 0))
                    continue;
                const sdk::TerrainCell* cell
                    = local.at(static_cast<std::int32_t>(cx + dx), static_cast<std::int32_t>(cy + dy));
                if (!cell)
                    continue;
                const std::int64_t ux = dx != 0 ? intervals : lx;
                const std::int64_t uy = dy != 0 ? intervals : ly;
                const std::size_t index = static_cast<std::size_t>(uy) * verts + static_cast<std::size_t>(ux);
                if (cell->hasHeights && index < cell->heights.size())
                    out.mHeight = cell->heights[index];
                if (cell->hasNormals && index * 3 + 2 < cell->normals.size())
                {
                    vsg::vec3 n(static_cast<float>(cell->normals[index * 3]),
                        static_cast<float>(cell->normals[index * 3 + 1]), static_cast<float>(cell->normals[index * 3 + 2]));
                    const float length = vsg::length(n);
                    if (length > 1e-6f)
                        out.mNormal = n / length;
                }
                if (cell->hasColours && index * 3 + 2 < cell->colours.size())
                    out.mColour = vsg::ubvec4(
                        cell->colours[index * 3], cell->colours[index * 3 + 1], cell->colours[index * 3 + 2], 255);
                return out;
            }
            return out;
        }
    }

    Storage::Storage(std::shared_ptr<CellCache> cache)
        : Terrain::Storage(cellSizeOf(cache), vertsOf(cache))
        , mCache(std::move(cache))
    {
    }

    float Storage::vertexHeight(std::int64_t gx, std::int64_t gy) const
    {
        Local local(*mCache);
        return sampleVertex(local, *mCache, gx, gy).mHeight;
    }

    Terrain::Storage::VertexData Storage::getVertexData(int lodLevel, const Terrain::Bounds& bounds)
    {
        const std::uint32_t count = numVerts(bounds.size, static_cast<std::uint8_t>(lodLevel));
        auto heights = vsg::floatArray2D::create(count, count, vsg::Data::Properties{ VK_FORMAT_R32_SFLOAT });
        // RGBA rather than RGB: three-channel 32-bit float images are not sampleable on common Windows drivers (the
        // same choice as ESMTerrain::Storage).
        auto normals
            = vsg::vec4Array2D::create(count, count, vsg::Data::Properties{ VK_FORMAT_R32G32B32A32_SFLOAT });
        auto colours = vsg::ubvec4Array2D::create(count, count, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });

        const std::int64_t intervals = static_cast<std::int64_t>(cellVertices) - 1;
        const std::int64_t gx0 = std::llround(static_cast<double>(bounds.min.x) * static_cast<double>(intervals));
        const std::int64_t gy0 = std::llround(static_cast<double>(bounds.min.y) * static_cast<double>(intervals));
        const std::int64_t step = std::int64_t{ 1 } << lodLevel;

        Local local(*mCache);
        for (std::uint32_t vy = 0; vy < count; ++vy)
            for (std::uint32_t vx = 0; vx < count; ++vx)
            {
                const Sample s = sampleVertex(local, *mCache, gx0 + static_cast<std::int64_t>(vx) * step,
                    gy0 + static_cast<std::int64_t>(vy) * step);
                (*heights)(vx, vy) = s.mHeight;
                (*normals)(vx, vy) = vsg::vec4(s.mNormal.x, s.mNormal.y, s.mNormal.z, 0.f);
                (*colours)(vx, vy) = s.mColour;
            }
        return { heights, normals, colours };
    }

    namespace
    {
        std::int64_t floorMod(std::int64_t a, std::int64_t b)
        {
            const std::int64_t r = a % b;
            return (r != 0 && ((r < 0) != (b < 0))) ? r + b : r;
        }

        // The splat blend map (splat.hpp) of one chunk. The chunk's quadrant slots are k x k whole quadrants, or one slot
        // holding the part of a quadrant the chunk covers when it is smaller than a quadrant.
        vsg::ref_ptr<vsg::Data> splatBlendmap(const Terrain::Bounds& bounds, const CellCache& cache)
        {
            const Splat::Layout layout = Splat::layoutFor(bounds.size);
            auto image = vsg::ubvec4Array2D::create(layout.mWidth, layout.mHeight, vsg::ubvec4(),
                vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });

            // The chunk's first quadrant, and for a chunk inside one quadrant the vertex it starts at.
            const double qx = static_cast<double>(bounds.min.x) * 2.0;
            const double qy = static_cast<double>(bounds.min.y) * 2.0;
            const std::int64_t firstX = static_cast<std::int64_t>(layout.mK == 1 ? std::floor(qx) : std::llround(qx));
            const std::int64_t firstY = static_cast<std::int64_t>(layout.mK == 1 ? std::floor(qy) : std::llround(qy));
            int offsetX = 0;
            int offsetY = 0;
            if (layout.mK == 1)
            {
                offsetX = static_cast<int>(std::llround((qx - static_cast<double>(firstX)) * (Splat::kSide - 1)));
                offsetY = static_cast<int>(std::llround((qy - static_cast<double>(firstY)) * (Splat::kSide - 1)));
            }

            Local local(cache);
            for (int sy = 0; sy < layout.mK; ++sy)
                for (int sx = 0; sx < layout.mK; ++sx)
                {
                    const std::int64_t gx = firstX + sx;
                    const std::int64_t gy = firstY + sy;
                    const std::int64_t cx = floorDiv(gx, 2);
                    const std::int64_t cy = floorDiv(gy, 2);
                    const int quadrant = static_cast<int>(floorMod(gy, 2) * 2 + floorMod(gx, 2));
                    const sdk::TerrainCell* cell = nullptr;
                    if (cx > INT32_MIN && cx < INT32_MAX && cy > INT32_MIN && cy < INT32_MAX)
                        cell = local.at(static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cy));

                    Splat::Quadrant composed;
                    if (cell)
                    {
                        const auto indexOf = [&](sdk::WorldId id) {
                            const std::size_t i = cache.textureIndex(id);
                            return static_cast<std::uint8_t>(i < 256 ? i : 0);
                        };
                        std::vector<Splat::LayerIn> inputs;
                        for (const sdk::TerrainLayer& layer : cell->layers[static_cast<std::size_t>(quadrant)])
                            inputs.push_back({ indexOf(layer.texture), &layer.opacity });
                        composed = Splat::composeQuadrant(indexOf(cell->baseTextures[static_cast<std::size_t>(quadrant)]), inputs);
                    }
                    else
                    {
                        composed = Splat::composeQuadrant(0, std::span<const Splat::LayerIn>());
                    }

                    for (int ty = 0; ty < layout.mS; ++ty)
                        for (int tx = 0; tx < layout.mS; ++tx)
                        {
                            const int vx = std::min(offsetX + tx, Splat::kSide - 1);
                            const int vy = std::min(offsetY + ty, Splat::kSide - 1);
                            const auto& w = composed.mWeights[static_cast<std::size_t>(vy * Splat::kSide + vx)];
                            (*image)(sx * layout.mS + tx, sy * layout.mS + ty) = vsg::ubvec4(w[0], w[1], w[2], w[3]);
                        }
                    (*image)(sx, layout.mWidth + sy)
                        = vsg::ubvec4(composed.mIds[0], composed.mIds[1], composed.mIds[2], composed.mIds[3]);
                }
            return image;
        }
    }

    vsg::ref_ptr<vsg::Data> Storage::getBlendmap(const Terrain::Bounds& bounds)
    {
        if (mLayerSplat && bounds.size <= Splat::kMaxChunkCells)
            return splatBlendmap(bounds, *mCache);
        const int size = std::max(1, static_cast<int>(std::lround(kBlendTexelsPerCell * bounds.size)));
        auto image = vsg::ubvec4Array2D::create(
            size, size, vsg::ubvec4(), vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        const std::int64_t x0 = std::llround(static_cast<double>(bounds.min.x) * kBlendTexelsPerCell);
        const std::int64_t y0 = std::llround(static_cast<double>(bounds.min.y) * kBlendTexelsPerCell);
        Local local(*mCache);
        constexpr std::int64_t half = kBlendTexelsPerCell / 2;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const std::int64_t gx = x0 + x;
                const std::int64_t gy = y0 + y;
                const std::int64_t cx = floorDiv(gx, kBlendTexelsPerCell);
                const std::int64_t cy = floorDiv(gy, kBlendTexelsPerCell);
                unsigned char index = 0;
                if (cx > INT32_MIN && cx < INT32_MAX && cy > INT32_MIN && cy < INT32_MAX)
                    if (const sdk::TerrainCell* cell
                        = local.at(static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cy)))
                    {
                        const std::int64_t lx = gx - cx * kBlendTexelsPerCell;
                        const std::int64_t ly = gy - cy * kBlendTexelsPerCell;
                        const int quadrant = (ly >= half ? 2 : 0) + (lx >= half ? 1 : 0);
                        const std::size_t layer = mCache->textureIndex(cell->baseTextures[quadrant]);
                        index = static_cast<unsigned char>(layer < 256 ? layer : 0);
                    }
                (*image)(x, y) = vsg::ubvec4(index, index, index, index);
            }
        return image;
    }

    std::vector<Terrain::LayerInfo> Storage::getLayers()
    {
        std::vector<Terrain::LayerInfo> layers;
        for (const sdk::GroundTexture& t : mCache->textures())
        {
            if (layers.size() >= 256)
                break; // Terrain::Builder holds at most 256 layers
            std::string path = t.diffusePath;
            std::replace(path.begin(), path.end(), '\\', '/');
            layers.push_back(std::move(path));
        }
        return layers;
    }

    float Storage::getHeightAt(const vsg::vec3& worldPos, ESM::RefId)
    {
        const float fallback = mCache->world().defaultLandHeight;
        if (!std::isfinite(worldPos.x) || !std::isfinite(worldPos.y))
            return fallback;
        const double cellX = std::floor(static_cast<double>(worldPos.x) / cellWorldSize);
        const double cellY = std::floor(static_cast<double>(worldPos.y) / cellWorldSize);
        if (std::fabs(cellX) > 1.0e6 || std::fabs(cellY) > 1.0e6)
            return fallback;
        const auto cell = mCache->get(static_cast<std::int32_t>(cellX), static_cast<std::int32_t>(cellY));
        if (!cell || !cell->hasHeights)
            return fallback;

        const int verts = static_cast<int>(cellVertices);
        const double factor = verts - 1.0;
        const double nX = (static_cast<double>(worldPos.x) - cellX * cellWorldSize) / cellWorldSize * factor;
        const double nY = (static_cast<double>(worldPos.y) - cellY * cellWorldSize) / cellWorldSize * factor;
        const int startX = std::clamp(static_cast<int>(nX), 0, verts - 1);
        const int startY = std::clamp(static_cast<int>(nY), 0, verts - 1);
        const int endX = std::min(startX + 1, verts - 1);
        const int endY = std::min(startY + 1, verts - 1);
        const double xParam = nX - startX;
        const double yParam = nY - startY;
        const auto h = [&](int x, int y) { return static_cast<double>(cell->heights[static_cast<std::size_t>(y) * verts + x]); };
        const double h0 = h(startX, startY);
        const double h1 = h(endX, startY);
        const double h2 = h(endX, endY);
        const double h3 = h(startX, endY);
        // The two triangles of a quad, split along the diagonal from (end, start) to (start, end), as ESMTerrain::Storage.
        if ((1.0 - yParam) > xParam)
            return static_cast<float>(h0 + (h1 - h0) * xParam + (h3 - h0) * yParam);
        return static_cast<float>(h2 + (h3 - h2) * (1.0 - xParam) + (h1 - h2) * (1.0 - yParam));
    }
}
