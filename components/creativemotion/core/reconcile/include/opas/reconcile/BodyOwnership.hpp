#pragma once

// Body ownership model and controller for physics authority management
// (Milestone C7 Stage C7c, docs/tasks/0026-c7-reconciliation-and-ownership.md).

#include "opas/arbiter/Arbiter.hpp"
#include "opas/arbiter/BodyPartMap.hpp"
#include "opas/motor/Ragdoll.hpp"
#include "opas/physics/PhysicsWorld.hpp"
#include "opas/rig/Profile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace opas::reconcile {

enum class BodyOwnership : std::uint8_t {
    Kinematic,      // Driven purely by animation runtime
    Simulated,      // Driven by physical motors and dynamic simulation
    PassiveRagdoll, // Free un-motored dynamic body (stiffness/damping = 0)
    HandedOff       // Authority yielded to external host system
};

[[nodiscard]] const char* bodyOwnershipName(BodyOwnership ownership) noexcept;

class OwnershipController {
public:
    static constexpr std::size_t kMaxBodies = 64;

    OwnershipController() noexcept;
    OwnershipController(const arbiter::BodyPartMap& partMap, const rig::Profile& profile);

    [[nodiscard]] BodyOwnership bodyOwnership(std::size_t bodyIndex) const noexcept;
    [[nodiscard]] BodyOwnership partOwnership(arbiter::BodyPart part) const noexcept;

    void setBodyOwnership(std::size_t bodyIndex, BodyOwnership ownership) noexcept;
    void setPartOwnership(arbiter::BodyPart part, BodyOwnership ownership) noexcept;
    void setAllOwnership(BodyOwnership ownership) noexcept;

    // Applies ownership state to the physics world bodies and ragdoll motors without popping.
    // Zero dynamic allocations on the hot path.
    void apply(motor::Ragdoll& ragdoll, physics::PhysicsWorld& world);

private:
    std::size_t bodyCount_ = 0;
    std::array<BodyOwnership, kMaxBodies> bodyOwnerships_{};
    std::array<arbiter::BodyPart, kMaxBodies> bodyParts_{};
    std::array<BodyOwnership, static_cast<std::size_t>(arbiter::BodyPart::Count)> partOwnerships_{};
    rig::Profile profile_{};
};

} // namespace opas::reconcile
