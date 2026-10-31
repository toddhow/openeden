#ifndef OPENMW_COMPONENTS_ESM_VECTOR3_H
#define OPENMW_COMPONENTS_ESM_VECTOR3_H

#include <vsg/maths/vec3.h>

namespace ESM
{
    // format 0, savegames only
    struct Vector3
    {
        float mValues[3];

        Vector3() = default;

        Vector3(const vsg::vec3& v)
            : mValues{ v.x, v.y, v.z }
        {
        }

        operator vsg::vec3() const { return vsg::vec3(mValues[0], mValues[1], mValues[2]); }
    };
}

#endif
