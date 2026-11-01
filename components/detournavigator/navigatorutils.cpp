#include "navigatorutils.hpp"
#include "debug.hpp"
#include "findrandompointaroundcircle.hpp"
#include "navigator.hpp"
#include "raycast.hpp"

#include <components/debug/debuglog.hpp>
#include <components/misc/vsgmath.hpp>

namespace DetourNavigator
{
    std::optional<vsg::vec3> findRandomPointAroundCircle(const Navigator& navigator, const AgentBounds& agentBounds,
        const vsg::vec3& start, const float maxRadius, const Flags includeFlags, float (*prng)())
    {
        const auto navMesh = navigator.getNavMesh(agentBounds);
        if (!navMesh)
            return std::nullopt;
        const Settings& settings = navigator.getSettings();
        const auto locked = navMesh->lock();
        const auto result = DetourNavigator::findRandomPointAroundCircle(locked->getQuery(),
            toNavMeshCoordinates(settings.mRecast, agentBounds.mHalfExtents),
            toNavMeshCoordinates(settings.mRecast, start), toNavMeshCoordinates(settings.mRecast, maxRadius),
            includeFlags, prng);
        if (!result)
            return std::nullopt;
        return std::optional<vsg::vec3>(fromNavMeshCoordinates(settings.mRecast, *result));
    }

    std::optional<vsg::vec3> raycast(const Navigator& navigator, const AgentBounds& agentBounds, const vsg::vec3& start,
        const vsg::vec3& end, const Flags includeFlags)
    {
        const auto navMesh = navigator.getNavMesh(agentBounds);
        if (navMesh == nullptr)
            return std::nullopt;
        const Settings& settings = navigator.getSettings();
        const auto locked = navMesh->lock();
        const auto result = DetourNavigator::raycast(locked->getQuery(),
            toNavMeshCoordinates(settings.mRecast, agentBounds.mHalfExtents),
            toNavMeshCoordinates(settings.mRecast, start), toNavMeshCoordinates(settings.mRecast, end), includeFlags);
        if (!result)
            return std::nullopt;
        return fromNavMeshCoordinates(settings.mRecast, *result);
    }

    std::optional<vsg::vec3> findNearestNavMeshPosition(const Navigator& navigator, const AgentBounds& agentBounds,
        const vsg::vec3& position, const vsg::vec3& searchAreaHalfExtents, const Flags includeFlags)
    {
        const auto navMesh = navigator.getNavMesh(agentBounds);
        if (navMesh == nullptr)
            return std::nullopt;

        const auto& settings = navigator.getSettings();
        const vsg::vec3 navMeshPosition = toNavMeshCoordinates(settings.mRecast, position);
        const auto lockedNavMesh = navMesh->lockConst();

        dtNavMeshQuery navMeshQuery;
        if (const dtStatus status
            = navMeshQuery.init(&lockedNavMesh->getImpl(), settings.mDetour.mMaxNavMeshQueryNodes);
            dtStatusFailed(status))
        {
            Log(Debug::Error) << "Failed to init dtNavMeshQuery for findNearestNavMeshPosition: "
                              << WriteDtStatus{ status };
            return std::nullopt;
        }

        dtQueryFilter queryFilter;
        queryFilter.setIncludeFlags(includeFlags);

        vsg::vec3 nearestNavMeshPos;
        const vsg::vec3 endPolyHalfExtents = toNavMeshCoordinates(settings.mRecast, searchAreaHalfExtents);
        dtPolyRef polyRef;
        if (const dtStatus status = navMeshQuery.findNearestPoly(
                navMeshPosition.data(), endPolyHalfExtents.data(), &queryFilter, &polyRef, nearestNavMeshPos.data());
            dtStatusFailed(status) || polyRef == 0)
        {
            return std::nullopt;
        }

        return fromNavMeshCoordinates(settings.mRecast, nearestNavMeshPos);
    }
}
