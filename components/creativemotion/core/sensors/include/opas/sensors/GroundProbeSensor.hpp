#pragma once

// Ground probe sensor measuring clearance and slope under each foot
// (Stage C2a, docs/tasks/0025-c2-remaining-behaviours-and-sensors.md).
//
// Casts downward rays from feet via PhysicsWorld::castRay. Allocates nothing
// during updates.

#include "opas/math/Vec3.hpp"
#include "opas/motor/Ragdoll.hpp"
#include "opas/physics/PhysicsWorld.hpp"

#include <cstddef>
#include <vector>

namespace opas::sensors
{

    struct FootGroundReading
    {
        bool hit = false;
        math::Vec3 contactPointM{};
        math::Vec3 surfaceNormal{ 0.0, 0.0, 1.0 }; // unit normal pointing up/away from ground
        double clearanceM = 0.0; // distance along probe ray from foot to ground
        double slopeRad = 0.0; // angle of incline: std::acos(surfaceNormal.z)
    };

    class GroundProbeSensor
    {
    public:
        struct Config
        {
            double probeDistanceM = 1.0; // ray length downwards from foot
            math::Vec3 rayDirection{ 0.0, 0.0, -1.0 };
        };

        GroundProbeSensor(std::vector<std::size_t> footBodies, Config config = {});

        // Updates ground probes using the physics world. Ignores foot bodies during ray casts.
        // Zero heap allocations.
        void update(const physics::PhysicsWorld& world, const motor::Ragdoll& ragdoll);

        [[nodiscard]] std::size_t footCount() const noexcept { return footBodies_.size(); }
        [[nodiscard]] const FootGroundReading& reading(std::size_t footIndex) const;
        [[nodiscard]] bool allGrounded() const noexcept;
        [[nodiscard]] bool anyGrounded() const noexcept;
        [[nodiscard]] double minClearanceM() const noexcept;
        [[nodiscard]] double averageSlopeRad() const noexcept;

    private:
        std::vector<std::size_t> footBodies_;
        Config config_;
        std::vector<FootGroundReading> readings_;
    };

} // namespace opas::sensors
