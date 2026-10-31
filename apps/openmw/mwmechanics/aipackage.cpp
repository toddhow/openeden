#include "aipackage.hpp"

#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/navigator.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/misc/coordinateconverter.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/worldmodel.hpp"

#include "../mwphysics/raycasting.hpp"

#include "actorutil.hpp"
#include "creaturestats.hpp"
#include "movement.hpp"
#include "pathgrid.hpp"
#include "steering.hpp"

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/quat.h>

namespace
{
    float divOrMax(float dividend, float divisor)
    {
        return divisor == 0 ? std::numeric_limits<float>::max() * std::numeric_limits<float>::epsilon()
                            : dividend / divisor;
    }

    float getPointTolerance(float speed, float duration, const vsg::vec3& halfExtents)
    {
        const float actorTolerance = 2 * speed * duration + 1.2f * std::max(halfExtents.x, halfExtents.y);
        return std::max(MWMechanics::MIN_TOLERANCE, actorTolerance);
    }

    bool canOpenDoors(const MWWorld::Ptr& ptr)
    {
        return ptr.getClass().isBipedal(ptr) || ptr.getClass().hasInventoryStore(ptr);
    }
}

MWMechanics::AiPackage::AiPackage(AiPackageTypeId typeId, const Options& options)
    : mTypeId(typeId)
    , mOptions(options)
    , mReaction(MWBase::Environment::get().getWorld()->getPrng())
{
}

MWWorld::Ptr MWMechanics::AiPackage::getTarget() const
{
    if (mTargetActor.isSet())
        return MWBase::Environment::get().getWorldModel()->getPtr(mTargetActor);
    if (mTargetActorRefId.empty() || mTargetNotFound)
        return {};
    MWWorld::Ptr ptr = MWBase::Environment::get().getWorld()->searchPtr(mTargetActorRefId, false);
    if (ptr.isEmpty())
        mTargetNotFound = true;
    else
    {
        MWBase::Environment::get().getWorldModel()->registerPtr(ptr);
        mTargetActor = ptr.getCellRef().getRefNum();
    }
    return ptr;
}

bool MWMechanics::AiPackage::targetIs(const MWWorld::Ptr& ptr) const
{
    if (ptr.isEmpty())
        return getTarget() == ptr;
    if (mTargetActor.isSet())
        return ptr.getCellRef().getRefNum() == mTargetActor;
    if (ptr.getCellRef().getRefId() != mTargetActorRefId)
        return false;
    return getTarget() == ptr;
}

void MWMechanics::AiPackage::reset()
{
    // reset all members
    mReaction.reset();
    mIsShortcutting = false;
    mShortcutProhibited = false;
    mShortcutFailPos = vsg::vec3();
    mTargetNotFound = false;

    mPathFinder.clearPath();
    mObstacleCheck.clear();
}

bool MWMechanics::AiPackage::pathTo(const MWWorld::Ptr& actor, const vsg::vec3& dest, float duration,
    MWWorld::MovementDirectionFlags supportedMovementDirections, float destTolerance, float endTolerance,
    PathType pathType)
{
    const Misc::TimerStatus timerStatus = mReaction.update(duration);

    const vsg::vec3 position = actor.getRefData().getPosition().asVec3(); // position of the actor
    MWBase::World* world = MWBase::Environment::get().getWorld();
    const DetourNavigator::AgentBounds agentBounds = world->getPathfindingAgentBounds(actor);

    /// Stops the actor when it gets too close to a unloaded cell or when the actor is playing a scripted animation
    //... At current time, the first test is unnecessary. AI shuts down when actor is more than
    //... "actors processing range" setting value units from player, and exterior cells are 8192 units long and wide.
    //... But AI processing distance may increase in the future.
    if (isNearInactiveCell(position)
        || MWBase::Environment::get().getMechanicsManager()->checkScriptedAnimationPlaying(actor))
    {
        actor.getClass().getMovementSettings(actor).mPosition[0] = 0;
        actor.getClass().getMovementSettings(actor).mPosition[1] = 0;
        world->updateActorPath(actor, mPathFinder.getPath(), agentBounds, position, dest);
        return false;
    }

    mLastDestinationTolerance = destTolerance;

    const float distToTarget = distance(position, dest);
    const bool isDestReached = (distToTarget <= destTolerance);
    const bool actorCanMoveByZ = canActorMoveByZAxis(actor);

    if (!isDestReached && timerStatus == Misc::TimerStatus::Elapsed)
    {
        if (canOpenDoors(actor))
            openDoors(actor);

        const bool wasShortcutting = mIsShortcutting;
        bool destInLOS = false;

        // Prohibit shortcuts for AiWander, if the actor can not move in 3 dimensions.
        mIsShortcutting = actorCanMoveByZ
            && shortcutPath(position, dest, actor, &destInLOS, actorCanMoveByZ); // try to shortcut first

        if (!mIsShortcutting)
        {
            if (wasShortcutting || doesPathNeedRecalc(dest, actor)) // if need to rebuild path
            {
                const ESM::Pathgrid* pathgrid
                    = world->getStore().get<ESM::Pathgrid>().search(*actor.getCell()->getCell());
                const DetourNavigator::Flags navigatorFlags = getNavigatorFlags(actor);
                const DetourNavigator::AreaCosts areaCosts = getAreaCosts(actor, navigatorFlags);
                mPathFinder.buildLimitedPath(actor, position, dest, getPathGridGraph(pathgrid), agentBounds,
                    navigatorFlags, areaCosts, endTolerance, pathType);
                mRotateOnTheRunChecks = 3;

                // give priority to go directly on target if there is minimal opportunity
                if (destInLOS && mPathFinder.getPath().size() > 1)
                {
                    // get point just before dest
                    auto pPointBeforeDest = mPathFinder.getPath().rbegin() + 1;

                    // if start point is closer to the target then last point of path (excluding target itself) then go
                    // straight on the target
                    if (distance(position, dest) <= distance(dest, *pPointBeforeDest))
                    {
                        mPathFinder.clearPath();
                        mPathFinder.addPointToPath(dest);
                    }
                }
            }

            if (!mPathFinder.getPath().empty()) // Path has points in it
            {
                const vsg::vec3& lastPos = mPathFinder.getPath().back(); // Get the end of the proposed path

                if (distance(dest, lastPos) > 100) // End of the path is far from the destination
                    mPathFinder.addPointToPath(
                        dest); // Adds the final destination to the path, to try to get to where you want to go
            }
        }
    }

    const float pointTolerance
        = getPointTolerance(actor.getClass().getMaxSpeed(actor), duration, world->getHalfExtents(actor));

    const bool smoothMovement = Settings::game().mSmoothMovement;

    PathFinder::UpdateFlags updateFlags{};

    if (actorCanMoveByZ)
        updateFlags |= PathFinder::UpdateFlag_CanMoveByZ;
    if (timerStatus == Misc::TimerStatus::Elapsed && smoothMovement)
        updateFlags |= PathFinder::UpdateFlag_ShortenIfAlmostStraight;
    if (timerStatus == Misc::TimerStatus::Elapsed)
        updateFlags |= PathFinder::UpdateFlag_RemoveLoops;

    mPathFinder.update(position, pointTolerance, DEFAULT_TOLERANCE, updateFlags, agentBounds, getNavigatorFlags(actor));

    if (isDestReached || mPathFinder.checkPathCompleted()) // if path is finished
    {
        // turn to destination point
        zTurn(actor, duration, getZAngleToPoint(position, dest));
        smoothTurn(actor, duration, getXAngleToPoint(position, dest), 0);
        world->removeActorPath(actor);
        return true;
    }
    else if (mPathFinder.getPath().empty())
        return false;

    world->updateActorPath(actor, mPathFinder.getPath(), agentBounds, position, dest);

    if (mRotateOnTheRunChecks == 0
        || isReachableRotatingOnTheRun(
            actor, *mPathFinder.getPath().begin())) // to prevent circling around a path point
    {
        actor.getClass().getMovementSettings(actor).mPosition[1] = 1; // move to the target
        if (mRotateOnTheRunChecks > 0)
            mRotateOnTheRunChecks--;
    }

    // turn to next path point by X,Z axes
    float zAngleToNext = mPathFinder.getZAngleToNext(position.x, position.y);
    zTurn(actor, duration, zAngleToNext);
    smoothTurn(actor, duration, mPathFinder.getXAngleToNext(position.x, position.y, position.z), 0);

    const auto destination = getNextPathPoint(dest);
    mObstacleCheck.update(actor, destination, duration, supportedMovementDirections);

    if (smoothMovement)
    {
        const float smoothTurnReservedDist = 150;
        auto& movement = actor.getClass().getMovementSettings(actor);
        float distToNextSqr = vsg::length2(vsg::vec2(destination.x - position.x, destination.y - position.y));
        float diffAngle = zAngleToNext - actor.getRefData().getPosition().rot[2];
        if (std::cos(diffAngle) < -0.1)
            movement.mPosition[0] = movement.mPosition[1] = 0;
        else if (distToNextSqr > smoothTurnReservedDist * smoothTurnReservedDist)
        { // Go forward (and slowly turn towards the next path point)
            movement.mPosition[0] = 0;
            movement.mPosition[1] = 1;
        }
        else
        { // Next path point is near, so use diagonal movement to follow the path precisely.
            movement.mPosition[0] = std::sin(diffAngle);
            movement.mPosition[1] = std::max(std::cos(diffAngle), 0.f);
        }
    }

    // handle obstacles on the way
    evadeObstacles(actor);

    return false;
}

void MWMechanics::AiPackage::evadeObstacles(const MWWorld::Ptr& actor)
{
    // check if stuck due to obstacles
    if (!mObstacleCheck.isEvading())
        return;

    // first check if obstacle is a door
    float distance = MWBase::Environment::get().getWorld()->getMaxActivationDistance();
    const MWWorld::Ptr door = getNearbyDoor(actor, distance);
    if (!door.isEmpty() && canOpenDoors(actor))
    {
        openDoors(actor);
    }
    else
    {
        mObstacleCheck.takeEvasiveAction(actor.getClass().getMovementSettings(actor));
    }
}

namespace
{
    bool isDoorOnTheWay(const MWWorld::Ptr& actor, const MWWorld::Ptr& door, const vsg::vec3& nextPathPoint)
    {
        const auto world = MWBase::Environment::get().getWorld();
        const auto halfExtents = world->getHalfExtents(actor);
        const auto position = vsg::vec3(actor.getRefData().getPosition().asVec3()) + vsg::vec3(0, 0, halfExtents.z);
        const auto destination = nextPathPoint + vsg::vec3(0, 0, halfExtents.z);

        return world->hasCollisionWithDoor(door, position, destination);
    }
}

void MWMechanics::AiPackage::openDoors(const MWWorld::Ptr& actor)
{
    // note: AiWander currently does not open doors
    if (getTypeId() == AiPackageTypeId::Wander)
        return;

    if (mPathFinder.getPathSize() == 0)
        return;

    float distance = MWBase::Environment::get().getWorld()->getMaxActivationDistance();
    const MWWorld::Ptr door = getNearbyDoor(actor, distance);
    if (door == MWWorld::Ptr())
        return;

    if (!door.getCellRef().getTeleport() && door.getClass().getDoorState(door) == MWWorld::DoorState::Idle)
    {
        if (!isDoorOnTheWay(actor, door, mPathFinder.getPath().front()))
            return;

        if (door.getCellRef().getTrap().empty() && !door.getCellRef().isLocked())
        {
            MWBase::Environment::get().getLuaManager()->objectActivated(door, actor);
            return;
        }

        const ESM::RefId& keyId = door.getCellRef().getKey();
        if (keyId.empty())
            return;

        MWWorld::ContainerStore& invStore = actor.getClass().getContainerStore(actor);
        MWWorld::Ptr keyPtr = invStore.search(keyId);

        if (!keyPtr.isEmpty())
            MWBase::Environment::get().getLuaManager()->objectActivated(door, actor);
    }
}

const MWMechanics::PathgridGraph& MWMechanics::AiPackage::getPathGridGraph(const ESM::Pathgrid* pathgrid) const
{
    if (!pathgrid || pathgrid->mPoints.empty())
        return PathgridGraph::sEmpty;
    // static cache is OK for now, pathgrids can never change during runtime
    static std::map<const ESM::Pathgrid*, std::unique_ptr<MWMechanics::PathgridGraph>> cache;
    auto found = cache.find(pathgrid);
    if (found == cache.end())
        found = cache.emplace(pathgrid, std::make_unique<MWMechanics::PathgridGraph>(*pathgrid)).first;
    return *found->second.get();
}

bool MWMechanics::AiPackage::shortcutPath(const vsg::vec3& startPoint, const vsg::vec3& endPoint,
    const MWWorld::Ptr& actor, bool* destInLOS, bool isPathClear)
{
    if (!mShortcutProhibited || vsg::length(mShortcutFailPos - startPoint) >= PATHFIND_SHORTCUT_RETRY_DIST)
    {
        // check if target is clearly visible
        isPathClear = !MWBase::Environment::get()
                           .getWorld()
                           ->getRayCasting()
                           ->castRay(startPoint, endPoint, MWPhysics::Layers::WORLD | MWPhysics::Layers::DOOR)
                           .mHit;

        if (destInLOS != nullptr)
            *destInLOS = isPathClear;

        if (!isPathClear)
            return false;

        // check if an actor can move along the shortcut path
        isPathClear = checkWayIsClearForActor(startPoint, endPoint, actor);
    }

    if (isPathClear) // can shortcut the path
    {
        mPathFinder.clearPath();
        mPathFinder.addPointToPath(endPoint);
        return true;
    }

    return false;
}

bool MWMechanics::AiPackage::checkWayIsClearForActor(
    const vsg::vec3& startPoint, const vsg::vec3& endPoint, const MWWorld::Ptr& actor)
{
    if (canActorMoveByZAxis(actor))
        return true;

    const float actorSpeed = actor.getClass().getMaxSpeed(actor);
    const float maxAvoidDist
        = AI_REACTION_TIME * actorSpeed + actorSpeed / getAngularVelocity(actorSpeed) * 2; // *2 - for reliability
    const float distToTarget = vsg::length(vsg::vec2(endPoint.x, endPoint.y));

    const float offsetXY = distToTarget > maxAvoidDist * 1.5 ? maxAvoidDist : maxAvoidDist / 2;

    // update shortcut prohibit state
    if (checkWayIsClear(startPoint, endPoint, offsetXY))
    {
        if (mShortcutProhibited)
        {
            mShortcutProhibited = false;
            mShortcutFailPos = vsg::vec3();
        }
        return true;
    }
    else
    {
        if (mShortcutFailPos == vsg::vec3())
        {
            mShortcutProhibited = true;
            mShortcutFailPos = startPoint;
        }
    }

    return false;
}

bool MWMechanics::AiPackage::doesPathNeedRecalc(const vsg::vec3& newDest, const MWWorld::Ptr& actor) const
{
    return mPathFinder.getPath().empty() || getPathDistance(actor, mPathFinder.getPath().back(), newDest) > 10
        || mPathFinder.getPathCell() != actor.getCell();
}

bool MWMechanics::AiPackage::isNearInactiveCell(vsg::vec3 position)
{
    const MWWorld::Cell* playerCell = getPlayer().getCell()->getCell();
    if (playerCell->isExterior())
    {
        // get actor's distance from origin of center cell
        position = Misc::makeCoordinateConverter(*playerCell).toLocalVec3(position);

        // currently assumes 3 x 3 grid for exterior cells, with player at center cell.
        // AI shuts down actors before they reach edges of 3 x 3 grid.
        const float distanceFromEdge = 200.0;
        float minThreshold = (-1.0f * ESM::Land::REAL_SIZE) + distanceFromEdge;
        float maxThreshold = (2.0f * ESM::Land::REAL_SIZE) - distanceFromEdge;
        return (position.x < minThreshold) || (maxThreshold < position.x) || (position.y < minThreshold)
            || (maxThreshold < position.y);
    }
    else
    {
        return false;
    }
}

bool MWMechanics::AiPackage::isReachableRotatingOnTheRun(const MWWorld::Ptr& actor, const vsg::vec3& dest)
{
    // get actor's shortest radius for moving in circle
    float speed = actor.getClass().getMaxSpeed(actor);
    speed += speed * 0.1f; // 10% real speed inaccuracy
    float radius = speed / getAngularVelocity(speed);

    // get radius direction to the center
    const float* rot = actor.getRefData().getPosition().rot;
    const vsg::dquat quatRot = Misc::VsgMath::makeRotateFromAxis(rot[0], -vsg::vec3(1, 0, 0))
        * Misc::VsgMath::makeRotateFromAxis(rot[1], -vsg::vec3(0, 1, 0))
        * Misc::VsgMath::makeRotateFromAxis(rot[2], -vsg::vec3(0, 0, 1));
    vsg::vec3 dir = vsg::quat(quatRot) * vsg::vec3(0, 1, 0); // actor's orientation direction is a tangent to circle
    vsg::vec3 radiusDir = vsg::cross(dir, vsg::vec3(0, 0, 1)); // radius is perpendicular to a tangent
    Misc::VsgMath::normalizeInPlace(radiusDir);
    radiusDir *= radius;

    // pick up the nearest center candidate
    vsg::vec3 pos = actor.getRefData().getPosition().asVec3();
    vsg::vec3 center1 = pos - radiusDir;
    vsg::vec3 center2 = pos + radiusDir;
    vsg::vec3 center = vsg::length2(center1 - dest) < vsg::length2(center2 - dest) ? center1 : center2;

    float distToDest = vsg::length(center - dest);

    // if pathpoint is reachable for the actor rotating on the run:
    // no points of actor's circle should be farther from the center than destination point
    return (radius <= distToDest);
}

DetourNavigator::Flags MWMechanics::AiPackage::getNavigatorFlags(const MWWorld::Ptr& actor) const
{
    const MWWorld::Class& actorClass = actor.getClass();
    DetourNavigator::Flags result = DetourNavigator::Flag_none;

    if ((actorClass.isPureWaterCreature(actor)
            || (getTypeId() != AiPackageTypeId::Wander
                && ((Settings::game().mAllowActorsToFollowOverWaterSurface && getTypeId() == AiPackageTypeId::Follow)
                    || actorClass.canSwim(actor) || hasWaterWalking(actor))))
        && actorClass.getSwimSpeed(actor) > 0)
        result |= DetourNavigator::Flag_swim;

    if (actorClass.canWalk(actor) && actor.getClass().getWalkSpeed(actor) > 0)
    {
        result |= DetourNavigator::Flag_walk;
        if (getTypeId() != AiPackageTypeId::Wander)
            result |= DetourNavigator::Flag_usePathgrid;
    }

    if (canOpenDoors(actor) && getTypeId() != AiPackageTypeId::Wander)
        result |= DetourNavigator::Flag_openDoor;

    return result;
}

DetourNavigator::AreaCosts MWMechanics::AiPackage::getAreaCosts(
    const MWWorld::Ptr& actor, DetourNavigator::Flags flags) const
{
    DetourNavigator::AreaCosts costs;
    const MWWorld::Class& actorClass = actor.getClass();

    const float walkSpeed = [&] {
        if ((flags & DetourNavigator::Flag_walk) == 0)
            return 0.0f;
        if (getTypeId() == AiPackageTypeId::Wander)
            return actorClass.getWalkSpeed(actor);
        return actorClass.getRunSpeed(actor);
    }();

    const float swimSpeed = [&] {
        if ((flags & DetourNavigator::Flag_swim) == 0)
            return 0.0f;
        if (hasWaterWalking(actor))
            return walkSpeed;
        return actorClass.getSwimSpeed(actor);
    }();

    const float maxSpeed = std::max(swimSpeed, walkSpeed);

    if (maxSpeed == 0)
        return costs;

    const float swimFactor = swimSpeed / maxSpeed;
    const float walkFactor = walkSpeed / maxSpeed;

    costs.mWater = divOrMax(costs.mWater, swimFactor);
    costs.mDoor = divOrMax(costs.mDoor, walkFactor);
    costs.mPathgrid = divOrMax(costs.mPathgrid, walkFactor);
    costs.mGround = divOrMax(costs.mGround, walkFactor);

    return costs;
}

vsg::vec3 MWMechanics::AiPackage::getNextPathPoint(const vsg::vec3& destination) const
{
    return mPathFinder.getPath().empty() ? destination : mPathFinder.getPath().front();
}

float MWMechanics::AiPackage::getNextPathPointTolerance(float speed, float duration, const vsg::vec3& halfExtents) const
{
    if (mPathFinder.getPathSize() <= 1)
        return std::max(DEFAULT_TOLERANCE, mLastDestinationTolerance);
    return getPointTolerance(speed, duration, halfExtents);
}
