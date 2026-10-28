#pragma once

// Inbound and outbound events for the OPAS Event Bus
// (Milestone C7 Stage C7b, docs/tasks/0026-c7-reconciliation-and-ownership.md).

#include "opas/arbiter/Arbiter.hpp"
#include "opas/behaviors/GetUpController.hpp"
#include "opas/math/Vec3.hpp"

#include <cstdint>
#include <variant>

namespace opas::reconcile {

// -----------------------------------------------------------------------------
// Inbound Game Events
// -----------------------------------------------------------------------------

struct GameHitEvent {
    math::Vec3 contactPointM{0.0, 0.0, 0.0};
    math::Vec3 direction{0.0, 1.0, 0.0};
    double impulseNs = 0.0;
    std::size_t bodyIndex = 0;
    arbiter::BodyPart bodyPart = arbiter::BodyPart::Torso;
    double damage = 0.0;
};

struct GameDamageEvent {
    arbiter::BodyPart bodyPart = arbiter::BodyPart::Torso;
    double severity = 0.0;
    bool isPermanent = false;
};

enum class TriggerType : std::uint8_t {
    Stagger,
    Trip,
    Knockdown,
    GetUp,
    Die,
    Recover
};

struct GameTriggerEvent {
    TriggerType type = TriggerType::Stagger;
    math::Vec3 direction{0.0, 1.0, 0.0};
    double intensity = 1.0;
};

using GameEventIn = std::variant<GameHitEvent, GameDamageEvent, GameTriggerEvent>;

// -----------------------------------------------------------------------------
// Outbound State Events
// -----------------------------------------------------------------------------

enum class StateEventType : std::uint8_t {
    StaggerStarted,
    StaggerRecovered,
    KnockedDown,
    SettledResting,
    GettingUpStarted,
    RecoveredUpright,
    Died,
    SurfaceGrabbed,
    CapsuleExcursionAlert
};

struct StateEventOut {
    StateEventType type = StateEventType::StaggerStarted;
    double magnitude = 0.0;
    math::Vec3 direction{};
    math::Vec3 position{};
    behaviors::RestingOrientation orientation = behaviors::RestingOrientation::Unknown;
    double durationS = 0.0;
    arbiter::BodyPart bodyPart = arbiter::BodyPart::Torso;
    bool clamped = false;

    static StateEventOut staggerStarted(double magnitude = 0.0, math::Vec3 direction = math::Vec3{}) noexcept
    {
        StateEventOut ev;
        ev.type = StateEventType::StaggerStarted;
        ev.magnitude = magnitude;
        ev.direction = direction;
        return ev;
    }

    static StateEventOut staggerRecovered() noexcept
    {
        StateEventOut ev;
        ev.type = StateEventType::StaggerRecovered;
        return ev;
    }

    static StateEventOut knockedDown(math::Vec3 direction = math::Vec3{}, double magnitude = 0.0) noexcept
    {
        StateEventOut ev;
        ev.type = StateEventType::KnockedDown;
        ev.direction = direction;
        ev.magnitude = magnitude;
        return ev;
    }

    static StateEventOut settledResting(behaviors::RestingOrientation orientation, double durationS) noexcept
    {
        StateEventOut ev;
        ev.type = StateEventType::SettledResting;
        ev.orientation = orientation;
        ev.durationS = durationS;
        return ev;
    }

    static StateEventOut gettingUpStarted(math::Vec3 recoveryDirection = math::Vec3{}) noexcept
    {
        StateEventOut ev;
        ev.type = StateEventType::GettingUpStarted;
        ev.direction = recoveryDirection;
        return ev;
    }

    static StateEventOut recoveredUpright() noexcept
    {
        StateEventOut ev;
        ev.type = StateEventType::RecoveredUpright;
        return ev;
    }

    static StateEventOut died(arbiter::BodyPart part = arbiter::BodyPart::Torso) noexcept
    {
        StateEventOut ev;
        ev.type = StateEventType::Died;
        ev.bodyPart = part;
        return ev;
    }

    static StateEventOut surfaceGrabbed(math::Vec3 contactLocationM, arbiter::BodyPart handSide) noexcept
    {
        StateEventOut ev;
        ev.type = StateEventType::SurfaceGrabbed;
        ev.position = contactLocationM;
        ev.bodyPart = handSide;
        return ev;
    }

    static StateEventOut capsuleExcursionAlert(double excursionM, bool clamped = false) noexcept
    {
        StateEventOut ev;
        ev.type = StateEventType::CapsuleExcursionAlert;
        ev.magnitude = excursionM;
        ev.clamped = clamped;
        return ev;
    }
};

} // namespace opas::reconcile
