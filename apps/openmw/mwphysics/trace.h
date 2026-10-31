#ifndef OENGINE_PHYSICS_ACTOR_TRACE_H
#define OENGINE_PHYSICS_ACTOR_TRACE_H

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace JPH
{
    class PhysicsSystem;
    class Body;
}

namespace MWPhysics
{
    class Actor;

    struct ActorTracer
    {
        vsg::vec3 mEndPos;
        vsg::vec3 mPlaneNormal;
        vsg::vec3 mHitPoint;
        JPH::ObjectLayer mHitObjectLayer;
        const JPH::Body* mHitObject;

        float mFraction;

        void doTrace(const JPH::BodyID actor, const vsg::vec3& start, const vsg::vec3& end,
            const JPH::PhysicsSystem* world, const int collisionMask, bool attemptShortTrace = false);
        void findGround(
            const Actor* actor, const vsg::vec3& start, const vsg::vec3& end, const JPH::PhysicsSystem* physicsSystem);
    };
}

#endif
