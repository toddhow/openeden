#include "opas/motor/MotorSolver.hpp"

#include "Conversions.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace opas::motor {

void resetJointTargetHistory(Ragdoll& ragdoll)
{
    ragdoll.hasLastJointTargets = false;
}

bool driveTowardsPose(physics::PhysicsWorld& world, Ragdoll& ragdoll,
                      std::span<const math::Transform> localPose,
                      float dtSeconds)
{
    if (localPose.size() != ragdoll.skeleton.bones.size() ||
        ragdoll.poseModel.size() != ragdoll.skeleton.bones.size()) {
        return false;
    }
    // Model-space pose (root = identity): joint targets depend only on
    // relative rotations, so the root's placement cancels out.
    rig::computeWorldPoseInto(ragdoll.skeleton, localPose, math::Transform{}, ragdoll.poseModel);

    const auto bodyModel = [&ragdoll](std::size_t bodyIndex) {
        const RagdollBody& body = ragdoll.bodies[bodyIndex];
        return ragdoll.poseModel[body.bone] * body.local;
    };

    const bool computeVelocity = dtSeconds > 0.0f && ragdoll.hasLastJointTargets &&
                                 ragdoll.lastJointTargets.size() == ragdoll.joints.size();

    for (std::size_t j = 0; j < ragdoll.joints.size(); ++j) {
        const RagdollJoint& joint = ragdoll.joints[j];
        const math::Quat target = jointRotation(ragdoll, j, bodyModel(joint.parentBody).rotation,
                                                bodyModel(joint.childBody).rotation);

        math::Vec3 targetOmega{0.0, 0.0, 0.0};
        if (computeVelocity) {
            const math::Quat prev = ragdoll.lastJointTargets[j];
            // In constraint space: deltaQ rotates from prev to target:
            // target = prev * deltaQ  <=>  deltaQ = prev^* * target
            math::Quat deltaQ = math::conjugate(prev) * target;
            if (deltaQ.w < 0.0) {
                deltaQ = {-deltaQ.x, -deltaQ.y, -deltaQ.z, -deltaQ.w};
            }
            const double sinHalfAngle = std::sqrt(std::max(0.0, 1.0 - deltaQ.w * deltaQ.w));
            if (sinHalfAngle > 1e-7) {
                const double angle = 2.0 * std::atan2(sinHalfAngle, deltaQ.w);
                const double scale = angle / (sinHalfAngle * static_cast<double>(dtSeconds));
                targetOmega = {deltaQ.x * scale, deltaQ.y * scale, deltaQ.z * scale};
            }
        }

        world.setJointTarget(joint.id, toPhysics(target), toPhysics(targetOmega));

        if (j < ragdoll.lastJointTargets.size()) {
            ragdoll.lastJointTargets[j] = target;
        }
    }
    if (ragdoll.lastJointTargets.size() == ragdoll.joints.size()) {
        ragdoll.hasLastJointTargets = true;
    }

    if (ragdoll.kinematicRoot) {
        const math::Transform rootTarget = ragdoll.rootWorld * bodyModel(ragdoll.rootBody);
        world.setKinematicTarget(ragdoll.bodies[ragdoll.rootBody].id,
                                 toPhysics(rootTarget.translation), toPhysics(rootTarget.rotation));
    }
    return true;
}

RagdollDriver::RagdollDriver(Ragdoll& ragdoll, PoseSource source)
    : ragdoll_(&ragdoll), source_(std::move(source)), localPose_(ragdoll.skeleton.bones.size())
{
    // Start from the rest pose, so a source that fills only some bones still
    // gives a complete pose.
    for (std::size_t i = 0; i < localPose_.size(); ++i) {
        localPose_[i] = ragdoll.skeleton.bones[i].localRest;
    }
}

void RagdollDriver::onPreStep(physics::PhysicsWorld& world, float dtSeconds)
{
    const double timeSeconds = static_cast<double>(steps_ + 1) * static_cast<double>(dtSeconds);
    source_(timeSeconds, localPose_);
    driveTowardsPose(world, *ragdoll_, localPose_, dtSeconds);
    ++steps_;
}

void RagdollDriver::onPostStep(physics::PhysicsWorld& /*world*/, float /*dtSeconds*/)
{}

} // namespace opas::motor
