#include "opas/motor/Handover.hpp"

#include "Conversions.hpp"
#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/motor/MotorSolver.hpp"
#include "opas/rig/Skeleton.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace opas::motor
{

    namespace
    {

        constexpr math::Vec3 lerp(math::Vec3 a, math::Vec3 b, double t)
        {
            return a + (b - a) * t;
        }

        math::Quat slerp(math::Quat a, math::Quat b, double t)
        {
            double d = math::dot(a, b);
            if (d < 0.0)
            {
                b = math::Quat{ -b.x, -b.y, -b.z, -b.w };
                d = -d;
            }
            constexpr double kLinearAbove = 0.9995;
            double wa = 1.0 - t;
            double wb = t;
            if (d < kLinearAbove)
            {
                const double theta = std::acos(std::clamp(d, -1.0, 1.0));
                const double s = std::sin(theta);
                if (s > 1e-8)
                {
                    wa = std::sin((1.0 - t) * theta) / s;
                    wb = std::sin(t * theta) / s;
                }
            }
            return math::normalized(
                math::Quat{ wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z, wa * a.w + wb * b.w });
        }

    } // namespace

    BodyVelocity computeBodyVelocity(
        const math::Transform& currentWorld, const math::Transform& previousWorld, float dtSeconds)
    {
        if (dtSeconds <= 0.0f || !std::isfinite(dtSeconds))
        {
            return BodyVelocity{};
        }

        const double invDt = 1.0 / static_cast<double>(dtSeconds);
        const math::Vec3 linearVel = (currentWorld.translation - previousWorld.translation) * invDt;

        // deltaQ rotates from previous to current in world space:
        // current.rotation = deltaQ * previous.rotation  <=>  deltaQ = current.rotation * conjugate(previous.rotation)
        math::Quat deltaQ = currentWorld.rotation * math::conjugate(previousWorld.rotation);
        if (deltaQ.w < 0.0)
        {
            deltaQ = { -deltaQ.x, -deltaQ.y, -deltaQ.z, -deltaQ.w };
        }

        math::Vec3 angularVel{ 0.0, 0.0, 0.0 };
        const double sinHalfAngle = std::sqrt(std::max(0.0, 1.0 - deltaQ.w * deltaQ.w));
        if (sinHalfAngle > 1e-7)
        {
            const double angle = 2.0 * std::atan2(sinHalfAngle, deltaQ.w);
            const double scale = (angle / sinHalfAngle) * invDt;
            angularVel = { deltaQ.x * scale, deltaQ.y * scale, deltaQ.z * scale };
        }

        return BodyVelocity{ linearVel, angularVel };
    }

    bool computeRagdollBodyTransforms(const Ragdoll& ragdoll, std::span<const math::Transform> localPose,
        const math::Transform& rootWorld, std::span<math::Transform> outWorldTransforms)
    {
        constexpr std::size_t kMaxStackBones = 256;
        if (localPose.size() != ragdoll.skeleton.bones.size() || ragdoll.skeleton.bones.size() > kMaxStackBones
            || outWorldTransforms.size() < ragdoll.bodies.size())
        {
            return false;
        }

        std::array<math::Transform, kMaxStackBones> boneWorld;
        rig::computeWorldPoseInto(ragdoll.skeleton, localPose, rootWorld,
            std::span<math::Transform>(boneWorld.data(), ragdoll.skeleton.bones.size()));

        for (std::size_t i = 0; i < ragdoll.bodies.size(); ++i)
        {
            const RagdollBody& body = ragdoll.bodies[i];
            outWorldTransforms[i] = boneWorld[body.bone] * body.local;
        }
        return true;
    }

    bool handoverToDynamic(physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const std::size_t> bodyIndices,
        std::span<const BodyVelocity> velocities)
    {
        if (bodyIndices.empty())
        {
            if (!velocities.empty() && velocities.size() != ragdoll.bodies.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < ragdoll.bodies.size(); ++i)
            {
                world.setMotionType(ragdoll.bodies[i].id, physics::MotionType::Dynamic);
                if (!velocities.empty())
                {
                    world.setVelocity(ragdoll.bodies[i].id, toPhysics(velocities[i].linearMps),
                        toPhysics(velocities[i].angularRadps));
                }
            }
            ragdoll.kinematicRoot = false;
            return true;
        }

        if (!velocities.empty() && velocities.size() != bodyIndices.size())
        {
            return false;
        }
        for (std::size_t idx : bodyIndices)
        {
            if (idx >= ragdoll.bodies.size())
            {
                return false;
            }
        }
        for (std::size_t k = 0; k < bodyIndices.size(); ++k)
        {
            const std::size_t idx = bodyIndices[k];
            world.setMotionType(ragdoll.bodies[idx].id, physics::MotionType::Dynamic);
            if (!velocities.empty())
            {
                world.setVelocity(
                    ragdoll.bodies[idx].id, toPhysics(velocities[k].linearMps), toPhysics(velocities[k].angularRadps));
            }
            if (idx == ragdoll.rootBody)
            {
                ragdoll.kinematicRoot = false;
            }
        }
        return true;
    }

    bool handoverToDynamic(physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const std::size_t> bodyIndices,
        std::span<const math::Transform> currentBodyWorld, std::span<const math::Transform> previousBodyWorld,
        float dtSeconds)
    {
        if (bodyIndices.empty())
        {
            if (currentBodyWorld.size() != ragdoll.bodies.size() || previousBodyWorld.size() != ragdoll.bodies.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < ragdoll.bodies.size(); ++i)
            {
                const BodyVelocity vel = computeBodyVelocity(currentBodyWorld[i], previousBodyWorld[i], dtSeconds);
                world.setMotionType(ragdoll.bodies[i].id, physics::MotionType::Dynamic);
                world.setVelocity(ragdoll.bodies[i].id, toPhysics(vel.linearMps), toPhysics(vel.angularRadps));
            }
            ragdoll.kinematicRoot = false;
            return true;
        }

        if (currentBodyWorld.size() != bodyIndices.size() || previousBodyWorld.size() != bodyIndices.size())
        {
            return false;
        }
        for (std::size_t idx : bodyIndices)
        {
            if (idx >= ragdoll.bodies.size())
            {
                return false;
            }
        }
        for (std::size_t k = 0; k < bodyIndices.size(); ++k)
        {
            const std::size_t idx = bodyIndices[k];
            const BodyVelocity vel = computeBodyVelocity(currentBodyWorld[k], previousBodyWorld[k], dtSeconds);
            world.setMotionType(ragdoll.bodies[idx].id, physics::MotionType::Dynamic);
            world.setVelocity(ragdoll.bodies[idx].id, toPhysics(vel.linearMps), toPhysics(vel.angularRadps));
            if (idx == ragdoll.rootBody)
            {
                ragdoll.kinematicRoot = false;
            }
        }
        return true;
    }

    bool handoverToKinematic(physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const std::size_t> bodyIndices)
    {
        if (bodyIndices.empty())
        {
            for (std::size_t i = 0; i < ragdoll.bodies.size(); ++i)
            {
                world.setMotionType(ragdoll.bodies[i].id, physics::MotionType::Kinematic);
            }
            ragdoll.kinematicRoot = true;
            resetJointTargetHistory(ragdoll);
            return true;
        }

        for (std::size_t idx : bodyIndices)
        {
            if (idx >= ragdoll.bodies.size())
            {
                return false;
            }
        }
        for (std::size_t idx : bodyIndices)
        {
            world.setMotionType(ragdoll.bodies[idx].id, physics::MotionType::Kinematic);
            if (idx == ragdoll.rootBody)
            {
                ragdoll.kinematicRoot = true;
                resetJointTargetHistory(ragdoll);
            }
        }
        return true;
    }

    RecoveryBlender::RecoveryBlender(const Ragdoll& ragdoll)
        : startBodyWorld_(ragdoll.bodies.size())
        , currentTargetBodyWorld_(ragdoll.bodies.size())
    {
        kinematicBodies_.reserve(ragdoll.bodies.size());
    }

    bool RecoveryBlender::start(
        physics::PhysicsWorld& world, Ragdoll& ragdoll, std::span<const std::size_t> bodyIndices, float durationSeconds)
    {
        if (durationSeconds < 0.0f || !std::isfinite(durationSeconds))
        {
            return false;
        }

        if (startBodyWorld_.size() < ragdoll.bodies.size() || currentTargetBodyWorld_.size() < ragdoll.bodies.size())
        {
            return false;
        }

        for (std::size_t i = 0; i < ragdoll.bodies.size(); ++i)
        {
            physics::BodyState state;
            if (!world.readBody(ragdoll.bodies[i].id, state))
            {
                return false;
            }
            startBodyWorld_[i] = math::Transform{ fromPhysics(state.positionM), fromPhysics(state.rotation) };
        }

        if (!handoverToKinematic(world, ragdoll, bodyIndices))
        {
            return false;
        }

        kinematicBodies_.clear();
        if (bodyIndices.empty())
        {
            for (std::size_t i = 0; i < ragdoll.bodies.size(); ++i)
            {
                kinematicBodies_.push_back(i);
            }
        }
        else
        {
            for (std::size_t idx : bodyIndices)
            {
                kinematicBodies_.push_back(idx);
            }
        }

        durationS_ = durationSeconds;
        elapsedS_ = 0.0f;
        progress_ = (durationSeconds == 0.0f) ? 1.0f : 0.0f;
        active_ = (durationSeconds > 0.0f);
        complete_ = (durationSeconds == 0.0f);
        return true;
    }

    bool RecoveryBlender::update(physics::PhysicsWorld& world, Ragdoll& ragdoll,
        std::span<const math::Transform> targetLocalPose, const math::Transform& targetRootWorld, float dtSeconds)
    {
        if (!active_ && !complete_)
        {
            return false;
        }

        if (dtSeconds < 0.0f || !std::isfinite(dtSeconds))
        {
            dtSeconds = 0.0f;
        }

        if (active_)
        {
            elapsedS_ = std::min(durationS_, elapsedS_ + dtSeconds);
            const float u = durationS_ > 0.0f ? std::clamp(elapsedS_ / durationS_, 0.0f, 1.0f) : 1.0f;
            progress_ = u;
            if (elapsedS_ >= durationS_)
            {
                active_ = false;
                complete_ = true;
            }
        }

        if (!computeRagdollBodyTransforms(ragdoll, targetLocalPose, targetRootWorld, currentTargetBodyWorld_))
        {
            return false;
        }

        const float u = progress_;
        const double smoothAlpha = static_cast<double>(u * u * (3.0f - 2.0f * u));

        for (std::size_t b : kinematicBodies_)
        {
            if (b >= ragdoll.bodies.size())
            {
                continue;
            }
            const math::Transform& startT = startBodyWorld_[b];
            const math::Transform& targetT = currentTargetBodyWorld_[b];

            const math::Vec3 pos = lerp(startT.translation, targetT.translation, smoothAlpha);
            const math::Quat rot = slerp(startT.rotation, targetT.rotation, smoothAlpha);

            world.setKinematicTarget(ragdoll.bodies[b].id, toPhysics(pos), toPhysics(rot));
        }

        return true;
    }

    void RecoveryBlender::cancel()
    {
        active_ = false;
        complete_ = false;
        progress_ = 0.0f;
        elapsedS_ = 0.0f;
        durationS_ = 0.0f;
    }

} // namespace opas::motor
