#include "paging.hpp"

#include <algorithm>
#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/misc/mathutil.hpp>
#include <components/vsgutil/convert.hpp>

#include "batchindex.hpp"
#include "builder.hpp"
#include "lod.hpp"
#include "storage.hpp"
#include "viewdata.hpp"

namespace Terrain
{
    Paging::Paging(vsg::ref_ptr<vsgUtil::CompileContext> compile, vsg::ref_ptr<const vsg::Options> imageOptions,
        vsg::ref_ptr<const vsg::Options> shaderOptions, vsg::ref_ptr<vsg::Sampler> samplerOptions,
        TerrainMapSettings mapSettings)
        : mCompile(compile)
    {
        mBuilder = std::make_unique<Builder>(imageOptions, shaderOptions, samplerOptions, std::move(mapSettings));
    }

    Paging::~Paging() {}

    std::unique_ptr<View> Paging::createView() const
    {
        return std::make_unique<ViewData>();
    }

    int Paging::getBatchSize(float cellViewDist) const
    {
        int batchSize = std::max(1, Misc::nextPowerOfTwo(std::ceil(cellViewDist * 2 / 4)));
        return std::min(mMaxBatchSize, batchSize);
    }

    vsg::ref_ptr<vsgUtil::CompileOp> Paging::updateViewForCell(
        View& v, const vsg::ivec2& cell, float referenceViewDistance, vsg::vec2* heightRange) const
    {
        ViewData& view = static_cast<ViewData&>(v);

        int batchSize = getBatchSize(referenceViewDistance / mStorage->cellWorldSize);
        int x = batchIndex(cell.x, batchSize);
        int y = batchIndex(cell.y, batchSize);
        Bounds batchBounds{ static_cast<float>(batchSize), vsg::vec2(x * batchSize, y * batchSize) };
        auto batch = mBuilder->createBatch(batchBounds, 0);
        Bounds drawBounds{ 1.f, vsg::vec2(cell) };
        auto draw = mBuilder->createDraw(batch, drawBounds, 0, 0);
        if (heightRange != nullptr)
            *heightRange = mBuilder->heightRange(batch.data, batchBounds, drawBounds);
        batch.leafGroup->addChild(draw);

        for (auto& child : view.rootNode->children)
            mCompile->detach(child);
        view.rootNode->children = { batch.root };
        return vsg::ref_ptr{ new vsgUtil::CompileOp(view.rootNode, mCompile, [&view]() { view.clear(); }) };
    }

    vsg::ref_ptr<vsgUtil::CompileOp> Paging::updateView(View& v, const vsg::dvec3& viewPoint, float viewDistance,
        bool preload, const Coverage* coverage, const ChunkFadeParams* fade) const
    {
        ViewData& view = static_cast<ViewData&>(v);
        const bool fading = fade != nullptr && fade->fadeSeconds > 0.f && !preload;
        if (fading && view.fader.active())
        {
            // Move the fades on and write them into the draws that are in the scene (the command graph is recorded
            // every frame). A chunk that was dropped needs the draws rebuilt, below.
            if (view.fader.step(fade->deltaSeconds, fade->fadeSeconds))
                view.fadeDirty = true;
            for (const ViewData::FadeDraw& item : view.fadeDraws)
                item.draw->firstInstance = encodeChunkFade(item.baseInstance, view.fader.fadeOf(item.bounds));
        }

        const uint64_t coverageVersion = coverage ? coverage->version + 1 : 0;
        const unsigned int previousCount = view.numEntries;
        const bool coverageChanged = view.coverageVersion != coverageVersion;
        if (vsg::length2(view.viewPoint - viewPoint) > View::sqrReuseDistance || view.viewDistance != viewDistance
            || view.coverageVersion != coverageVersion || view.fadeDirty)
        {
            view.reset();
            view.coverageVersion = coverageVersion;
            view.viewPoint = viewPoint;
            view.viewDistance = viewDistance;

            const double cellWorldSize = mStorage->cellWorldSize;
            vsg::dvec2 viewCell = vsgUtil::toVec2(viewPoint) / cellWorldSize;
            double cellViewDistance = viewDistance / cellWorldSize;
            int batchSize = getBatchSize(cellViewDistance);

            double cellViewDistanceLoad = cellViewDistance + View::reuseDistance / mStorage->cellWorldSize;
            if (preload)
                cellViewDistanceLoad += 1.0;

            int minX = std::floor((viewCell.x - cellViewDistanceLoad) / batchSize);
            int maxX = std::ceil((viewCell.x + cellViewDistanceLoad) / batchSize);
            int minY = std::floor((viewCell.y - cellViewDistanceLoad) / batchSize);
            int maxY = std::ceil((viewCell.y + cellViewDistanceLoad) / batchSize);

            std::function<void(const Bounds& bounds)> traverse
                = [this, &view, viewCell, cellViewDistanceLoad, coverage, &traverse](const Bounds& bounds) {
                      float cellDist = distance(bounds, viewCell);
                      if (cellDist > cellViewDistanceLoad)
                          return;
                      // Task 0055: something else draws this square. A chunk only partly inside is split.
                      if (coverage && coverage->contains(bounds))
                          return;
                      // A chunk next to a covered square must be at least as detailed as the square's draw (the
                      // draw does not stitch itself to coarser neighbours), so it is split until it is.
                      bool tooCoarseForCoverage = false;
                      if (coverage && bounds.size > Bounds::minSize)
                      {
                          const int touched = coverage->touchedVertexLod(bounds);
                          tooCoarseForCoverage
                              = touched >= 0 && static_cast<int>(getVertexLod(bounds.size, vertexLodMod)) > touched;
                      }
                      if (isSufficientDetail(bounds.size, cellDist, lodFactor)
                          && !(coverage && coverage->intersects(bounds)) && !tooCoarseForCoverage)
                      {
                          view.add(bounds, cellDist);
                      }
                      else
                      {
                          float childSize = bounds.radius();
                          traverse({ childSize, bounds.min });
                          traverse({ childSize, bounds.min + vsg::vec2(childSize, childSize) });
                          traverse({ childSize, bounds.min + vsg::vec2(0.f, childSize) });
                          traverse({ childSize, bounds.min + vsg::vec2(childSize, 0.f) });
                      }
                  };

            struct Batch
            {
                size_t start;
                size_t end;
                float distance;
                Bounds bounds;
                std::vector<Bounds> leaving; // task 0088: chunks that stay drawn while they fade out
            };
            std::vector<Batch> batches;

            for (int x = minX; x < maxX; ++x)
            {
                for (int y = minY; y < maxY; ++y)
                {
                    Bounds bounds{ static_cast<float>(batchSize), vsg::vec2(x * batchSize, y * batchSize) };

                    auto start = view.numEntries;
                    traverse(bounds);
                    auto end = view.numEntries;
                    if (start != end)
                    {
                        float dist = distance(bounds, viewCell);
                        batches.push_back(Batch{ start, end, dist, bounds, {} });
                    }
                }
            }

            // Task 0088: hand the wanted chunks to the fader, and add the chunks it keeps drawn as they leave.
            std::vector<Bounds> leaving;
            if (fading)
            {
                std::vector<Bounds> wanted;
                wanted.reserve(view.numEntries);
                for (unsigned int i = 0; i < view.numEntries; ++i)
                    wanted.push_back(view.entries[i].bounds);
                // A new view with nothing in common with the old one (a teleport, a first build) starts solid; so does
                // a chunk where something else (a LOD tile) drew a moment ago, and a leaving chunk goes at once where
                // something else draws now.
                const bool instant = !view.fader.sharesAreaWith(wanted);
                const std::vector<Bounds> drawnBefore = view.previousCoverage;
                view.fader.setDesired(
                    wanted, instant,
                    [coverage](const Bounds& b) { return coverage != nullptr && coverage->contains(b); },
                    [&drawnBefore](const Bounds& b) {
                        return std::any_of(drawnBefore.begin(), drawnBefore.end(),
                            [&b](const Bounds& square) { return boundsOverlap(square, b); });
                    });
                view.previousCoverage.clear();
                if (coverage != nullptr)
                {
                    for (const CoveredSquare& square : coverage->squares)
                        view.previousCoverage.push_back(square.bounds);
                }
                leaving = view.fader.leaving();
                if (instant && view.numEntries > 0)
                    Log(Debug::Verbose) << "Terrain chunk fade: the live view starts solid (" << view.numEntries
                                        << " chunks)";
                else if (!leaving.empty() || view.fader.active())
                {
                    size_t appearing = 0;
                    for (const Bounds& b : wanted)
                        appearing += view.fader.fadeOf(b) < 1.f ? 1 : 0;
                    Log(Debug::Verbose) << "Terrain chunk fade: " << view.numEntries << " chunks, " << appearing
                                        << " fading in, " << leaving.size() << " leaving";
                }
                for (const Bounds& ghost : leaving)
                {
                    // A batch of the current size that holds the chunk; one bigger than a batch cannot be drawn.
                    if (ghost.size > static_cast<float>(batchSize))
                        continue;
                    const Bounds batchBounds{ static_cast<float>(batchSize),
                        vsg::vec2(std::floor(ghost.min.x / batchSize) * batchSize,
                            std::floor(ghost.min.y / batchSize) * batchSize) };
                    auto found = std::find_if(batches.begin(), batches.end(),
                        [&batchBounds](const Batch& batch) { return batch.bounds == batchBounds; });
                    if (found == batches.end())
                    {
                        batches.push_back(Batch{ 0, 0, distance(batchBounds, viewCell), batchBounds, {} });
                        found = batches.end() - 1;
                    }
                    found->leaving.push_back(ghost);
                }
                if (!leaving.empty() || view.fadeDirty)
                    view.changed = true;
            }
            view.fadeDirty = false;

            // Task 0055: ViewData::add notices a chunk that differs from the old one at the same index, but not a list
            // that got shorter or longer (a tile now covers the last chunks, or lets them go). With a coverage, a
            // new coverage or a new count rebuilds the view.
            if (coverage && (coverageChanged || view.numEntries != previousCount))
                view.changed = true;

            if (view.changed)
            {
                // drawFrontToBack()
                std::sort(
                    batches.begin(), batches.end(),
                    [](const Batch& lhs, const Batch& rhs) -> auto{ return lhs.distance < rhs.distance; });

                auto getLodFlags = [this, &view, coverage](const Bounds& bounds, uint32_t ourVertexLod) -> uint32_t {
                    uint32_t lodFlags = 0;
                    vsg::vec2 directions[4] = {
                        { 0.f, 1.f }, // North
                        { 1.f, 0.f }, // East
                        { 0.f, -1.f }, // South
                        { -1.f, 0.f } // West
                    };
                    for (uint32_t i = 0; i < 4; ++i)
                    {
                        auto neighbourCenter = bounds.center() + directions[i] * bounds.size;
                        auto neighbour = view.getContainingChunk(neighbourCenter);
                        // Task 0055: a neighbour that is covered by another draw is a neighbour at that draw's LOD.
                        const CoveredSquare* covered = nullptr;
                        if (!neighbour.valid() && coverage)
                        {
                            covered = coverage->find(neighbourCenter);
                            if (covered)
                                neighbour = covered->bounds;
                        }
                        // We only need to worry about neighbours less detailed than we are - neighbours with more
                        // detail will do the stitching themselves
                        // (A covered neighbour of our own size still counts when its draw has fewer vertices.)
                        if (!neighbour.valid()
                            || (covered ? covered->vertexLod <= ourVertexLod : neighbour.size <= bounds.size))
                            continue;
                        uint32_t lod = covered ? covered->vertexLod : getVertexLod(neighbour.size, vertexLodMod);
                        if (lod > ourVertexLod)
                            lodFlags |= (lod - ourVertexLod) << (4 * i);
                    }
                    return lodFlags;
                };

                for (auto& oldChild : view.rootNode->children)
                    mCompile->detach(oldChild);
                view.rootNode->children.clear();
                view.rootNode->children.reserve(batches.size());

                view.fadeDraws.clear();
                for (const auto& viewBatch : batches)
                {
                    // At the moment, applying lod to batches won't help because we have to loop over all affected
                    // cells' data anyway.
                    auto batch = mBuilder->createBatch(viewBatch.bounds, /*lod*/ 0);

                    std::vector<size_t> indices;
                    indices.reserve(viewBatch.end - viewBatch.start);
                    for (size_t i = viewBatch.start; i < viewBatch.end; ++i)
                        indices.push_back(i);
                    // drawFrontToBack
                    std::sort(
                        indices.begin(), indices.end(), [view](size_t lhs, size_t rhs) -> auto{
                            return view.entries[lhs].distance < view.entries[rhs].distance;
                        });

                    for (auto i : indices)
                    {
                        const auto& entry = view.entries[i];
                        auto vertexLod = getVertexLod(entry.bounds.size, vertexLodMod);
                        auto lodFlags = getLodFlags(entry.bounds, vertexLod);
                        if (!fading)
                        {
                            auto node = mBuilder->createDraw(batch, entry.bounds, vertexLod, lodFlags);
                            batch.leafGroup->addChild(node);
                            continue;
                        }
                        Builder::DrawHandle handle;
                        auto node = mBuilder->createDraw(
                            batch, entry.bounds, vertexLod, lodFlags, view.fader.fadeOf(entry.bounds), &handle);
                        batch.leafGroup->addChild(node);
                        view.fadeDraws.push_back({ entry.bounds, handle.draw, handle.baseInstance });
                    }
                    for (const Bounds& ghost : viewBatch.leaving)
                    {
                        auto vertexLod = getVertexLod(ghost.size, vertexLodMod);
                        auto lodFlags = getLodFlags(ghost, vertexLod);
                        Builder::DrawHandle handle;
                        auto node = mBuilder->createDraw(
                            batch, ghost, vertexLod, lodFlags, view.fader.fadeOf(ghost), &handle);
                        batch.leafGroup->addChild(node);
                        view.fadeDraws.push_back({ ghost, handle.draw, handle.baseInstance });
                    }
                    view.rootNode->children.push_back(batch.root);
                }
                return vsg::ref_ptr{ new vsgUtil::CompileOp(view.rootNode, mCompile, [&view]() { view.clear(); }) };
            }
        }
        return {};
    }

    void Paging::clearView(View& v) const
    {
        ViewData& view = static_cast<ViewData&>(v);
        for (auto& oldChild : view.rootNode->children)
            mCompile->detach(oldChild);
        view.clear();
    }

    void Paging::clearCache() const
    {
        mBuilder->clearCache();
    }

    void Paging::pruneCache() const
    {
        mBuilder->pruneCache();
    }

    void Paging::setStorage(Storage* storage)
    {
        mStorage = storage;
        mBuilder->setStorage(storage);
    }
}
