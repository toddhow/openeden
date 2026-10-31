#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_RAYCAST_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_RAYCAST_H

#include "flags.hpp"

#include <components/misc/vsgmath.hpp>

#include <optional>

class dtNavMeshQuery;

namespace DetourNavigator
{
    struct DetourSettings;

    std::optional<vsg::vec3> raycast(const dtNavMeshQuery& navMeshQuery, const vsg::vec3& halfExtents,
        const vsg::vec3& start, const vsg::vec3& end, const Flags includeFlags);
}

#endif
