#pragma once

// Automatic marker placement (A7, docs/tasks/0011-a7-automatic-markers.md):
// authoring markers from a recorded clip (A7a) and adapting a marker's offset
// to the real surface of the actor it touches (A7b).

#include "opas/markers/BodyMeasurements.hpp"
#include "opas/markers/ContactDetection.hpp"
#include "opas/markers/InteractionMarker.hpp"
#include "opas/markers/PoseClip.hpp"
#include "opas/math/Vec3.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opas::markers {

struct AutoAuthorSettings {
    // A7 decision 1 (toddhow, 2026-10-18): wider than the authoring defaults,
    // because in-game bodies differ from the ones the animation was made for.
    DetectionSettings detection{0.08, 0.5, 0.2, 0.02, false};
    // A7a rule 2: a contact must last at least this share of the clip, since
    // auto markers are active for the whole node (OStim has no animation time).
    double minShareOfClip = 0.5;
    // The kept markers' window: from 0 for as long as the node plays.
    double windowEndS = 3600.0;
};

struct AutoContactReport {
    std::string id; // the auto- id the marker has (or would have had)
    Confidence confidence = Confidence::Low;
    double durationS = 0.0;
    bool kept = false;
    std::string reason; // why it was dropped; empty when kept
};

struct AutoAuthorResult {
    bool valid = false;
    std::string error;                     // set when valid is false
    MarkerSet markers;                     // the kept markers, ids "auto-..."
    std::vector<AutoContactReport> report; // every detected contact, detection order
};

// A7a: detect contacts with the auto profile (High and Medium kept), keep
// steady ones (rule 2) and the best one per limb end (tier, distance,
// duration), validate each at the 1.0/1.0 pairing in the middle of its own
// run, and keep those that pass with windows [0, windowEndS]. Deterministic; never throws.
AutoAuthorResult autoAuthorMarkers(const PoseClip& clip, const std::array<BodyMapping, 2>& bodyMaps,
                                   const AutoAuthorSettings& settings = {});

struct SnapSettings {
    // A7 decision 3 (toddhow, 2026-10-18).
    double snapRadiusM = 0.03;    // how far from the line a surface point may be
    double contactOffsetM = 0.02; // the hand bone sits this far inside the palm
    // Surface farther than this along the line is ignored, so a missing front
    // layer cannot pull the marker through the body to its back (review,
    // 2026-10-18). Auto offsets can sit up to 8 cm from the recorded body (the
    // auto distance) plus a few centimetres for a slimmer live body; 12 cm still
    // stops a jump through a limb (8 cm out plus ~10 cm of limb) or the torso.
    double maxSnapDistanceM = 0.12;
};

struct SnapResult {
    math::Vec3 offsetM;   // the adapted offset (unchanged when not snapped)
    bool snapped = false; // a surface point was found near the line
    double movedM = 0.0;  // how far the offset moved
};

// A7b: `offsetM` and `inwardDirection` (the marker's contactDirection) and the
// surface points are all in the target region bone's frame. Points within
// snapRadiusM of the line offsetM + s * direction count. The nearest surface
// layer along the line is found (the smallest |s| up to maxSnapDistanceM, plus
// 1 cm of depth), and in
// it the point closest to the line; the offset then moves along the line only,
// to that point's s minus contactOffsetM, so it keeps its place across the
// surface. A zero or non-finite direction, or no point near the line, leaves
// the offset unchanged. Deterministic; never throws.
SnapResult snapOffsetToSurface(math::Vec3 offsetM, math::Vec3 inwardDirection,
                               std::span<const math::Vec3> surfacePointsM,
                               const SnapSettings& settings = {});

// --- A7 v2: per-frame contact correction (spec section "A7 v2") -------------

// One region of the partner's body this frame, in world meters: its capsule
// axis and radius, and its surface points (moved with their bones).
struct SurfaceRegion {
    std::string region;
    math::Vec3 axisStartM;
    math::Vec3 axisEndM;
    double radiusM = 0.0;
    std::vector<math::Vec3> pointsM;
};

struct ContactSettings {
    // A7 v2 decision 3 (toddhow, 2026-10-18): full correction within 4 cm of the
    // surface, none from 10 cm.
    double fullWithinM = 0.04;
    double noneBeyondM = 0.10;
    // As SnapSettings: the hand bone sits this far inside the palm, and surface
    // points this close to the line count.
    double contactOffsetM = 0.02;
    double snapRadiusM = 0.03;
};

struct ContactCorrection {
    std::size_t region = 0;     // index into the regions passed in
    double gapM = 0.0;          // signed distance to the surface along the direction; < 0 inside
    double weight = 0.0;        // 1 within fullWithinM (and inside), 0 at noneBeyondM
    math::Vec3 inwardDirection; // unit, from the limb end toward the region's axis
    math::Vec3 targetM;         // limb end + weight * (gap - contactOffset) * direction
};

// A7 v2: for a limb end at `limbEndM` (its animated position this frame),
// the region whose surface is nearest along the direction to that region's
// axis (inside the body counts as nearest), and the depth-corrected target.
// Regions whose capsule is clearly beyond noneBeyondM, or with no surface on
// the line, are skipped. nullopt when no region is within noneBeyondM.
// Deterministic; never throws.
std::optional<ContactCorrection> contactCorrection(math::Vec3 limbEndM,
                                                   std::span<const SurfaceRegion> regions,
                                                   const ContactSettings& settings = {});

} // namespace opas::markers
