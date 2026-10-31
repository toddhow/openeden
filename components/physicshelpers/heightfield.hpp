#ifndef OPENMW_COMPONENTS_JOLTHELPERS_HEIGHTFIELD_H
#define OPENMW_COMPONENTS_JOLTHELPERS_HEIGHTFIELD_H

#include <vsg/maths/vec3.h>

namespace PhysicsSystemHelpers
{
    inline vsg::vec3 getHeightfieldShift(int x, int y, int size, float minHeight, float maxHeight)
    {
        const float cellSize = static_cast<float>(size);
        return vsg::vec3((static_cast<float>(x) + 0.5f) * cellSize, (static_cast<float>(y) + 0.5f) * cellSize,
            (maxHeight + minHeight) * 0.5f);
    }
}

#endif
