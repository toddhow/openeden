#ifndef OPENMW_MWGUI_LOCALMAPCELL_H
#define OPENMW_MWGUI_LOCALMAPCELL_H

#include <cmath>

namespace MWGui
{
    /// A world position in an exterior grid whose cells are `cellSize` units wide: the cell it is in and where in the
    /// cell it lies (0 to 1 from the west and south edges). Task 0201: the local map used the Morrowind cell size
    /// (8192) for every worldspace, which put its markers in the wrong place in a Skyrim one (4096).
    struct LocalMapCell
    {
        int mX = 0;
        int mY = 0;
        float mNX = 0.f;
        float mNY = 0.f;
    };

    inline LocalMapCell localMapCellOf(float worldX, float worldY, float cellSize)
    {
        if (!(cellSize > 0.f))
            cellSize = 8192.f; // Constants::CellSizeInUnits: a bad size means the Morrowind one
        LocalMapCell cell;
        cell.mX = static_cast<int>(std::floor(worldX / cellSize));
        cell.mY = static_cast<int>(std::floor(worldY / cellSize));
        cell.mNX = (worldX - cellSize * static_cast<float>(cell.mX)) / cellSize;
        cell.mNY = (worldY - cellSize * static_cast<float>(cell.mY)) / cellSize;
        return cell;
    }
}

#endif
