#include "neutralphysicswater.hpp"

#include "cell.hpp"
#include "cellstore.hpp"

namespace MWWorld
{
    std::optional<float> neutralPhysicsWaterLevel(const CellStore& cell)
    {
        if (!cell.isNeutral() || !cell.isExterior() || !cell.getCell()->hasWater())
            return std::nullopt;
        return cell.getWaterLevel();
    }
}
