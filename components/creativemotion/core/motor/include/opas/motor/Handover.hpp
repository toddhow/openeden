#pragma once

// Pop-free kinematic <-> dynamic handover and recovery blending (task 0022, stage C4c).
//
// Bridges the animation runtime and physical simulation:
// - computeBodyVelocity: computes derivative linear and angular velocities from consecutive
//   world transforms using shortest-arc quaternion differentiation.
// - handoverToDynamic: transfers bodies from kinematic to dynamic simulation, setting
//   motion type to Dynamic and applying initial linear and angular velocities so momentum
//   is preserved without position popping.
// - handoverToKinematic: transfers bodies from dynamic simulation to kinematic, setting
//   motion type to Kinematic and resetting joint target history.
// - computeRagdollBodyTransforms: evaluates forward kinematics into preallocated world transform
//   buffers without heap allocations.
// - RecoveryBlender: smooth C1 Hermite / smoothstep blending from a physical fallen pose
//   back into an incoming animation pose over an authored duration without popping.
//
// Zero heap allocations on the hot path (verified by opas_alloc_tests).

#include "opas/math/Transform.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/motor/Ragdoll.hpp"
#include "opas/physics/PhysicsWorld.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace opas::motor
{

    struct BodyVelocity
    {
        math::Vec3 linearMps;
        math::Vec3 angularRadps;
    };

    // Computes linear and angular velocity from consecutive world transforms:
    // v = (current.translation - previous.translation) / dtSeconds
    // omega = axisAngle(current.rotation * conjugate(previous.rotation)) / dtSeconds
    // Shortest-arc quaternion derivative ensures angular velocity magnitude <= pi / dtSeconds.
    // Returns zero velocities when dtSeconds <= 0.0f.
    BodyVelocity computeBodyVelocity(
        const math::Transform& currentWorld, const math::Transform& previousWorld, float dtSeconds);

    // Hands over specified bodies (or all bodies if bodyIndices is empty) to Dynamic,
    // setting motion type to Dynamic and applying the specified velocities.
    // If ragdoll.rootBody is among bodyIndices (or bodyIndices is empty), sets ragdoll.kinematicRoot = false.
    // Allocates nothing.
    bool handoverToDynamic(physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const std::size_t> bodyIndices,
        std::span<const BodyVelocity> velocities);

    // Overload computing velocities from previous and current body world transforms.
    // Allocates nothing.
    bool handoverToDynamic(physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const std::size_t> bodyIndices,
        std::span<const math::Transform> currentBodyWorld, std::span<const math::Transform> previousBodyWorld,
        float dtSeconds);

    // Switches specified bodies (or all bodies if bodyIndices is empty) to Kinematic.
    // If ragdoll.rootBody is among bodyIndices (or bodyIndices is empty), sets ragdoll.kinematicRoot = true
    // and calls resetJointTargetHistory(ragdoll).
    // Allocates nothing.
    bool handoverToKinematic(
        physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const std::size_t> bodyIndices = {});

    // Computes body world transforms from skeleton local pose and rootWorld without heap allocations.
    // outWorldTransforms must have at least ragdoll.bodies.size() elements.
    bool computeRagdollBodyTransforms(const Ragdoll& ragdoll, std::span<const math::Transform> localPose,
        const math::Transform& rootWorld, std::span<math::Transform> outWorldTransforms);

    // RecoveryBlender: blends physical landing/fallen pose into incoming animation pose over a duration.
    class RecoveryBlender
    {
    public:
        explicit RecoveryBlender(const Ragdoll& ragdoll); // preallocates internal scratch buffers

        bool start(physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const std::size_t> bodyIndices,
            float durationSeconds);

        bool update(physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const math::Transform> targetLocalPose,
            const math::Transform& targetRootWorld, float dtSeconds);

        [[nodiscard]] bool active() const { return active_; }
        [[nodiscard]] bool isComplete() const { return complete_; }
        [[nodiscard]] float progress() const { return progress_; }
        [[nodiscard]] float elapsedSeconds() const { return elapsedS_; }
        [[nodiscard]] float durationSeconds() const { return durationS_; }
        void cancel();

    private:
        std::vector<math::Transform> startBodyWorld_;
        std::vector<math::Transform> currentTargetBodyWorld_;
        std::vector<std::size_t> kinematicBodies_;
        float durationS_ = 0.0f;
        float elapsedS_ = 0.0f;
        float progress_ = 0.0f;
        bool active_ = false;
        bool complete_ = false;
    };

} // namespace opas::motor
