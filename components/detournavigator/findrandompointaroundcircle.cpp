#include "findrandompointaroundcircle.hpp"

#include <components/misc/vsgmath.hpp>

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

namespace DetourNavigator
{
    std::optional<vsg::vec3> findRandomPointAroundCircle(const dtNavMeshQuery& navMeshQuery,
        const vsg::vec3& halfExtents, const vsg::vec3& start, const float maxRadius, const Flags includeFlags,
        float (*prng)())
    {
        dtQueryFilter queryFilter;
        queryFilter.setIncludeFlags(includeFlags);

        dtPolyRef startRef = 0;
        const dtStatus status
            = navMeshQuery.findNearestPoly(start.data(), halfExtents.data(), &queryFilter, &startRef, nullptr);
        if (dtStatusFailed(status))
            return std::nullopt;

        dtPolyRef resultRef = 0;
        vsg::vec3 resultPosition;

        navMeshQuery.findRandomPointAroundCircle(
            startRef, start.data(), maxRadius, &queryFilter, prng, &resultRef, resultPosition.data());

        if (resultRef == 0)
            return std::optional<vsg::vec3>();

        return std::optional<vsg::vec3>(resultPosition);
    }
}
