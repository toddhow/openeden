#include "neutralphysicswater.hpp"

#include <cmath>

#include <components/esmbridge/util.hpp>

#include "cell.hpp"
#include "cellstore.hpp"

namespace MWWorld
{
    std::optional<float> neutralPhysicsWaterLevel(const CellStore& cell)
    {
        if (!cell.isNeutral() || !cell.getCell()->hasWater())
            return std::nullopt;
        return cell.getWaterLevel();
    }

    std::optional<float> neutralWaterLevelAt(const CellStore& cell, float x, float y)
    {
        if (!cell.isNeutral() || !cell.getCell()->hasWater() || !std::isfinite(x) || !std::isfinite(y))
            return std::nullopt;
        if (!cell.isExterior())
            return cell.getWaterLevel();
        // A square's water is its own: an actor that stands over the border belongs to the cell it was placed in.
        const float size = static_cast<float>(ESM::getCellSize(cell.getCell()->getWorldSpace()));
        const int gridX = static_cast<int>(std::floor(x / size));
        const int gridY = static_cast<int>(std::floor(y / size));
        if (gridX != cell.getCell()->getGridX() || gridY != cell.getCell()->getGridY())
            return std::nullopt;
        return cell.getWaterLevel();
    }
}
