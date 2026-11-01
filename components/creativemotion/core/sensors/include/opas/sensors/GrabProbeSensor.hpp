#pragma once

// Grab probe sensor detecting reachable solid obstacles and surfaces
// (Stage C2a, docs/tasks/0025-c2-remaining-behaviours-and-sensors.md).
//
// Casts spatial rays from hand bodies into the physics world. Allocates nothing
// during updates.

#include "opas/arbiter/Arbiter.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/motor/Ragdoll.hpp"
#include "opas/physics/PhysicsWorld.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace opas::sensors
{

    struct GrabTarget
    {
        math::Vec3 positionM{};
        math::Vec3 surfaceNormal{ 0.0, 0.0, 1.0 };
        double distanceM = 0.0;
        std::size_t handBodyIndex = 0;
        arbiter::BodyPart handPart = arbiter::BodyPart::RightArm;
    };

    class GrabProbeSensor
    {
    public:
        struct HandProbe
        {
            std::size_t bodyIndex = 0;
            arbiter::BodyPart part = arbiter::BodyPart::RightArm;
            math::Vec3 localRayDirection{ 1.0, 0.0, 0.0 }; // ray direction in hand body local frame
            double maxReachM = 0.55;
        };

        explicit GrabProbeSensor(std::vector<HandProbe> probes);

        // Casts rays from hand bodies into world, ignoring ragdoll hand bodies.
        // Zero heap allocations.
        void update(const physics::PhysicsWorld& world, const motor::Ragdoll& ragdoll);

        [[nodiscard]] bool hasTarget() const noexcept;
        [[nodiscard]] std::optional<GrabTarget> bestTarget() const noexcept; // closest reachable target
        [[nodiscard]] std::optional<GrabTarget> targetForHand(arbiter::BodyPart handPart) const noexcept;

    private:
        std::vector<HandProbe> probes_;
        std::vector<std::optional<GrabTarget>> targets_;
    };

} // namespace opas::sensors
