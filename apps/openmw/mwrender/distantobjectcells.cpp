#include "distantobjectcells.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <vsg/maths/transform.h>

#include <components/esm/defs.hpp>

namespace MWRender
{
    bool ActiveCellGrid::contains(const std::pair<int, int>& cell) const
    {
        return std::abs(cell.first - centre.first) <= halfGrid && std::abs(cell.second - centre.second) <= halfGrid;
    }

    std::vector<std::pair<int, int>> distantObjectCells(
        const vsg::vec3& cameraPosition, float cellSize, float viewDistance, const ActiveCellGrid& active)
    {
        std::vector<std::pair<int, int>> cells;
        const int minX = static_cast<int>(std::floor((cameraPosition.x - viewDistance) / cellSize));
        const int maxX = static_cast<int>(std::floor((cameraPosition.x + viewDistance) / cellSize));
        const int minY = static_cast<int>(std::floor((cameraPosition.y - viewDistance) / cellSize));
        const int maxY = static_cast<int>(std::floor((cameraPosition.y + viewDistance) / cellSize));
        for (int x = minX; x <= maxX; ++x)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                const std::pair<int, int> cell(x, y);
                if (active.contains(cell))
                    continue;
                const float dx = std::max({ x * cellSize - cameraPosition.x, 0.f, cameraPosition.x - (x + 1) * cellSize });
                const float dy = std::max({ y * cellSize - cameraPosition.y, 0.f, cameraPosition.y - (y + 1) * cellSize });
                if (dx * dx + dy * dy <= viewDistance * viewDistance)
                    cells.push_back(cell);
            }
        }
        return cells;
    }

    bool isDistantObjectType(int type)
    {
        return type == ESM::REC_STAT || type == ESM::REC_ACTI || type == ESM::REC_DOOR;
    }

    float distantObjectMaxDistance(float radius, float scale, float minSize)
    {
        if (minSize <= 0.f)
            return std::numeric_limits<float>::max();
        return radius * scale / minSize;
    }

    vsg::dmat4 distantObjectMatrix(const vsg::vec3& position, const vsg::vec3& rotation, float scale)
    {
        return vsg::translate(vsg::dvec3(position)) * vsg::rotate(-static_cast<double>(rotation.x), 1.0, 0.0, 0.0)
            * vsg::rotate(-static_cast<double>(rotation.y), 0.0, 1.0, 0.0)
            * vsg::rotate(-static_cast<double>(rotation.z), 0.0, 0.0, 1.0) * vsg::scale(static_cast<double>(scale));
    }
}
