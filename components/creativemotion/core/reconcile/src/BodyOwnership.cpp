#include "opas/reconcile/BodyOwnership.hpp"

#include <algorithm>

namespace opas::reconcile {

const char* bodyOwnershipName(BodyOwnership ownership) noexcept
{
    switch (ownership) {
    case BodyOwnership::Kinematic:
        return "Kinematic";
    case BodyOwnership::Simulated:
        return "Simulated";
    case BodyOwnership::PassiveRagdoll:
        return "PassiveRagdoll";
    case BodyOwnership::HandedOff:
        return "HandedOff";
    default:
        return "Unknown";
    }
}

OwnershipController::OwnershipController() noexcept
{
    bodyOwnerships_.fill(BodyOwnership::Simulated);
    partOwnerships_.fill(BodyOwnership::Simulated);
}

OwnershipController::OwnershipController(const arbiter::BodyPartMap& partMap, const rig::Profile& profile)
    : profile_(profile)
{
    bodyCount_ = std::min(partMap.bodyCount(), kMaxBodies);
    bodyOwnerships_.fill(BodyOwnership::Simulated);
    partOwnerships_.fill(BodyOwnership::Simulated);

    for (std::size_t i = 0; i < bodyCount_; ++i) {
        bodyParts_[i] = partMap.bodyPart(i);
    }
}

BodyOwnership OwnershipController::bodyOwnership(std::size_t bodyIndex) const noexcept
{
    if (bodyIndex >= bodyCount_) {
        return BodyOwnership::Simulated;
    }
    return bodyOwnerships_[bodyIndex];
}

BodyOwnership OwnershipController::partOwnership(arbiter::BodyPart part) const noexcept
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx >= partOwnerships_.size()) {
        return BodyOwnership::Simulated;
    }
    return partOwnerships_[idx];
}

void OwnershipController::setBodyOwnership(std::size_t bodyIndex, BodyOwnership ownership) noexcept
{
    if (bodyIndex >= bodyCount_) {
        return;
    }
    bodyOwnerships_[bodyIndex] = ownership;
}

void OwnershipController::setPartOwnership(arbiter::BodyPart part, BodyOwnership ownership) noexcept
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx < partOwnerships_.size()) {
        partOwnerships_[idx] = ownership;
    }
    for (std::size_t i = 0; i < bodyCount_; ++i) {
        if (bodyParts_[i] == part) {
            bodyOwnerships_[i] = ownership;
        }
    }
}

void OwnershipController::setAllOwnership(BodyOwnership ownership) noexcept
{
    bodyOwnerships_.fill(ownership);
    partOwnerships_.fill(ownership);
}

void OwnershipController::apply(motor::Ragdoll& ragdoll, physics::PhysicsWorld& world)
{
    if (ragdoll.rootBody < bodyCount_) {
        ragdoll.kinematicRoot = (bodyOwnerships_[ragdoll.rootBody] == BodyOwnership::Kinematic);
    }

    for (std::size_t i = 0; i < ragdoll.bodies.size(); ++i) {
        const BodyOwnership ow = (i < bodyCount_) ? bodyOwnerships_[i] : BodyOwnership::Simulated;
        if (ow == BodyOwnership::Kinematic) {
            world.setMotionType(ragdoll.bodies[i].id, physics::MotionType::Kinematic);
        } else {
            world.setMotionType(ragdoll.bodies[i].id, physics::MotionType::Dynamic);
        }
    }

    for (std::size_t j = 0; j < ragdoll.joints.size(); ++j) {
        const std::size_t childIdx = ragdoll.joints[j].childBody;
        const BodyOwnership childOw = (childIdx < bodyCount_) ? bodyOwnerships_[childIdx] : BodyOwnership::Simulated;
        if (childOw == BodyOwnership::PassiveRagdoll) {
            physics::JointMotor off{};
            off.stiffnessNmPerRad = 0.0f;
            off.dampingNmsPerRad = 0.0f;
            world.setJointMotor(ragdoll.joints[j].id, off);
        } else if (childOw == BodyOwnership::Simulated) {
            const std::string_view boneName =
                (childIdx < ragdoll.bodies.size() &&
                 ragdoll.bodies[childIdx].bone < ragdoll.skeleton.bones.size())
                    ? std::string_view{ragdoll.skeleton.bones[ragdoll.bodies[childIdx].bone].name}
                    : std::string_view{};
            const rig::MotorGains gains = rig::motorFor(profile_, boneName);
            physics::JointMotor m{};
            m.stiffnessNmPerRad = static_cast<float>(gains.stiffnessNmPerRad);
            m.dampingNmsPerRad = static_cast<float>(gains.dampingNmsPerRad);
            m.maxTorqueNm = static_cast<float>(gains.maxTorqueNm);
            world.setJointMotor(ragdoll.joints[j].id, m);
        }
    }
}

} // namespace opas::reconcile
