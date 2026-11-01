#pragma once

// Contiguous character state snapshot structure, capture/restore operations,
// and zero-allocation binary serialization with checksum validation
// (Milestone C7 Stage C7c, docs/tasks/0026-c7-reconciliation-and-ownership.md).

#include "opas/arbiter/Arbiter.hpp"
#include "opas/math/Quat.hpp"
#include "opas/math/Transform.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/motor/Ragdoll.hpp"
#include "opas/physics/PhysicsWorld.hpp"
#include "opas/reconcile/CapsuleReconciler.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace opas::reconcile
{

    struct CharacterStateSnapshot
    {
        static constexpr std::uint32_t kMagic = 0x4F504153; // "OPAS"
        static constexpr std::uint32_t kCurrentVersion = 1;
        static constexpr std::size_t kMaxRigIdLength = 32;
        static constexpr std::size_t kMaxBodies = 64;
        static constexpr std::size_t kMaxJoints = 64;

        std::uint32_t magic = kMagic;
        std::uint32_t version = kCurrentVersion;
        std::uint32_t bodyCount = 0;
        std::uint32_t jointCount = 0;
        char rigId[kMaxRigIdLength] = {};

        math::Transform rootWorld{};
        math::Vec3 rootLinearVelocityMps{};
        math::Vec3 rootAngularVelocityRadps{};

        std::array<math::Transform, kMaxBodies> bodyTransforms{};
        std::array<math::Vec3, kMaxBodies> bodyLinearVelocitiesMps{};
        std::array<math::Vec3, kMaxBodies> bodyAngularVelocitiesRadps{};

        std::array<math::Quat, kMaxJoints> jointTargets{};
        std::array<math::Vec3, kMaxJoints> jointTargetAngularVelocitiesRadps{};

        arbiter::ArbiterState overallArbiterState = arbiter::ArbiterState::Standing;
        std::array<arbiter::ArbiterState, static_cast<std::size_t>(arbiter::BodyPart::Count)> partArbiterStates{};
        std::uint8_t pad0 = 0;
        std::array<double, static_cast<std::size_t>(arbiter::BodyPart::Count)> partStrictness{};

        math::Vec3 capsulePosition{};
        float capsuleYawRad = 0.0f;
        CapsuleMode capsuleMode = CapsuleMode::Anchored;
        std::uint8_t pad1[3] = {};

        std::uint32_t checksum = 0;
        std::uint8_t pad2[4] = {};
    };

    static_assert(std::is_trivially_copyable_v<CharacterStateSnapshot>);

    // Computes 32-bit FNV-1a checksum across snapshot payload preceding checksum field
    [[nodiscard]] std::uint32_t computeChecksum(const CharacterStateSnapshot& snapshot) noexcept;

    // Captures active character state into snapshot. Zero dynamic allocations.
    bool captureSnapshot(const motor::Ragdoll& ragdoll, const arbiter::Arbiter& arbiter,
        const CapsuleReconciler& reconciler, const physics::PhysicsWorld& world, std::string_view rigId,
        CharacterStateSnapshot& out);

    // Restores state from snapshot into active subsystems. Zero dynamic allocations.
    bool restoreSnapshot(motor::Ragdoll& ragdoll, arbiter::Arbiter& arbiter, CapsuleReconciler& reconciler,
        physics::PhysicsWorld& world, const CharacterStateSnapshot& in);

    // Zero-allocation buffer-based binary serialization
    bool serializeSnapshot(
        const CharacterStateSnapshot& snapshot, std::span<std::uint8_t> outBuffer, std::size_t& bytesWritten) noexcept;

    // Convenience vector-based binary serialization helper
    [[nodiscard]] std::vector<std::uint8_t> serializeSnapshot(const CharacterStateSnapshot& snapshot);

    // Binary deserialization with magic, version, capacity, and checksum validation
    bool deserializeSnapshot(std::span<const std::uint8_t> bytes, CharacterStateSnapshot& out) noexcept;

} // namespace opas::reconcile
