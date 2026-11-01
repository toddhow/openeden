#pragma once

// The physics interface: everything the behavior core needs from a physics
// engine, and nothing engine-specific. Backends (physics/jolt today, OpenMW's
// Jolt world later) implement PhysicsWorld.
//
// Threading: a world is used from one thread at a time, and step callbacks run
// on the thread that called step().

#include "opas/physics/Descriptions.hpp"
#include "opas/physics/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace opas::physics
{

    struct WorldConfig
    {
        float fixedStepSeconds = 1.0f / 60.0f; // must be > 0
        Vec3 gravityMps2{ 0.0f, 0.0f, -9.81f };
        std::uint32_t maxBodies = 1024; // must be > 0
        std::uint32_t maxJoints = 1024; // must be > 0
        // Physics sub-steps inside each fixed step, in [1, 16]. Listeners still run
        // once per step. Standing balance needs the solver at >= 120 Hz internally
        // (60 Hz: 2, 30 Hz: 4; measured in M5, docs/tasks/0017), so worlds that
        // balance ragdolls raise this; 1 keeps M1-M4 behaviour unchanged.
        std::uint32_t subStepsPerStep = 1;
        // Threads working on each step, in [1, 64] (task 0018). 1 runs Jolt on the
        // calling thread only, as before; more uses Jolt's thread pool, whose
        // threads live as long as the world. Fewer may work on a step: at most 32,
        // and fewer with many sub-steps (14 at 16; physics/jolt/CLAUDE.md). The
        // results are the same either way. Listeners always run on the thread that
        // calls step(), never on Jolt's workers, and no other call may run while
        // step() does.
        std::uint32_t workerThreads = 1;
    };

    struct BodyState
    {
        Vec3 positionM;
        Quat rotation;
        Vec3 linearVelocityMps;
        Vec3 angularVelocityRadps;
        bool awake = false; // false when the body is static or asleep
    };

    struct RayHit
    {
        BodyId body;
        Vec3 pointM;
        Vec3 normal; // unit, pointing away from the hit surface
        float fraction = 0.0f; // 0 at the ray origin, 1 at origin + directionTimesLength
    };

    class PhysicsWorld;

    // Hook the behavior core runs in. Not owned by the world: remove a listener
    // before destroying it. Listeners may read and drive bodies and joints, but
    // creating or destroying bodies or joints from a callback fails with
    // ErrorCode::CalledDuringStep.
    class StepListener
    {
    public:
        virtual ~StepListener() = default;
        virtual void onPreStep(PhysicsWorld& world, float dtSeconds) = 0;
        virtual void onPostStep(PhysicsWorld& world, float dtSeconds) = 0;
    };

    // Owned by the host through std::unique_ptr. Destroying the world destroys all
    // of its bodies and joints.
    class PhysicsWorld
    {
    public:
        PhysicsWorld() = default;
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        PhysicsWorld(PhysicsWorld&&) = delete;
        PhysicsWorld& operator=(PhysicsWorld&&) = delete;
        virtual ~PhysicsWorld() = default;

        // --- Build -------------------------------------------------------------
        virtual CreateResult<BodyId> createBody(const BodyDesc& desc) = 0;
        // Also destroys every joint attached to the body. False if the handle is stale.
        virtual bool destroyBody(BodyId body) = 0;

        virtual CreateResult<JointId> createJoint(const JointDesc& desc) = 0;
        virtual bool destroyJoint(JointId joint) = 0;

        // --- Collision groups ----------------------------------------------------
        // A group of bodies that can skip collisions among themselves, e.g. the
        // neighbouring parts of a ragdoll that overlap at their joints. Bodies join
        // through BodyDesc::collisionGroup and subGroup. Two bodies in the same
        // group collide unless they share a sub-group or their sub-groups were set
        // not to; bodies in different groups, or in none, always collide. A new
        // group lets every pair of different sub-groups collide.
        // subGroupCount must be in [1, min(WorldConfig::maxBodies, 65536)].
        virtual CreateResult<CollisionGroupId> createCollisionGroup(std::uint32_t subGroupCount) = 0;
        // False for a stale group, a sub-group >= subGroupCount, a == b, or during a step.
        virtual bool setSubGroupsCollide(CollisionGroupId group, std::uint32_t a, std::uint32_t b, bool collide) = 0;
        // Bodies already in the group keep its last settings. False if stale or during a step.
        virtual bool destroyCollisionGroup(CollisionGroupId group) = 0;

        // --- Drive joints --------------------------------------------------------
        virtual bool setJointMotor(JointId joint, const JointMotor& motor) = 0;
        // How many position iterations the solver spends on this joint each
        // (sub-)step, in [1, 255]; fewer is cheaper and lets limits stretch a
        // little (task 0018: dead ragdolls use 2, living joints 10). False if
        // stale, out of range, or during a step.
        virtual bool setJointPositionSteps(JointId joint, std::uint32_t steps) = 0;
        // Target rotation of body B's joint frame relative to body A's joint frame.
        // Identity means "the relative pose the joint was created in". Expressed
        // in joint-frame axes: X = twistAxis, Y = planeAxis cross twistAxis,
        // Z = planeAxis (Jolt's constraint space, SwingTwistConstraint.cpp; M1
        // documented Y and Z the wrong way round, corrected in M3). So a
        // pure twist of angle a is a rotation of a about X; a swing about Y is
        // limited by the plane swing range and a swing about Z by the normal
        // swing range (JointDesc). Identity stays the creation pose for both
        // swing shapes, even when it is not the middle of a range. A target
        // outside the limits is clamped to them.
        virtual bool setJointTarget(JointId joint, Quat targetRotation) = 0;

        // Target rotation and target angular velocity of body B relative to body A
        // in constraint space. Default implementation forwards to setJointTarget(joint, targetRotation)
        // for backends and mock worlds that do not implement velocity feed-forward.
        virtual bool setJointTarget(JointId joint, Quat targetRotation, Vec3 targetAngularVelocityRadps)
        {
            (void)targetAngularVelocityRadps;
            return setJointTarget(joint, targetRotation);
        }

        // --- Drive bodies --------------------------------------------------------
        virtual bool applyImpulse(BodyId body, Vec3 impulseNs, Vec3 worldPointM) = 0;
        virtual bool setVelocity(BodyId body, Vec3 linearMps, Vec3 angularRadps) = 0;
        // Dynamic bodies only: replaces the body's damping, with the same meaning
        // as BodyDesc::linearDamping / angularDamping (each >= 0 and finite). False
        // if stale, not dynamic, invalid, or during a step.
        virtual bool setBodyDamping(BodyId body, float linearPerS, float angularPerS) = 0;
        // Kinematic bodies only: reach this pose at the end of the next step, then
        // stay there until moved again.
        virtual bool setKinematicTarget(BodyId body, Vec3 positionM, Quat rotation) = 0;

        // --- Motion type (Stage C4c) ---------------------------------------------
        // Changes a body's motion type between Dynamic and Kinematic at runtime.
        // Setting Static on a Dynamic or Kinematic body deactivates it.
        // Returns false if stale, during a step, or if body was created Static.
        virtual bool setMotionType(BodyId body, MotionType motion)
        {
            (void)body;
            (void)motion;
            return false;
        }

        virtual std::optional<MotionType> getMotionType(BodyId body) const
        {
            (void)body;
            return std::nullopt;
        }

        // --- Read ----------------------------------------------------------------
        virtual bool readBody(BodyId body, BodyState& out) const = 0;
        // Reads many bodies without allocating. out.size() must be >= bodies.size().
        // Entries for stale handles are left untouched. Returns how many were read.
        virtual std::size_t readBodies(std::span<const BodyId> bodies, std::span<BodyState> out) const = 0;

        // --- Query ---------------------------------------------------------------
        // Closest hit along origin -> origin + directionTimesLengthM, skipping `ignore`.
        virtual std::optional<RayHit> castRay(Vec3 originM, Vec3 directionTimesLengthM, BodyId ignore = {}) const = 0;

        // --- Step ----------------------------------------------------------------
        // Advances exactly one fixed step: pre-step listeners, simulation,
        // post-step listeners, each listener list in registration order.
        virtual void step() = 0;
        [[nodiscard]] virtual float fixedStepSeconds() const = 0;

        // Safe to call from a callback: a listener added during a step is first
        // called on the next step; one removed during a step is not called again,
        // and the other listeners of that step are unaffected.
        virtual StepListenerId addStepListener(StepListener& listener) = 0;
        virtual bool removeStepListener(StepListenerId id) = 0;
    };

} // namespace opas::physics
