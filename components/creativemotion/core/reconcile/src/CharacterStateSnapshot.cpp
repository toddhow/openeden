#include "opas/reconcile/CharacterStateSnapshot.hpp"

#include <algorithm>
#include <cstring>

namespace opas::reconcile
{

    namespace
    {

        [[nodiscard]] inline math::Vec3 toMath(physics::Vec3 v) noexcept
        {
            return math::Vec3{ static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z) };
        }

        [[nodiscard]] inline math::Quat toMath(physics::Quat q) noexcept
        {
            return math::Quat{ static_cast<double>(q.x), static_cast<double>(q.y), static_cast<double>(q.z),
                static_cast<double>(q.w) };
        }

        [[nodiscard]] inline physics::Vec3 toPhysics(math::Vec3 v) noexcept
        {
            return physics::Vec3{ static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
        }

        [[nodiscard]] inline physics::Quat toPhysics(math::Quat q) noexcept
        {
            return physics::Quat{ static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z),
                static_cast<float>(q.w) };
        }

    } // namespace

    std::uint32_t computeChecksum(const CharacterStateSnapshot& snapshot) noexcept
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&snapshot);
        constexpr std::size_t len = offsetof(CharacterStateSnapshot, checksum);
        std::uint32_t hash = 2166136261u;
        for (std::size_t i = 0; i < len; ++i)
        {
            hash ^= bytes[i];
            hash *= 16777619u;
        }
        return hash;
    }

    bool captureSnapshot(const motor::Ragdoll& ragdoll, const arbiter::Arbiter& arbiter,
        const CapsuleReconciler& reconciler, const physics::PhysicsWorld& world, std::string_view rigId,
        CharacterStateSnapshot& out)
    {
        if (ragdoll.bodies.size() > CharacterStateSnapshot::kMaxBodies
            || ragdoll.joints.size() > CharacterStateSnapshot::kMaxJoints)
        {
            return false;
        }

        std::memset(&out, 0, sizeof(CharacterStateSnapshot));
        out.magic = CharacterStateSnapshot::kMagic;
        out.version = CharacterStateSnapshot::kCurrentVersion;
        out.bodyCount = static_cast<std::uint32_t>(ragdoll.bodies.size());
        out.jointCount = static_cast<std::uint32_t>(ragdoll.joints.size());

        const std::size_t copyLen = std::min(rigId.size(), CharacterStateSnapshot::kMaxRigIdLength - 1);
        std::memcpy(out.rigId, rigId.data(), copyLen);
        out.rigId[copyLen] = '\0';

        out.rootWorld = ragdoll.rootWorld;

        for (std::size_t i = 0; i < ragdoll.bodies.size(); ++i)
        {
            physics::BodyState bState{};
            if (world.readBody(ragdoll.bodies[i].id, bState))
            {
                out.bodyTransforms[i] = math::Transform{ toMath(bState.positionM), toMath(bState.rotation) };
                out.bodyLinearVelocitiesMps[i] = toMath(bState.linearVelocityMps);
                out.bodyAngularVelocitiesRadps[i] = toMath(bState.angularVelocityRadps);
            }
        }

        if (!ragdoll.bodies.empty() && ragdoll.rootBody < ragdoll.bodies.size())
        {
            out.rootLinearVelocityMps = out.bodyLinearVelocitiesMps[ragdoll.rootBody];
            out.rootAngularVelocityRadps = out.bodyAngularVelocitiesRadps[ragdoll.rootBody];
        }

        for (std::size_t j = 0; j < ragdoll.joints.size(); ++j)
        {
            if (ragdoll.hasLastJointTargets && j < ragdoll.lastJointTargets.size())
            {
                out.jointTargets[j] = ragdoll.lastJointTargets[j];
            }
            else
            {
                out.jointTargets[j] = math::Quat{ 0.0, 0.0, 0.0, 1.0 };
            }
        }

        out.overallArbiterState = arbiter.overallState();
        for (std::size_t p = 0; p < static_cast<std::size_t>(arbiter::BodyPart::Count); ++p)
        {
            const auto part = static_cast<arbiter::BodyPart>(p);
            out.partArbiterStates[p] = arbiter.partState(part);
            out.partStrictness[p] = arbiter.partStrictness(part);
        }

        out.capsulePosition = reconciler.position();
        out.capsuleYawRad = reconciler.yawRad();
        out.capsuleMode = reconciler.mode();

        out.checksum = computeChecksum(out);
        return true;
    }

    bool restoreSnapshot(motor::Ragdoll& ragdoll, arbiter::Arbiter& arbiter, CapsuleReconciler& reconciler,
        physics::PhysicsWorld& world, const CharacterStateSnapshot& in)
    {
        if (in.magic != CharacterStateSnapshot::kMagic)
        {
            return false;
        }
        if (in.version != CharacterStateSnapshot::kCurrentVersion)
        {
            return false;
        }
        if (in.bodyCount != ragdoll.bodies.size() || in.jointCount != ragdoll.joints.size())
        {
            return false;
        }

        ragdoll.rootWorld = in.rootWorld;

        for (std::size_t i = 0; i < ragdoll.bodies.size(); ++i)
        {
            world.setVelocity(ragdoll.bodies[i].id, toPhysics(in.bodyLinearVelocitiesMps[i]),
                toPhysics(in.bodyAngularVelocitiesRadps[i]));
        }

        for (std::size_t j = 0; j < ragdoll.joints.size(); ++j)
        {
            if (j < ragdoll.lastJointTargets.size())
            {
                ragdoll.lastJointTargets[j] = in.jointTargets[j];
            }
            world.setJointTarget(ragdoll.joints[j].id, toPhysics(in.jointTargets[j]),
                toPhysics(in.jointTargetAngularVelocitiesRadps[j]));
        }
        ragdoll.hasLastJointTargets = true;

        for (std::size_t p = 0; p < static_cast<std::size_t>(arbiter::BodyPart::Count); ++p)
        {
            const auto part = static_cast<arbiter::BodyPart>(p);
            arbiter.setPartStrictness(part, in.partStrictness[p]);
        }

        reconciler.setPosition(in.capsulePosition);
        reconciler.setYaw(in.capsuleYawRad);
        reconciler.setMode(in.capsuleMode);

        return true;
    }

    bool serializeSnapshot(
        const CharacterStateSnapshot& snapshot, std::span<std::uint8_t> outBuffer, std::size_t& bytesWritten) noexcept
    {
        if (outBuffer.size() < sizeof(CharacterStateSnapshot))
        {
            return false;
        }
        std::memcpy(outBuffer.data(), &snapshot, sizeof(CharacterStateSnapshot));
        bytesWritten = sizeof(CharacterStateSnapshot);
        return true;
    }

    std::vector<std::uint8_t> serializeSnapshot(const CharacterStateSnapshot& snapshot)
    {
        std::vector<std::uint8_t> bytes(sizeof(CharacterStateSnapshot));
        std::size_t written = 0;
        serializeSnapshot(snapshot, bytes, written);
        return bytes;
    }

    bool deserializeSnapshot(std::span<const std::uint8_t> bytes, CharacterStateSnapshot& out) noexcept
    {
        if (bytes.size() != sizeof(CharacterStateSnapshot))
        {
            return false;
        }

        CharacterStateSnapshot temp{};
        std::memcpy(&temp, bytes.data(), sizeof(CharacterStateSnapshot));

        if (temp.magic != CharacterStateSnapshot::kMagic)
        {
            return false;
        }
        if (temp.version != CharacterStateSnapshot::kCurrentVersion)
        {
            return false;
        }
        if (temp.bodyCount > CharacterStateSnapshot::kMaxBodies || temp.jointCount > CharacterStateSnapshot::kMaxJoints)
        {
            return false;
        }

        if (temp.checksum != computeChecksum(temp))
        {
            return false;
        }

        out = temp;
        return true;
    }

} // namespace opas::reconcile
