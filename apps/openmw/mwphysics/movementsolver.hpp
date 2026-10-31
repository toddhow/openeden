#ifndef OPENMW_MWPHYSICS_MOVEMENTSOLVER_H
#define OPENMW_MWPHYSICS_MOVEMENTSOLVER_H

#include <components/misc/vsgmath.hpp>

#include <vsg/maths/vec3.h>

#include <components/misc/constants.hpp>

namespace JPH
{
    class PhysicsSystem;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWPhysics
{
    /// Vector projection
    static inline vsg::vec3 project(const vsg::vec3& u, const vsg::vec3& v)
    {
        return v * vsg::dot(u, v);
    }

    /// Vector rejection
    static inline vsg::vec3 reject(const vsg::vec3& direction, const vsg::vec3& planeNormal)
    {
        return direction - project(direction, planeNormal);
    }

    template <class Vec3>
    static bool isWalkableSlope(const Vec3& normal)
    {
        static const float sMaxSlopeCos = std::cos(Misc::VsgMath::inDegrees(Constants::sMaxSlope));
        return (normal.z > sMaxSlopeCos);
    }

    class Actor;
    struct ActorFrameData;
    struct WorldFrameData;

    class MovementSolver
    {
    public:
        static vsg::vec3 traceDown(const MWWorld::Ptr& ptr, const vsg::vec3& position, Actor* actor,
            JPH::PhysicsSystem* collisionWorld, float maxHeight);
        static void move(ActorFrameData& actor, float time, const JPH::PhysicsSystem* physicsSystem,
            const WorldFrameData& worldData);
        static void unstuck(ActorFrameData& actor, JPH::PhysicsSystem* physicsSystem);
    };
}

#endif
