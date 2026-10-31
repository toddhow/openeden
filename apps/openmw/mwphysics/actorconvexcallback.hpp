#ifndef OPENMW_MWPHYSICS_ACTORCONVEXCALLBACK_H
#define OPENMW_MWPHYSICS_ACTORCONVEXCALLBACK_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/convert.hpp>

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>

#include "../mwrender/joltdebugdraw.hpp"

#include "joltlayers.hpp"
#include "physicssystem.hpp"

namespace MWPhysics
{
    class ActorConvexCallback : public JPH::CastShapeCollector
    {
    public:
        ActorConvexCallback()
            : mClosestHitFraction(0.0f)
            , mMinCollisionDot(0.0f)
            , mHitCollisionLayer(0)
            , mHitPointWorld(0.0f, 0.0f, 0.0f)
            , mHitNormalWorld(JPH::Vec3(0.0, 0.0, 0.0))
            , mMotion(JPH::Vec3(0.0, 0.0, 0.0))
            , mPhysicsSystem(nullptr)
            , mSelfCenterWorld(0.0f, 0.0f, 0.0f)
            , mOrigin(0.0f, 0.0f, 0.0f)
        {
        }

        ActorConvexCallback(const JPH::BodyID actor, const JPH::PhysicsSystem* inPhysicsSystem,
            JPH::RVec3 selfCenterWorld, JPH::RVec3 origin, float minCollisionDot, JPH::Vec3 motion)
            : mMinCollisionDot(minCollisionDot)
            , mMe(actor)
            , mHitCollisionLayer(0)
            , mHitPointWorld(0.0f, 0.0f, 0.0f)
            , mHitNormalWorld(JPH::Vec3(0.0f, 0.0f, 0.0f))
            , mMotion(motion)
            , mPhysicsSystem(inPhysicsSystem)
            , mSelfCenterWorld(Misc::Convert::toOsg(selfCenterWorld))
            , mOrigin(Misc::Convert::toOsg(origin))
        {
        }

        virtual void AddHit(const JPH::ShapeCastResult& inResult) override
        {
            // If self, skip
            if (inResult.mBodyID2 == mMe)
            {
                return;
            }

            JPH::BodyLockRead lock(mPhysicsSystem->GetBodyLockInterfaceNoLock(), inResult.mBodyID2);
            if (!lock.Succeeded())
                return;

            const JPH::Body* body = &lock.GetBody();

            if (body->IsSensor())
                return;

            const JPH::ObjectLayer collisionGroup = body->GetObjectLayer();

            JPH::Vec3 hitNormalWorld = -inResult.mPenetrationAxis.Normalized();

            // override data for actor-actor collisions
            // vanilla Morrowind seems to make overlapping actors collide as though they are both cylinders with a
            // diameter of the distance between them For some reason this doesn't work as well as it should when using
            // capsules, but it still helps a lot.
            float earlyOut = inResult.GetEarlyOutFraction();
            if (collisionGroup == Layers::ACTOR)
            {
                // Jolt reports shapes that already intersect at the start of the cast with a fraction of 0
                const bool isOverlapping = inResult.mFraction <= 0.0f;
                if (isOverlapping)
                {
                    auto originA = mSelfCenterWorld;
                    auto originB = Misc::Convert::toOsg(body->GetCenterOfMassTransform().GetTranslation());
                    vsg::vec3 motion = Misc::Convert::toOsg(mMotion);
                    vsg::vec3 normal = (originA - originB);
                    normal.z = 0;
                    Misc::VsgMath::normalizeInPlace(normal);

                    // only collide if horizontally moving towards the hit actor, otherwise ignore the overlap so
                    // that actors spawned inside each other (e.g. placeatpc) are able to walk apart
                    if (vsg::dot(normal, motion) >= 0.0f)
                        return;

                    // Get the contact properties
                    mHitCollisionObject = body;
                    mHitCollisionLayer = collisionGroup;
                    mHitPointWorld = mOrigin + Misc::Convert::toOsg(inResult.mContactPointOn2);
                    mHitNormalWorld = Misc::Convert::toJolt<JPH::Vec3>(normal);
                    mClosestHitFraction = 0.0f;
                    ForceEarlyOut();
                    return;
                }
            }

            if (collisionGroup == Layers::PROJECTILE)
                return;

            // dot product of the motion vector against the collision contact normal
            const float approachDot = -mMotion.Dot(hitNormalWorld);
            if (approachDot <= mMinCollisionDot)
                return;

            // Test if this collision is closer/deeper than the previous one
            if (earlyOut < GetEarlyOutFraction())
            {
                // Update early out fraction to this hit
                UpdateEarlyOutFraction(earlyOut);

                // Get the contact properties
                mHitCollisionObject = body;
                mHitCollisionLayer = collisionGroup;
                mHitPointWorld = mOrigin + Misc::Convert::toOsg(inResult.mContactPointOn2);
                mHitNormalWorld = hitNormalWorld;
                mClosestHitFraction = inResult.mFraction;
            }
        }

        bool hasHit() const { return mHitCollisionObject != nullptr; }

        float mClosestHitFraction;
        const float mMinCollisionDot;
        const JPH::BodyID mMe;
        const JPH::Body* mHitCollisionObject = nullptr;

        JPH::ObjectLayer mHitCollisionLayer;
        vsg::vec3 mHitPointWorld;
        JPH::Vec3 mHitNormalWorld;
        JPH::Vec3 mMotion;

    private:
        const JPH::PhysicsSystem* mPhysicsSystem;
        vsg::vec3 mSelfCenterWorld;
        vsg::vec3 mOrigin;
    };
}

#endif
