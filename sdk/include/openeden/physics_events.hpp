#pragma once

#include <openeden/event_bus.hpp>
#include <openeden/physics.hpp>

namespace openeden::sdk
{
    /**
     * @brief Physics events plugins can subscribe to on the event bus (decision 0003).
     *
     * The core delivers them at the start of Phase::Input, on the main thread, never on a physics
     * worker thread. Bodies in an event may already be gone: check them with
     * PhysicsQueryService::isBodyValid.
     */

    /// A moving body such as a projectile struck another body.
    struct PhysicsHitEvent
    {
        static constexpr EventId kEventId = 0x50480001;

        BodyId body{};
        BodyId other{};
        PhysicsVec3 pointM{};
        PhysicsVec3 normal{};
    };

    /// Two bodies started touching.
    struct PhysicsContactBeginEvent
    {
        static constexpr EventId kEventId = 0x50480002;

        BodyId bodyA{};
        BodyId bodyB{};
        PhysicsVec3 pointM{};
        PhysicsVec3 normal{};
    };

    /// Two bodies stopped touching.
    struct PhysicsContactEndEvent
    {
        static constexpr EventId kEventId = 0x50480003;

        BodyId bodyA{};
        BodyId bodyB{};
    };
} // namespace openeden::sdk
