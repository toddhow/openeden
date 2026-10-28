#pragma once

// Marker validation (A4b): which markers cannot be met when the actors have
// different body sizes. Rules: docs/tasks/0007-a4b-marker-validation.md.

#include "opas/markers/BodyMeasurements.hpp"
#include "opas/markers/InteractionMarker.hpp"
#include "opas/markers/PoseClip.hpp"
#include "opas/math/Transform.hpp"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace opas::markers {

// Rule 1: one actor's bone world transforms scaled about its ground point
// (x, y of the first bone; lowest z of all bones). Rotations are unchanged.
std::vector<math::Transform> scaleActorPose(std::span<const math::Transform> frame, double scale);

struct ValidationSettings {
    std::vector<double> scales{0.8, 1.0, 1.25}; // every ordered pair is tried
};

struct FailedPairing {
    double scale0 = 1.0;
    double scale1 = 1.0;
    double residualM = 0.0; // the marker's residual; 0 when alignPair was invalid
    bool clamped = false;   // alignPair clamped the root shift
    std::string error;      // alignPair's error when it returned valid == false
};

struct MarkerValidation {
    std::string id;
    double worstResidualM = 0.0;
    std::vector<FailedPairing> failures; // sweep order; empty = the marker passes

    [[nodiscard]] bool passed() const { return failures.empty(); }
};

struct ValidationReport {
    bool valid = false;
    std::string error;                     // set when valid is false
    std::vector<MarkerValidation> markers; // MarkerSet order

    [[nodiscard]] bool allPassed() const;
};

// Validates every marker against the clip at the sweep's body-size pairings.
// `bodyMaps[i]` describes actor i (IK chains are ignored; root alignment
// only). Deterministic; never throws.
ValidationReport validateMarkers(const MarkerSet& markers, const PoseClip& clip,
                                 const std::array<BodyMapping, 2>& bodyMaps,
                                 const ValidationSettings& settings = {});

} // namespace opas::markers
