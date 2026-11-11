#ifndef OPENMW_COMPONENTS_PUDDLE_PUDDLEBAKE_H
#define OPENMW_COMPONENTS_PUDDLE_PUDDLEBAKE_H

// OpenEden puddles (task 0095, stage W-b). Baking the puddle map of one exterior cell from the terrain heights. Pure
// code: the land comes through `LandHeightSource`, which the game implements over Terrain::Storage::getVertexData
// (apps/openmw/puddlebaker.*) and the tests implement with generated land. Nothing here reads a file or the clock.
//
// What a bake of one cell does (decisions of toddhow, 2026-11-10, recorded in the task spec):
//  1. The ground is sampled at the baker's spacing on the triangles the terrain is drawn with (see
//     `terrainHeightAt`), over a WINDOW of (2 * halo + 1)^2 cells round the cell; texels of a cell with no land, and
//     texels at or below the water level, are sinks, and the window's border is a sink (Render::Puddle).
//  2. Depressions are filled with no cap, pits are labelled (8-connected), and the capacity is measured from the pit's
//     floor (decision 1: `capacityFromFloor`).
//  3. A deep pit holds a puddle only at its floor, up to the maximum puddle depth `capacityCapSteps / 16` units
//     (decision 2; the same cap, a setting).
//  4. A pit whose pond has fewer than `minPondTexels` texels is dropped (decision 3).
//  5. The catchment ratio counts only ground whose water path to the pit is at most `catchmentRadiusUnits` long
//     (decision 5, `catchmentRatioBounded`).
//  6. The cell's own texels are stored; the height above a per-cell base fits 16 bits (decision 4).
// The result does not depend on which other cells are baked, nor on the order.

#include <cstdint>
#include <string>
#include <vector>

#include "puddlemap.hpp"

namespace Puddle
{
    /// Version of the baker's behaviour (goes into the parameter digest). Bump when the output for the same input and
    /// settings can change.
    inline constexpr std::uint32_t bakerVersion = 1;

    /// The settings of a bake. Integers on purpose: they are written into the manifest and the digest unchanged.
    struct BakeSettings
    {
        /// World units a texel. Must divide the land's vertex spacing (cellSizeUnits / (cellVertices - 1)) into a
        /// power of two number of texels.
        std::uint32_t mSpacingUnits = 64;
        /// The maximum puddle depth in 1/16 world units (decision 2): 192 is 12 units, about 17 cm. At most 255.
        std::uint32_t mCapacityCapSteps = 192;
        /// Pits whose pond has fewer texels are dropped (decision 3).
        std::uint32_t mMinPondTexels = 16;
        /// The catchment radius in world units (decision 5), measured along the water's path.
        std::uint32_t mCatchmentRadiusUnits = 1024;
        /// Cells round the cell that the window holds.
        std::uint32_t mHalo = 2;
        bool mStatics = false;
        /// The water level, whole world units; land at or below it is a sink.
        std::int32_t mWaterLevelUnits = -1;
        /// The land: world units a cell, and vertices a side (65 for Morrowind).
        std::uint32_t mCellSizeUnits = 8192;
        std::uint32_t mCellVertices = 65;

        bool operator==(const BakeSettings&) const = default;

        float capacityCap() const { return static_cast<float>(mCapacityCapSteps) / 16.f; }
        /// Texels a cell side; 0 when the settings are not valid.
        std::uint32_t texelsPerCell() const;
        /// Texels between two land vertices.
        std::uint32_t texelsPerVertexStep() const;
    };

    /// Empty when the settings can be baked with, else the first problem.
    Error validateSettings(const BakeSettings& settings);

    /// Where the land comes from. One call from one thread at a time.
    class LandHeightSource
    {
    public:
        virtual ~LandHeightSource() = default;
        /// The heights of the vertices of one cell, mCellVertices * mCellVertices values, x fastest and the south row
        /// first (the order of Terrain::Storage::getVertexData); false when the cell has no land.
        virtual bool cellHeights(std::int32_t cellX, std::int32_t cellY, std::vector<float>& heights) = 0;
    };

    /// The height of the terrain surface at texel (tx, ty) of a cell, on the triangles the renderer draws: the quad
    /// between vertices (i, j) and (i + 1, j + 1) is split along the diagonal from (i, j + 1) to (i + 1, j) when i + j
    /// is odd and along the one from (i, j) to (i + 1, j + 1) when it is even (the "diamond pattern" of
    /// Terrain::CreateIndexBuffer, components/terrain/indexbuffer.hpp; i is x, j is y). `heights` is one cell's vertex
    /// heights as `LandHeightSource::cellHeights` gives them, `texelsPerStep` (a power of two) the texels between two
    /// vertices. tx and ty may be at most (vertices - 1) * texelsPerStep - 1.
    float terrainHeightAt(const std::vector<float>& heights, std::uint32_t vertices, std::uint32_t texelsPerStep,
        std::uint32_t tx, std::uint32_t ty);

    /// What a bake measured, for the log and the tests.
    struct BakeStats
    {
        std::uint32_t mWindowTexels = 0;
        std::uint32_t mPitsFound = 0; // pits of the window with a pond, before the area filter
        std::uint32_t mPitsKept = 0; // after it, in the window
        std::uint32_t mPitsInCell = 0; // kept and with a texel in the cell
        std::uint32_t mPitsDroppedByArea = 0;
        std::uint32_t mWetTexels = 0; // texels of the cell with a pond
        /// Pits in the cell whose catchment ratio is beyond the stored range (2^17), without and with the bound.
        std::uint32_t mClippedUnbounded = 0;
        std::uint32_t mClippedBounded = 0;
        std::vector<float> mKappasBounded; // per pit in the cell
        std::vector<float> mKappasUnbounded;
        bool mHeightsClamped = false; // some height did not fit the cell's 16 bit range
        /// Pond texels of the cell dropped because their height is beyond the 16 bit range over the cell's base (a
        /// level test at run time could not use them): they have no capacity in the pack.
        std::uint32_t mPondTexelsDroppedClamped = 0;
    };

    /// What a bake measures beyond what it stores.
    struct BakeOptions
    {
        /// Also work out the unbounded catchment ratio of each pit (`BakeStats::mKappasUnbounded`,
        /// `mClippedUnbounded`), which costs a flow accumulation over the whole window on every cell. For reports only:
        /// off, those stay empty.
        bool mStatistics = false;
    };

    struct BakeResult
    {
        Error mError;
        CellMap mCell;
        BakeStats mStats;
    };

    /// Bakes cell (cellX, cellY). `settings` must be valid. The input digest in the result covers every cell of the
    /// window. A vertex height of the cell itself that is not finite or beyond +-1e7 is an error with the field
    /// "heights" and the place (cell and vertex); the same in a neighbour of the window makes that neighbour count as
    /// a cell with no land (a sink), so one bad cell stops nothing else.
    BakeResult bakeCell(LandHeightSource& source, std::int32_t cellX, std::int32_t cellY, const BakeSettings& settings,
        const BakeOptions& options = {});

    /// The digest of the baker's settings and version.
    Digest parameterDigest(const BakeSettings& settings);

    /// The digest of the land a cell's bake depends on (its window): cheap, does not bake. The same value bakeCell
    /// writes into the cell.
    Digest inputDigest(LandHeightSource& source, std::int32_t cellX, std::int32_t cellY, const BakeSettings& settings);

    /// Quantisation base of a set of heights: the lowest height rounded down to a whole unit.
    float heightBaseOf(float lowest);
}

#endif
