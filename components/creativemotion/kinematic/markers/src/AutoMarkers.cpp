// Automatic marker placement: docs/tasks/0011-a7-automatic-markers.md.

#include "opas/markers/AutoMarkers.hpp"

#include "opas/markers/Validation.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

namespace opas::markers {
namespace {

// How deep the "nearest surface layer" is in the snap's second pass: points
// up to this much farther along the line than the nearest still count.
constexpr double kSheetM = 0.01;

bool isFinite(math::Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

AutoAuthorResult authorUnchecked(const PoseClip& clip, const std::array<BodyMapping, 2>& bodyMaps,
                                 const AutoAuthorSettings& settings)
{
    AutoAuthorResult result;
    // Rule 1: detection with the auto profile (Low is never included).
    DetectionSettings detection = settings.detection;
    detection.includeLowConfidence = false;
    const DetectionResult detected = detectContacts(clip, bodyMaps, detection);
    if (!detected.valid) {
        result.error = "detection: " + detected.error;
        return result;
    }
    const double clipS =
        clip.sampleRateHz > 0.0 ? static_cast<double>(clip.frameCount()) / clip.sampleRateHz : 0.0;

    // Rule 2: steady contacts only.
    std::vector<std::size_t> steady; // indices into detected.contacts
    for (std::size_t i = 0; i < detected.contacts.size(); ++i) {
        const DetectedContact& c = detected.contacts[i];
        AutoContactReport entry;
        entry.id = "auto-" + c.marker.id;
        entry.confidence = c.confidence;
        entry.durationS = c.durationS;
        if (c.durationS < settings.minShareOfClip * clipS) {
            entry.reason = "intermittent: lasts less than the required share of the clip";
        } else {
            steady.push_back(i);
        }
        result.report.push_back(std::move(entry));
    }

    // One contact per limb end (review, 2026-10-18): with the wide auto
    // distance a hand can be near the chest, belly and an arm at once, and all
    // of them would stay active for the whole node. The best fit wins: highest
    // tier, then closest, then longest, then detection order.
    const auto better = [&](std::size_t a, std::size_t b) {
        const DetectedContact& x = detected.contacts[a];
        const DetectedContact& y = detected.contacts[b];
        if (x.confidence != y.confidence) {
            return x.confidence > y.confidence;
        }
        if (x.meanDistanceM != y.meanDistanceM) {
            return x.meanDistanceM < y.meanDistanceM;
        }
        if (x.durationS != y.durationS) {
            return x.durationS > y.durationS;
        }
        return a < b;
    };
    std::vector<std::size_t> chosen;
    for (const std::size_t i : steady) {
        const InteractionMarker& m = detected.contacts[i].marker;
        bool best = true;
        for (const std::size_t j : steady) {
            const InteractionMarker& o = detected.contacts[j].marker;
            if (j != i && o.sourceActor == m.sourceActor && o.sourceBone == m.sourceBone &&
                better(j, i)) {
                best = false;
                result.report[i].reason = "shadowed by " + result.report[j].id;
                break;
            }
        }
        if (best) {
            chosen.push_back(i);
        }
    }

    // Rule 3: each chosen contact must pass validation at the bodies as
    // recorded, checked in the middle of its own run (where it is actually in
    // contact, with the contacts active then), not at frame 0: validation
    // evaluates a marker at round(startS * rate) (review, 2026-10-18).
    if (!chosen.empty()) {
        MarkerSet forValidation;
        for (const std::size_t i : chosen) {
            InteractionMarker m = detected.contacts[i].marker;
            m.id = result.report[i].id;
            m.window.startS = 0.5 * (m.window.startS + m.window.endS);
            forValidation.markers.push_back(std::move(m));
        }
        ValidationSettings asRecorded;
        asRecorded.scales = {1.0};
        const ValidationReport report = validateMarkers(forValidation, clip, bodyMaps, asRecorded);
        if (!report.valid) {
            result.error = "validation: " + report.error;
            return result;
        }
        for (std::size_t k = 0; k < chosen.size(); ++k) {
            AutoContactReport& entry = result.report[chosen[k]];
            if (!report.markers[k].passed()) {
                entry.reason = "not met at the recorded bodies";
                continue;
            }
            // Kept: active from 0 for the whole node (OStim has no animation time).
            InteractionMarker m = forValidation.markers[k];
            m.window = TimeWindow{0.0, settings.windowEndS};
            result.markers.markers.push_back(std::move(m));
            entry.kept = true;
        }
    }
    result.valid = true;
    return result;
}

} // namespace

AutoAuthorResult autoAuthorMarkers(const PoseClip& clip, const std::array<BodyMapping, 2>& bodyMaps,
                                   const AutoAuthorSettings& settings)
{
    try {
        return authorUnchecked(clip, bodyMaps, settings);
    } catch (const std::exception& e) {
        // Only allocation can throw here; the contract is "never throws".
        AutoAuthorResult result;
        result.error = std::string("internal error: ") + e.what();
        return result;
    }
}

namespace {

// A7b's surface-layer rule: among points within `radius` of the line
// from + s * d (d unit) with |s| <= maxAlong, the nearest layer (smallest |s|
// plus kSheetM of depth), and in it the point closest to the line; returns
// that point's s, or nullopt when no point is near the line.
std::optional<double> nearestLayerAlong(math::Vec3 from, math::Vec3 d,
                                        std::span<const math::Vec3> points, double radius,
                                        double maxAlong)
{
    double nearestAlong = std::numeric_limits<double>::infinity();
    for (const math::Vec3& q : points) {
        if (!isFinite(q)) {
            continue;
        }
        const math::Vec3 w = q - from;
        const double along = math::dot(w, d);
        if (math::length(w - along * d) <= radius && std::abs(along) <= maxAlong) {
            nearestAlong = std::min(nearestAlong, std::abs(along));
        }
    }
    if (!std::isfinite(nearestAlong)) {
        return std::nullopt;
    }
    // Within the nearest layer, the point closest to the line, so a point at
    // the edge of the search tube cannot pull the result short of a curved
    // surface.
    double bestFromLine = std::numeric_limits<double>::infinity();
    double bestAlong = 0.0;
    for (const math::Vec3& q : points) {
        if (!isFinite(q)) {
            continue;
        }
        const math::Vec3 w = q - from;
        const double along = math::dot(w, d);
        const double fromLine = math::length(w - along * d);
        if (fromLine <= radius && std::abs(along) <= nearestAlong + kSheetM &&
            fromLine < bestFromLine) { // strict: ties keep the earlier point
            bestFromLine = fromLine;
            bestAlong = along;
        }
    }
    return bestAlong;
}

// Smooth fall-off from 1 at `full` to 0 at `none` (smoothstep), 1 below `full`.
double fade(double gap, double full, double none)
{
    if (gap <= full) {
        return 1.0;
    }
    if (gap >= none || !(none > full)) {
        return 0.0;
    }
    const double x = (gap - full) / (none - full);
    return 1.0 - x * x * (3.0 - 2.0 * x);
}

// A capsule's surface can bulge past its radius (breasts, belly, clothing):
// regions are only skipped when their capsule is this much beyond range.
constexpr double kCapsuleMarginM = 0.06;

} // namespace

SnapResult snapOffsetToSurface(math::Vec3 offsetM, math::Vec3 inwardDirection,
                               std::span<const math::Vec3> surfacePointsM,
                               const SnapSettings& settings)
{
    SnapResult result;
    result.offsetM = offsetM;
    const double length = math::length(inwardDirection);
    if (!isFinite(offsetM) || !isFinite(inwardDirection) || !(length > 0.0)) {
        return result;
    }
    const math::Vec3 d = inwardDirection / length;
    const auto along = nearestLayerAlong(offsetM, d, surfacePointsM, settings.snapRadiusM,
                                         settings.maxSnapDistanceM);
    if (!along) {
        return result; // no surface near the line
    }
    // Move along the line only: the marker keeps its place across the surface.
    result.offsetM = offsetM + (*along - settings.contactOffsetM) * d;
    result.snapped = true;
    result.movedM = math::distance(result.offsetM, offsetM);
    return result;
}

std::optional<ContactCorrection> contactCorrection(math::Vec3 limbEndM,
                                                   std::span<const SurfaceRegion> regions,
                                                   const ContactSettings& settings)
{
    if (!isFinite(limbEndM)) {
        return std::nullopt;
    }
    // How far along the line a surface may be and still matter.
    const double maxAlong = settings.noneBeyondM + settings.contactOffsetM;
    std::optional<ContactCorrection> best;
    for (std::size_t r = 0; r < regions.size(); ++r) {
        const SurfaceRegion& region = regions[r];
        const math::Vec3 ab = region.axisEndM - region.axisStartM;
        const double len2 = math::lengthSquared(ab);
        const double t =
            len2 > 0.0 ? std::clamp(math::dot(limbEndM - region.axisStartM, ab) / len2, 0.0, 1.0)
                       : 0.0;
        const math::Vec3 toAxis = region.axisStartM + t * ab - limbEndM;
        const double axisDistance = math::length(toAxis);
        if (!(axisDistance > 0.0) ||
            axisDistance - region.radiusM > settings.noneBeyondM + kCapsuleMarginM) {
            continue; // on the axis (no direction) or clearly out of range
        }
        const math::Vec3 d = toAxis / axisDistance;
        const auto gap =
            nearestLayerAlong(limbEndM, d, region.pointsM, settings.snapRadiusM, maxAlong);
        if (!gap || *gap >= settings.noneBeyondM) {
            continue;
        }
        // The nearest surface wins; inside (negative) is nearest of all.
        if (!best || *gap < best->gapM) {
            ContactCorrection c;
            c.region = r;
            c.gapM = *gap;
            c.inwardDirection = d;
            c.weight = fade(*gap, settings.fullWithinM, settings.noneBeyondM);
            c.targetM = limbEndM + c.weight * (*gap - settings.contactOffsetM) * d;
            best = c;
        }
    }
    return best;
}
} // namespace opas::markers
