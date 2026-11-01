#pragma once

// The motor solver: turns an animated target pose into joint motor targets,
// once per physics step (M3, docs/tasks/0013).

#include "opas/math/Transform.hpp"
#include "opas/motor/Ragdoll.hpp"
#include "opas/physics/PhysicsWorld.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace opas::motor
{

    // Sets every joint's target from `localPose` (one local transform per
    // skeleton bone, like Skeleton::localRest; translations are ignored because
    // joints only rotate). Bones without bodies still count through forward
    // kinematics. A kinematic root body is also moved to its target. Allocates
    // nothing; call it from a pre-step listener. Returns false, and does
    // nothing, if localPose does not have one entry per skeleton bone.
    // When dtSeconds > 0 and ragdoll has a previous target, feeds forward
    // joint target angular velocity in constraint space to eliminate tracking lag.
    bool driveTowardsPose(physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const math::Transform> localPose,
        float dtSeconds = 0.0f);

    // Resets target history so the next driveTowardsPose does not compute
    // velocity feed-forward across a discontinuous jump.
    void resetJointTargetHistory(Ragdoll& ragdoll);

    // Fills `localPose` (one entry per skeleton bone) with the target pose for
    // `timeSeconds`.
    using PoseSource = std::function<void(double timeSeconds, std::span<math::Transform> localPose)>;

    // A step listener that drives one ragdoll towards a pose source every step.
    // Each step it asks for the pose at the end of the step being simulated,
    // (steps so far + 1) * dt, counted in steps so time never drifts.
    //
    // Ownership: the host owns the driver, the ragdoll and the world. The driver
    // keeps a pointer to the ragdoll (not to the world, which it is handed each
    // step); the ragdoll must outlive it, and the host removes the driver from
    // the world before destroying either.
    class RagdollDriver final : public physics::StepListener
    {
    public:
        RagdollDriver(Ragdoll& ragdoll, PoseSource source);

        void onPreStep(physics::PhysicsWorld& world, float dtSeconds) override;
        void onPostStep(physics::PhysicsWorld& world, float dtSeconds) override;

        [[nodiscard]] std::uint64_t stepsDriven() const { return steps_; }

    private:
        Ragdoll* ragdoll_; // not owned; see the class comment
        PoseSource source_;
        std::vector<math::Transform> localPose_; // sized once, reused every step
        std::uint64_t steps_ = 0;
    };

} // namespace opas::motor
