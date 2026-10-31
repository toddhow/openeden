#include "occlusionbox.hpp"

#include <components/misc/constants.hpp>

namespace MWRender
{
    OcclusionBox makeOcclusionBox(const vsg::vec3& cameraPosition, const vsg::vec3& range)
    {
        // The margin past the wrap range (PrecipitationOccluder::updateRange).
        constexpr float margin = 50.f;
        const float reach = range.z + static_cast<float>(Constants::CellSizeInUnits);

        OcclusionBox box;
        box.centre = vsg::vec2(cameraPosition.x, cameraPosition.y);
        box.size = vsg::vec2(range.x + margin, range.y + margin);
        box.zMax = cameraPosition.z + reach;
        box.zRange = reach * 2.f;
        return box;
    }

    vsg::vec3 occlusionCoord(const OcclusionBox& box, const vsg::vec3& worldPosition)
    {
        return vsg::vec3((worldPosition.x - box.centre.x) / box.size.x + 0.5f,
            (worldPosition.y - box.centre.y) / box.size.y + 0.5f, 1.f - (box.zMax - worldPosition.z) / box.zRange);
    }
}
