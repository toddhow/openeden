#ifndef OPENMW_COMPONENTS_MISC_MATH_H
#define OPENMW_COMPONENTS_MISC_MATH_H

#include <components/misc/vsgmath.hpp>

namespace Misc
{
    inline vsg::vec3 getVectorToLine(const vsg::vec3& position, const vsg::vec3& a, const vsg::vec3& b)
    {
        vsg::vec3 direction = b - a;
        VsgMath::normalizeInPlace(direction);
        return vsg::cross(position - a, direction);
    }
}

#endif
