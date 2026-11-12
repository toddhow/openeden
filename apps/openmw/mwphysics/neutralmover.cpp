#include "neutralmover.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <components/misc/convert.hpp>

#include "joltfilters.hpp"

namespace MWPhysics::NeutralMover
{
    namespace
    {
        bool finite(const vsg::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        float length2d(float x, float y)
        {
            return std::sqrt(x * x + y * y);
        }
    }

    bool Request::isMoving() const
    {
        return std::isfinite(mVelX) && std::isfinite(mVelY) && (mVelX != 0.f || mVelY != 0.f);
    }

    float headingOf(float velX, float velY)
    {
        if (!std::isfinite(velX) || !std::isfinite(velY) || (velX == 0.f && velY == 0.f))
            return 0.f;
        return std::atan2(velX, velY);
    }

    Result step(const vsg::vec3& feet, const Request& request, float dt, const Sweeper& world)
    {
        Result result;
        result.mFeet = feet;
        if (!(dt > 0.f) || !std::isfinite(dt) || !request.isMoving() || !finite(feet))
            return result;

        float vx = request.mVelX;
        float vy = request.mVelY;
        const float speed = length2d(vx, vy);
        if (speed > kMaxSpeed)
        {
            vx *= kMaxSpeed / speed;
            vy *= kMaxSpeed / speed;
        }
        vsg::vec3 remaining(vx * dt, vy * dt, 0.f);
        if (length2d(remaining.x, remaining.y) < kMinMove)
            return result;

        vsg::vec3 pos = feet;
        for (int pass = 0; pass < kMaxSlidePasses; ++pass)
        {
            // Raise, so that a low ledge and the floor itself are not in the way of the sideways sweep.
            const Sweep up = world.sweep(pos, vsg::vec3(0.f, 0.f, kStepUp));
            const float rise = up.mHit ? std::max(0.f, up.mFraction * kStepUp - kSkin) : kStepUp;
            const vsg::vec3 raised(pos.x, pos.y, pos.z + rise);

            const float length = length2d(remaining.x, remaining.y);
            const Sweep side = world.sweep(raised, remaining);
            float fraction = 1.f;
            if (side.mHit)
                fraction = std::clamp(side.mFraction - kSkin / length, 0.f, 1.f);
            const vsg::vec3 landing(raised.x + remaining.x * fraction, raised.y + remaining.y * fraction, raised.z);

            // Put the capsule down on whatever is below: within the raise and kStepDown.
            const float drop = rise + kStepDown;
            const Sweep down = world.sweep(landing, vsg::vec3(0.f, 0.f, -drop));
            if (!down.mHit)
            {
                result.mBlocked = true;
                result.mEdge = true;
                break;
            }
            const vsg::vec3 rest(landing.x, landing.y, landing.z - drop * down.mFraction);
            // The capsule's rim may rest on the corner of a step, whose normal is no floor normal: ask for the ground
            // under its centre. No ground there (a ledge) refuses the pass; a slope that is too steep does too.
            const Sweep ground = world.probeDown(vsg::vec3(rest.x, rest.y, rest.z + kProbeLift), kProbeLift + kStepDown);
            if (!ground.mHit)
            {
                result.mBlocked = true;
                result.mEdge = true;
                break;
            }
            if (ground.mNormal.z < kMinFloorNormalZ)
            {
                result.mBlocked = true;
                break;
            }
            pos = rest;
            result.mMoved = true;

            if (!side.mHit)
                break;
            result.mBlocked = true;
            // Slide along the wall with what is left of the motion.
            const float nl = length2d(side.mNormal.x, side.mNormal.y);
            if (!(nl > 1e-4f))
                break;
            const float nx = side.mNormal.x / nl;
            const float ny = side.mNormal.y / nl;
            const float left = 1.f - fraction;
            float rx = remaining.x * left;
            float ry = remaining.y * left;
            const float into = rx * nx + ry * ny;
            if (into < 0.f)
            {
                rx -= into * nx;
                ry -= into * ny;
            }
            remaining = vsg::vec3(rx, ry, 0.f);
            if (length2d(rx, ry) < kMinMove)
                break;
        }
        result.mFeet = pos;
        return result;
    }

    JoltSweeper::JoltSweeper(
        const JPH::PhysicsSystem& system, JPH::RefConst<JPH::Shape> shape, JPH::BodyID self, int layerMask)
        : mSystem(system)
        , mShape(std::move(shape))
        , mSelf(self)
        , mLayerMask(layerMask)
    {
    }

    Sweep JoltSweeper::sweep(const vsg::vec3& from, const vsg::vec3& delta) const
    {
        Sweep result;
        if (mShape == nullptr || !finite(from) || !finite(delta))
            return result;

        const JPH::RMat44 world = JPH::RMat44::sTranslation(Misc::Convert::toJolt<JPH::RVec3>(from));
        // The shape is in the body's frame, so the cast starts from the body's transform, not from its centre of mass.
        const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
            mShape.GetPtr(), JPH::Vec3::sReplicate(1.0f), world, Misc::Convert::toJolt<JPH::Vec3>(delta));

        JPH::ShapeCastSettings settings;
        // Triangles of a mesh are one-sided; the capsule must not catch on the back of a floor it is standing under.
        settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
        settings.mBackFaceModeConvex = JPH::EBackFaceMode::IgnoreBackFaces;

        JPH::BroadPhaseLayerFilter allBroadPhaseLayers;
        MaskedObjectLayerFilter objectLayers(mLayerMask);
        JPH::IgnoreSingleBodyFilter ownBody(mSelf);
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        mSystem.GetNarrowPhaseQuery().CastShape(
            cast, settings, JPH::RVec3::sZero(), collector, allBroadPhaseLayers, objectLayers, ownBody);

        if (collector.HadHit())
        {
            result.mHit = true;
            result.mFraction = std::clamp(collector.mHit.mFraction, 0.f, 1.f);
            const JPH::Vec3 normal = -collector.mHit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisZ());
            result.mNormal = vsg::vec3(normal.GetX(), normal.GetY(), normal.GetZ());
        }
        return result;
    }

    Sweep JoltSweeper::probeDown(const vsg::vec3& from, float depth) const
    {
        Sweep result;
        if (!finite(from) || !std::isfinite(depth) || !(depth > 0.f))
            return result;

        const JPH::RRayCast ray(Misc::Convert::toJolt<JPH::RVec3>(from), JPH::Vec3(0.f, 0.f, -depth));
        JPH::BroadPhaseLayerFilter allBroadPhaseLayers;
        MaskedObjectLayerFilter objectLayers(mLayerMask);
        JPH::IgnoreSingleBodyFilter ownBody(mSelf);
        JPH::RayCastResult hit;
        if (!mSystem.GetNarrowPhaseQuery().CastRay(ray, hit, allBroadPhaseLayers, objectLayers, ownBody))
            return result;

        result.mHit = true;
        result.mFraction = std::clamp(hit.mFraction, 0.f, 1.f);
        // The surface normal needs the body: a failed lock leaves the default (up), which is the safe reading for a
        // body that was removed between the cast and the lock.
        JPH::BodyLockRead lock(mSystem.GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Vec3 normal
                = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction));
            result.mNormal = vsg::vec3(normal.GetX(), normal.GetY(), normal.GetZ());
        }
        return result;
    }
}
