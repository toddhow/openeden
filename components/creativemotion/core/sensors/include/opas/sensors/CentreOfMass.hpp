#pragma once

// The mass-weighted mean of body centres (M5a, docs/tasks/0017): one
// definition for the live sensor (BalanceSensor) and the viewer, which reads
// recordings. A body's position is its centre of mass for our centred shapes.

#include "opas/math/Vec3.hpp"

#include <cstddef>
#include <span>

namespace opas::sensors {

// massesKg[i] belongs to positionsM[i]; both spans are the same length.
// Returns the origin if the total mass is not positive.
inline math::Vec3 centreOfMass(std::span<const double> massesKg,
                               std::span<const math::Vec3> positionsM)
{
    math::Vec3 weighted;
    double totalKg = 0.0;
    for (std::size_t i = 0; i < massesKg.size() && i < positionsM.size(); ++i) {
        weighted = weighted + positionsM[i] * massesKg[i];
        totalKg += massesKg[i];
    }
    return totalKg > 0.0 ? weighted * (1.0 / totalKg) : math::Vec3{};
}

} // namespace opas::sensors
