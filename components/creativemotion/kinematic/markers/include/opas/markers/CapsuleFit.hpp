#pragma once

// Capsule radii fitted to points sampled on an actor's skin (A5a,
// docs/tasks/0009-a5-body-surfaces-fingers.md). The result is a copy of the
// body map whose radiusRatio values match this actor's body, so the
// alignment solver uses it unchanged.

#include "opas/markers/BodyMeasurements.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/rig/Skeleton.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opas::markers {

// A point on the skin in the skeleton's model space (the same space as
// rig::computeWorldRest), with the index of the bone that weighs it most.
struct SurfacePoint {
    math::Vec3 positionM;
    int bone = -1; // index into Skeleton::bones
};

struct CapsuleFitSettings {
    // Spec decision 1 (toddhow, 2026-10-17): the radius is this percentile of
    // the point distances, so a few stray vertices do not inflate it.
    double percentile = 0.9; // in (0, 1]
    // Fewer usable points than this keep the rule's own radiusRatio.
    std::size_t minPoints = 20; // >= 1
};

struct CapsuleFitResult {
    std::optional<BodyMapping> value;
    std::string error;                   // set when value is empty
    std::vector<std::size_t> pointsUsed; // per capsule rule, same order
    std::vector<bool> fitted;            // per capsule rule: true if the radius was fitted
};

// For each capsule rule, the points whose bone is the rule's `bone` and
// that project onto the segment from `bone` to `endBone` (rest positions)
// are used; the radius is the nearest-rank percentile of their distances to
// the segment, and radiusRatio = radius / segment length. The skeleton must
// pass rig::validateSkeleton and name every capsule bone; settings must be
// in range; points must be finite. Deterministic; never throws.
CapsuleFitResult fitCapsules(const rig::Skeleton& skeleton, const BodyMapping& mapping,
                             std::span<const SurfacePoint> points,
                             const CapsuleFitSettings& settings = {});

} // namespace opas::markers
