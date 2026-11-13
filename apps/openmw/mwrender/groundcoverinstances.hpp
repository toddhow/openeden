#ifndef OPENEDEN_MWRENDER_GROUNDCOVERINSTANCES_H
#define OPENEDEN_MWRENDER_GROUNDCOVERINSTANCES_H

#include <map>
#include <utility>
#include <vector>

#include <vsg/maths/vec2.h>

#include <components/esm/position.hpp>
#include <components/vfs/pathutil.hpp>

namespace MWWorld
{
    class GroundcoverStore;
}

namespace MWRender
{
    /**
     * @brief Thins groundcover by the `[Groundcover] density` setting, as OpenMW's DensityCalculator did (task 0012,
     * V4f; groundcover.cpp at 442eec6e2f).
     *
     * A density of 1 or more keeps every instance. Otherwise each call adds the density to a running total and keeps
     * the instance only when the total reaches 1, then subtracts 1, so a density of 0.25 keeps every fourth instance,
     * starting with the fourth. A density of 0 or less keeps none. reset() sets the total back to 0 (OpenMW reset it
     * for each cell).
     */
    class GroundcoverDensity
    {
    public:
        explicit GroundcoverDensity(float density)
            : mDensity(density)
        {
        }

        bool isInstanceEnabled();
        void reset() { mTotal = 0.f; }

    private:
        float mDensity;
        float mTotal = 0.f;
    };

    /**
     * @brief Whether a reference at world position (x, y) belongs to the groundcover chunk spanning cells minBound to
     * maxBound (in cell units, so a world position divides by the cell size, 8192), as OpenMW's isInChunkBorders.
     *
     * A chunk at least one cell wide in both directions takes everything (the caller only reads its cells). A smaller
     * chunk, a part of one cell, takes a position only if it is not before minBound on an axis where minBound is not a
     * whole number, and not at or after maxBound on an axis where maxBound is not a whole number.
     */
    bool isInGroundcoverChunk(float x, float y, const vsg::vec2& minBound, const vsg::vec2& maxBound);

    /**
     * @brief The exterior cells (x, y) whose square comes within `distance` world units of the world position (x, y):
     * the cells whose groundcover chunks are shown (Groundcover::update). The distance is measured from the position
     * to the nearest point of the cell's square, so the position's own cell is always included, and a distance of 0
     * gives just that cell (or the cells sharing a border the position lies on). Cells are 8192 units square, cell
     * (0, 0) spanning [0, 8192) on both axes. The result is sorted.
     */
    std::vector<std::pair<int, int>> groundcoverCellsInRange(float x, float y, float distance);

    /// The same for cells of another size (task 0153: a game pack's exterior cells; Skyrim SE's are 4096 units).
    std::vector<std::pair<int, int>> groundcoverCellsInRange(float x, float y, float distance, float cellSize);

    struct GroundcoverInstance
    {
        ESM::Position position;
        float scale = 1.f;
    };

    /// Groundcover instances of one chunk, by model.
    using GroundcoverInstances = std::map<VFS::Path::Normalized, std::vector<GroundcoverInstance>, std::less<>>;

    /**
     * @brief Collects the groundcover of the chunk of `size` cells centred on `center` (in cell units), as OpenMW's
     * Groundcover::collectInstances: each cell's references from the groundcover content files, later files overriding
     * earlier ones by reference number, deleted ones removed, thinned by density (reset per cell) and kept to the
     * chunk. Runs on any thread once the store's init() has finished; reads the content files.
     */
    GroundcoverInstances collectGroundcoverInstances(
        const MWWorld::GroundcoverStore& store, float density, float size, const vsg::vec2& center);
}

#endif
