#include "pathfinding.hpp"

#include <iterator>
#include <limits>

#include <components/misc/vsgmath.hpp>

#include <vsg/io/stream.h>

#include <components/debug/debuglog.hpp>
#include <components/detournavigator/debug.hpp>
#include <components/detournavigator/navigatorutils.hpp>
#include <components/misc/coordinateconverter.hpp>
#include <components/misc/math.hpp>
#include <components/misc/pathgridutils.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwphysics/raycasting.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"

#include "actorutil.hpp"
#include "pathgrid.hpp"

namespace
{
    // Chooses a reachable end pathgrid point.  start is assumed reachable.
    std::pair<size_t, bool> getClosestReachablePoint(
        const ESM::Pathgrid* grid, const MWMechanics::PathgridGraph* graph, const vsg::vec3& pos, size_t start)
    {
        assert(grid && !grid->mPoints.empty());

        float closestDistanceBetween = std::numeric_limits<float>::max();
        float closestDistanceReachable = std::numeric_limits<float>::max();
        size_t closestIndex = 0;
        size_t closestReachableIndex = 0;
        // TODO: if this full scan causes performance problems mapping pathgrid
        //       points to a quadtree may help
        for (size_t counter = 0; counter < grid->mPoints.size(); counter++)
        {
            float potentialDistBetween = Misc::distanceSquared(grid->mPoints[counter], pos);
            if (potentialDistBetween < closestDistanceReachable)
            {
                // found a closer one
                if (graph->isPointConnected(start, counter))
                {
                    closestDistanceReachable = potentialDistBetween;
                    closestReachableIndex = counter;
                }
                if (potentialDistBetween < closestDistanceBetween)
                {
                    closestDistanceBetween = potentialDistBetween;
                    closestIndex = counter;
                }
            }
        }

        // post-condition: start and endpoint must be connected
        assert(graph->isPointConnected(start, closestReachableIndex));

        // AiWander has logic that depends on whether a path was created, deleting
        // allowed nodes if not.  Hence a path needs to be created even if the start
        // and the end points are the same.

        return { closestReachableIndex, closestReachableIndex == closestIndex };
    }

    float sqrDistance(const vsg::vec2& lhs, const vsg::vec2& rhs)
    {
        return vsg::length2(lhs - rhs);
    }

    float sqrDistanceIgnoreZ(const vsg::vec3& lhs, const vsg::vec3& rhs)
    {
        return sqrDistance(vsg::vec2(lhs.x, lhs.y), vsg::vec2(rhs.x, rhs.y));
    }

    float getHeight(const MWWorld::ConstPtr& actor)
    {
        const auto world = MWBase::Environment::get().getWorld();
        const auto halfExtents = world->getHalfExtents(actor);
        return 2.f * halfExtents.z;
    }

    // Returns true if turn in `p2` is less than 10 degrees and all the 3 points are almost on one line.
    bool isAlmostStraight(const vsg::vec3& p1, const vsg::vec3& p2, const vsg::vec3& p3, float pointTolerance)
    {
        vsg::vec3 v1 = p1 - p2;
        vsg::vec3 v3 = p3 - p2;
        v1.z = v3.z = 0;
        float dotProduct = v1.x * v3.x + v1.y * v3.y;
        float crossProduct = v1.x * v3.y - v1.y * v3.x;

        // Check that the angle between v1 and v3 is less or equal than 5 degrees.
        static const float cos175 = std::cos(Misc::VsgMath::PIf * (175.f / 180));
        bool checkAngle = dotProduct <= cos175 * vsg::length(v1) * vsg::length(v3);

        // Check that distance from p2 to the line (p1, p3) is less or equal than `pointTolerance`.
        bool checkDist = std::abs(crossProduct) <= pointTolerance * vsg::length(p3 - p1);

        return checkAngle && checkDist;
    }

    struct IsValidShortcut
    {
        const DetourNavigator::Navigator* mNavigator;
        const DetourNavigator::AgentBounds mAgentBounds;
        const DetourNavigator::Flags mFlags;

        bool operator()(const vsg::vec3& start, const vsg::vec3& end) const
        {
            const std::optional<vsg::vec3> position
                = DetourNavigator::raycast(*mNavigator, mAgentBounds, start, end, mFlags);
            return position.has_value()
                && std::abs(vsg::length2(position.value() - start) - vsg::length2(end - start)) <= 1;
        }
    };
}

namespace MWMechanics
{
    float getPathDistance(const MWWorld::Ptr& actor, const vsg::vec3& lhs, const vsg::vec3& rhs)
    {
        if (std::abs(lhs.z - rhs.z) > getHeight(actor) || canActorMoveByZAxis(actor))
            return distance(lhs, rhs);
        return distanceIgnoreZ(lhs, rhs);
    }

    bool checkWayIsClear(const vsg::vec3& from, const vsg::vec3& to, float offsetXY)
    {
        vsg::vec3 dir = to - from;
        dir.z = 0;
        Misc::VsgMath::normalizeInPlace(dir);
        float verticalOffset = 200; // instead of '200' here we want the height of the actor
        const vsg::vec3 adjustedFrom = from + dir * offsetXY + vsg::vec3(0, 0, 1) * verticalOffset;

        // cast up-down ray and find height of hit in world space
        float h = adjustedFrom.z
            - MWBase::Environment::get().getWorld()->getDistToNearestRayHit(
                adjustedFrom, -vsg::vec3(0, 0, 1), verticalOffset + PATHFIND_Z_REACH + 1);

        return (std::abs(from.z - h) <= PATHFIND_Z_REACH);
    }

    /*
     * NOTE: This method may fail to find a path.  The caller must check the
     * result before using it.  If there is no path the AI routies need to
     * implement some other heuristics to reach the target.
     *
     * NOTE: It may be desirable to simply go directly to the endPoint if for
     *       example there are no pathgrids in this cell.
     *
     * NOTE: startPoint & endPoint are in world coordinates
     *
     * Updates mPath using aStarSearch() or ray test (if shortcut allowed).
     * mPath consists of pathgrid points, except the last element which is
     * endPoint.  This may be useful where the endPoint is not on a pathgrid
     * point (e.g. combat).  However, if the caller has already chosen a
     * pathgrid point (e.g. wander) then it may be worth while to call
     * pop_back() to remove the redundant entry.
     *
     * NOTE: coordinates must be converted prior to calling getClosestPoint()
     *
     *    |
     *    |       cell
     *    |     +-----------+
     *    |     |           |
     *    |     |           |
     *    |     |      @    |
     *    |  i  |   j       |
     *    |<--->|<---->|    |
     *    |     +-----------+
     *    |   k
     *    |<---------->|         world
     *    +-----------------------------
     *
     *    i = x value of cell itself (multiply by ESM::Land::REAL_SIZE to convert)
     *    j = @.x in local coordinates (i.e. within the cell)
     *    k = @.x in world coordinates
     */
    void PathFinder::buildPathByPathgridImpl(const vsg::vec3& startPoint, const vsg::vec3& endPoint,
        const PathgridGraph& pathgridGraph, std::back_insert_iterator<std::deque<vsg::vec3>> out)
    {
        const auto pathgrid = pathgridGraph.getPathgrid();

        // Refer to AiWander research topic on openmw forums for some background.
        // Maybe there is no pathgrid for this cell.  Just go to destination and let
        // physics take care of any blockages.
        if (!pathgrid || pathgrid->mPoints.empty())
            return;

        // NOTE: getClosestPoint expects local coordinates
        const Misc::CoordinateConverter converter = Misc::makeCoordinateConverter(*mCell->getCell());

        // NOTE: It is possible that getClosestPoint returns a pathgrind point index
        //       that is unreachable in some situations. e.g. actor is standing
        //       outside an area enclosed by walls, but there is a pathgrid
        //       point right behind the wall that is closer than any pathgrid
        //       point outside the wall
        vsg::vec3 startPointInLocalCoords(converter.toLocalVec3(startPoint));
        const size_t startNode = Misc::getClosestPoint(*pathgrid, startPointInLocalCoords);

        vsg::vec3 endPointInLocalCoords(converter.toLocalVec3(endPoint));
        std::pair<size_t, bool> endNode
            = getClosestReachablePoint(pathgrid, &pathgridGraph, endPointInLocalCoords, startNode);

        // if it's shorter for actor to travel from start to end, than to travel from either
        // start or end to nearest pathgrid point, just travel from start to end.
        float startToEndLength2 = vsg::length2(endPointInLocalCoords - startPointInLocalCoords);
        float endTolastNodeLength2 = Misc::distanceSquared(pathgrid->mPoints[endNode.first], endPointInLocalCoords);
        float startTo1stNodeLength2 = Misc::distanceSquared(pathgrid->mPoints[startNode], startPointInLocalCoords);
        if ((startToEndLength2 < startTo1stNodeLength2) || (startToEndLength2 < endTolastNodeLength2))
        {
            *out++ = endPoint;
            return;
        }

        // AiWander has logic that depends on whether a path was created,
        // deleting allowed nodes if not.  Hence a path needs to be created
        // even if the start and the end points are the same.
        // NOTE: aStarSearch will return an empty path if the start and end
        //       nodes are the same
        if (startNode == endNode.first)
        {
            ESM::Pathgrid::Point temp(pathgrid->mPoints[startNode]);
            converter.toWorld(temp);
            *out++ = Misc::Convert::makeOsgVec3f(temp);
        }
        else
        {
            auto path = pathgridGraph.aStarSearch(startNode, endNode.first);

            // If nearest path node is in opposite direction from second, remove it from path.
            // Especially useful for wandering actors, if the nearest node is blocked for some reason.
            if (path.size() > 1)
            {
                ESM::Pathgrid::Point secondNode = *(++path.begin());
                vsg::vec3 firstNodeVec3f = Misc::Convert::makeOsgVec3f(pathgrid->mPoints[startNode]);
                vsg::vec3 secondNodeVec3f = Misc::Convert::makeOsgVec3f(secondNode);
                vsg::vec3 toSecondNodeVec3f = secondNodeVec3f - firstNodeVec3f;
                vsg::vec3 toStartPointVec3f = startPointInLocalCoords - firstNodeVec3f;
                if (vsg::dot(toSecondNodeVec3f, toStartPointVec3f) > 0)
                {
                    ESM::Pathgrid::Point temp(secondNode);
                    converter.toWorld(temp);
                    // Add Z offset since path node can overlap with other objects.
                    // Also ignore doors in raytesting.
                    const int mask = MWPhysics::Layers::WORLD;
                    const vsg::vec3 zOffset(0.f, 0.f, 16.f);
                    const vsg::vec3 from = startPoint + zOffset;
                    const vsg::vec3 to = vsg::vec3(Misc::Convert::makeOsgVec3f(temp)) + zOffset;
                    bool isPathClear
                        = !MWBase::Environment::get().getWorld()->getRayCasting()->castRay(from, to, mask).mHit;
                    if (isPathClear)
                        path.pop_front();
                }
            }

            // convert supplied path to world coordinates
            std::transform(path.begin(), path.end(), out, [&](ESM::Pathgrid::Point& point) {
                converter.toWorld(point);
                return Misc::Convert::makeOsgVec3f(point);
            });
        }

        // If endNode found is NOT the closest PathGrid point to the endPoint,
        // assume endPoint is not reachable from endNode. In which case,
        // path ends at endNode.
        //
        // So only add the destination (which may be different to the closest
        // pathgrid point) when endNode was the closest point to endPoint.
        //
        // This logic can fail in the opposite situate, e.g. endPoint may
        // have been reachable but happened to be very close to an
        // unreachable pathgrid point.
        //
        // The AI routines will have to deal with such situations.
        if (endNode.second)
            *out++ = endPoint;
    }

    float PathFinder::getZAngleToNext(float x, float y) const
    {
        // This should never happen (programmers should have an if statement checking
        // isPathConstructed that prevents this call if otherwise).
        if (mPath.empty())
            return 0.;

        const auto& nextPoint = mPath.front();
        const float directionX = nextPoint.x - x;
        const float directionY = nextPoint.y - y;

        return std::atan2(directionX, directionY);
    }

    float PathFinder::getXAngleToNext(float x, float y, float z) const
    {
        // This should never happen (programmers should have an if statement checking
        // isPathConstructed that prevents this call if otherwise).
        if (mPath.empty())
            return 0.;

        const vsg::vec3 dir = mPath.front() - vsg::vec3(x, y, z);

        return getXAngleToDir(dir);
    }

    void PathFinder::update(const vsg::vec3& position, float pointTolerance, float destinationTolerance,
        UpdateFlags updateFlags, const DetourNavigator::AgentBounds& agentBounds, DetourNavigator::Flags pathFlags)
    {
        if (mPath.empty())
            return;

        while (mPath.size() > 1 && sqrDistanceIgnoreZ(mPath.front(), position) < pointTolerance * pointTolerance)
            mPath.pop_front();

        const IsValidShortcut isValidShortcut{ MWBase::Environment::get().getWorld()->getNavigator(), agentBounds,
            pathFlags };

        if ((updateFlags & UpdateFlag_ShortenIfAlmostStraight) != 0)
        {
            while (mPath.size() > 2 && isAlmostStraight(mPath[0], mPath[1], mPath[2], pointTolerance)
                && isValidShortcut(mPath[0], mPath[2]))
                mPath.erase(mPath.begin() + 1);
            if (mPath.size() > 1 && isAlmostStraight(position, mPath[0], mPath[1], pointTolerance)
                && isValidShortcut(position, mPath[1]))
                mPath.pop_front();
        }

        if ((updateFlags & UpdateFlag_RemoveLoops) != 0 && mPath.size() > 1)
        {
            std::size_t begin = 0;
            for (std::size_t i = 1; i < mPath.size(); ++i)
            {
                const float sqrDistance = vsg::length2(Misc::getVectorToLine(position, mPath[i - 1], mPath[i]));
                if (sqrDistance < pointTolerance * pointTolerance && isValidShortcut(position, mPath[i]))
                    begin = i;
            }
            for (std::size_t i = 0; i < begin; ++i)
                mPath.pop_front();
        }

        if (mPath.size() == 1)
        {
            float distSqr;
            if ((updateFlags & UpdateFlag_CanMoveByZ) != 0)
                distSqr = vsg::length2(mPath.front() - position);
            else
                distSqr = sqrDistanceIgnoreZ(mPath.front(), position);
            if (distSqr < destinationTolerance * destinationTolerance)
                mPath.pop_front();
        }
    }

    void PathFinder::buildStraightPath(const vsg::vec3& endPoint)
    {
        mPath.clear();
        mPath.push_back(endPoint);
        mConstructed = true;
    }

    void PathFinder::buildPathByNavMesh(const MWWorld::ConstPtr& actor, const vsg::vec3& startPoint,
        const vsg::vec3& endPoint, const DetourNavigator::AgentBounds& agentBounds, const DetourNavigator::Flags flags,
        const DetourNavigator::AreaCosts& areaCosts, float endTolerance, PathType pathType,
        std::span<const vsg::vec3> checkpoints)
    {
        mPath.clear();

        // If it's not possible to build path over navmesh due to disabled navmesh generation fallback to straight path
        DetourNavigator::Status status = buildPathByNavigatorImpl(actor, startPoint, endPoint, agentBounds, flags,
            areaCosts, endTolerance, pathType, checkpoints, std::back_inserter(mPath));

        if (status != DetourNavigator::Status::Success)
            mPath.clear();

        if (status == DetourNavigator::Status::NavMeshNotFound)
            mPath.push_back(endPoint);

        mConstructed = !mPath.empty();
    }

    void PathFinder::buildPath(const MWWorld::ConstPtr& actor, const vsg::vec3& startPoint, const vsg::vec3& endPoint,
        const PathgridGraph& pathgridGraph, const DetourNavigator::AgentBounds& agentBounds,
        const DetourNavigator::Flags flags, const DetourNavigator::AreaCosts& areaCosts, float endTolerance,
        PathType pathType, std::span<const vsg::vec3> checkpoints)
    {
        mPath.clear();
        mCell = actor.getCell();

        DetourNavigator::Status status = DetourNavigator::Status::NavMeshNotFound;

        if (!actor.getClass().isPureWaterCreature(actor) && !actor.getClass().isPureFlyingCreature(actor))
        {
            status = buildPathByNavigatorImpl(actor, startPoint, endPoint, agentBounds, flags, areaCosts, endTolerance,
                pathType, checkpoints, std::back_inserter(mPath));
            if (status != DetourNavigator::Status::Success)
                mPath.clear();
        }

        if (status != DetourNavigator::Status::NavMeshNotFound && mPath.empty()
            && (flags & DetourNavigator::Flag_usePathgrid) == 0)
        {
            status = buildPathByNavigatorImpl(actor, startPoint, endPoint, agentBounds,
                flags | DetourNavigator::Flag_usePathgrid, areaCosts, endTolerance, pathType, checkpoints,
                std::back_inserter(mPath));
            if (status != DetourNavigator::Status::Success)
                mPath.clear();
        }

        if (mPath.empty())
            buildPathByPathgridImpl(startPoint, endPoint, pathgridGraph, std::back_inserter(mPath));

        if (status == DetourNavigator::Status::NavMeshNotFound && mPath.empty())
            mPath.push_back(endPoint);

        mConstructed = !mPath.empty();
    }

    DetourNavigator::Status PathFinder::buildPathByNavigatorImpl(const MWWorld::ConstPtr& actor,
        const vsg::vec3& startPoint, const vsg::vec3& endPoint, const DetourNavigator::AgentBounds& agentBounds,
        const DetourNavigator::Flags flags, const DetourNavigator::AreaCosts& areaCosts, float endTolerance,
        PathType pathType, std::span<const vsg::vec3> checkpoints, std::back_insert_iterator<std::deque<vsg::vec3>> out)
    {
        const MWBase::World& world = *MWBase::Environment::get().getWorld();
        const DetourNavigator::Navigator& navigator = *world.getNavigator();
        const DetourNavigator::Status status = DetourNavigator::findPath(navigator, agentBounds, startPoint, endPoint,
            flags, areaCosts, endTolerance, std::vector<vsg::vec3>(checkpoints.begin(), checkpoints.end()), out);

        if (pathType == PathType::Partial && status == DetourNavigator::Status::PartialPath)
            return DetourNavigator::Status::Success;

        if (status != DetourNavigator::Status::Success)
        {
            Log(Debug::Debug) << "Build path by navigator error: \"" << DetourNavigator::getMessage(status)
                              << "\" for \"" << actor.getClass().getName(actor) << "\" (" << actor.getBase()
                              << ") from " << startPoint << " to " << endPoint << " with flags ("
                              << DetourNavigator::WriteFlags{ flags } << ")";
        }

        return status;
    }

    void PathFinder::buildLimitedPath(const MWWorld::ConstPtr& actor, const vsg::vec3& startPoint,
        const vsg::vec3& endPoint, const PathgridGraph& pathgridGraph, const DetourNavigator::AgentBounds& agentBounds,
        const DetourNavigator::Flags flags, const DetourNavigator::AreaCosts& areaCosts, float endTolerance,
        PathType pathType)
    {
        const auto navigator = MWBase::Environment::get().getWorld()->getNavigator();
        const auto maxDistance
            = std::min(navigator->getMaxNavmeshAreaRealRadius(), static_cast<float>(Constants::CellSizeInUnits));
        const auto startToEnd = endPoint - startPoint;
        const auto distance = vsg::length(startToEnd);
        if (distance <= maxDistance)
            return buildPath(
                actor, startPoint, endPoint, pathgridGraph, agentBounds, flags, areaCosts, endTolerance, pathType);
        const auto end = startPoint + Misc::VsgMath::divide(startToEnd * maxDistance, distance);
        buildPath(actor, startPoint, end, pathgridGraph, agentBounds, flags, areaCosts, endTolerance, pathType);
    }
}
