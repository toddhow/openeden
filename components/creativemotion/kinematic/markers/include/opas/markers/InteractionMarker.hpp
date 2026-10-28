#pragma once

// Interaction markers: authored contact points for a paired animation, e.g.
// "actor A's right hand touches actor B's left shoulder between 1.2 s and
// 2.0 s". The alignment solver (A2) moves actors and limbs to satisfy them.

#include "opas/math/Vec3.hpp"

#include <string>
#include <vector>

namespace opas::markers {

struct TimeWindow {
    double startS = 0.0; // >= 0
    double endS = 0.0;   // >= startS
    friend bool operator==(const TimeWindow&, const TimeWindow&) = default;
};

struct InteractionMarker {
    std::string id;           // unique within a MarkerSet, non-empty
    int sourceActor = 0;      // index of the actor whose sourceBone makes contact (>= 0)
    int targetActor = 1;      // index of the actor owning targetRegion (>= 0, != sourceActor)
    std::string sourceBone;   // bone on the acting actor that makes contact
    std::string targetRegion; // named body region on the other actor (see BodyMeasurements)
    math::Vec3 offsetM;       // contact point relative to the target region's frame
    math::Vec3 contactDirection{0.0, 0.0, 1.0}; // unit; direction the source approaches along
    TimeWindow window;                          // when the contact applies, in clip time
    int priority = 0;                           // higher wins when markers conflict
    double toleranceM = 0.02;                   // > 0; how close counts as "met"

    friend bool operator==(const InteractionMarker&, const InteractionMarker&) = default;
};

struct MarkerSet {
    static constexpr int kFormatVersion = 2; // 2 added sourceActor/targetActor (A2)
    std::vector<InteractionMarker> markers;
    friend bool operator==(const MarkerSet&, const MarkerSet&) = default;
};

} // namespace opas::markers
