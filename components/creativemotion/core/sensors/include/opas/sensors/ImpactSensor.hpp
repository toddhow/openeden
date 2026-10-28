#pragma once

// Impact sensor recording sensory contact and collision impulse events
// (Stage C2a, docs/tasks/0025-c2-remaining-behaviours-and-sensors.md).
//
// Maintains a preallocated bounded ring buffer of impact events with zero heap
// allocation during updates.

#include "opas/arbiter/Arbiter.hpp"
#include "opas/math/Vec3.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace opas::sensors {

struct ImpactEvent {
    arbiter::BodyPart bodyPart = arbiter::BodyPart::Torso;
    std::size_t bodyIndex = 0;
    math::Vec3 contactPointM{};
    math::Vec3 normal{0.0, 0.0, 1.0}; // unit normal pointing away from struck body or impact surface
    math::Vec3 impulseNs{};           // applied impulse vector in N*s
    double energyJ = 0.0;             // kinetic energy / severity metric
    double timeS = 0.0;               // timestamp in seconds
};

class ImpactSensor {
public:
    explicit ImpactSensor(std::size_t maxEvents = 16);

    // Records a new impact event into the buffer, overwriting the oldest event
    // if capacity is reached. Allocates nothing.
    void recordImpact(const ImpactEvent& event);

    [[nodiscard]] bool hasImpacts() const noexcept { return count_ > 0; }
    [[nodiscard]] std::size_t impactCount() const noexcept { return count_; }
    [[nodiscard]] std::span<const ImpactEvent> impacts() const noexcept;
    [[nodiscard]] std::optional<ImpactEvent> latestImpact() const noexcept;
    [[nodiscard]] std::optional<ImpactEvent> latestPartImpact(arbiter::BodyPart part) const noexcept;
    [[nodiscard]] double totalImpulseMagnitude() const noexcept;
    void clear() noexcept;

private:
    std::size_t capacity_ = 16;
    std::size_t count_ = 0;
    std::vector<ImpactEvent> ordered_;
};

} // namespace opas::sensors
