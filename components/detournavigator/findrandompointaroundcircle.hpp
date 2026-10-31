#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_FINDRANDOMPOINTAROUNDCIRCLE_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_FINDRANDOMPOINTAROUNDCIRCLE_H

#include "flags.hpp"

#include <components/misc/vsgmath.hpp>

#include <optional>

class dtNavMeshQuery;

namespace DetourNavigator
{
    struct DetourSettings;

    std::optional<vsg::vec3> findRandomPointAroundCircle(const dtNavMeshQuery& navMeshQuery,
        const vsg::vec3& halfExtents, const vsg::vec3& start, const float maxRadius, const Flags includeFlags,
        float (*prng)());
}

#endif
