#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_OFFMESHCONNECTION_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_OFFMESHCONNECTION_H

#include "areatype.hpp"

#include <components/misc/vsgmath.hpp>

#include <tuple>

namespace DetourNavigator
{
    struct OffMeshConnection
    {
        vsg::vec3 mStart;
        vsg::vec3 mEnd;
        AreaType mAreaType;
    };

    inline bool operator<(const OffMeshConnection& lhs, const OffMeshConnection& rhs)
    {
        return std::tie(lhs.mStart, lhs.mEnd, lhs.mAreaType) < std::tie(rhs.mStart, rhs.mEnd, rhs.mAreaType);
    }
}

#endif
