#ifndef OPENEDEN_COMPONENTS_TERRAIN_BATCHINDEX_H
#define OPENEDEN_COMPONENTS_TERRAIN_BATCHINDEX_H

namespace Terrain
{
    /// The index of the batch of `batchSize` cells (> 0) that holds `cell`, along one axis: rounded down, so a negative
    /// cell lands in the batch to its west or south (task 0073). Integer division alone rounds toward zero, which put
    /// the local map's terrain of cell -9 in the batch of cells -8 to -7 once batches held more than one cell.
    inline int batchIndex(int cell, int batchSize)
    {
        const int quotient = cell / batchSize;
        return (cell % batchSize != 0 && cell < 0) ? quotient - 1 : quotient;
    }
}

#endif
