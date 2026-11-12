#include "mapdepthrange.hpp"

#include <algorithm>
#include <cmath>

#include <components/esmbridge/util.hpp>

namespace MWRender
{
    float mapWorldSizeFor(const ESM::RefId& worldspace)
    {
        return static_cast<float>(ESM::getCellSize(worldspace));
    }

    std::optional<MapDepthRange> mapDepthRange(
        const vsg::dbox& objects, const std::optional<vsg::vec2>& terrain, const std::optional<float>& water)
    {
        std::optional<MapDepthRange> range;
        const auto include = [&range](double value) {
            if (!std::isfinite(value))
                return;
            const float z = static_cast<float>(value);
            if (!std::isfinite(z))
                return;
            if (!range)
                range = MapDepthRange{ z, z };
            else
            {
                range->mMin = std::min(range->mMin, z);
                range->mMax = std::max(range->mMax, z);
            }
        };
        if (objects.valid())
        {
            include(objects.min.z);
            include(objects.max.z);
        }
        if (terrain)
        {
            include(terrain->x);
            include(terrain->y);
        }
        if (water)
            include(*water);
        return range;
    }
}
