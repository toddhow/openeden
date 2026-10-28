// Marker validation: rules in docs/tasks/0007-a4b-marker-validation.md.

#include "opas/markers/Validation.hpp"

#include "opas/markers/Alignment.hpp"
#include "opas/rig/Skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>

namespace opas::markers {
namespace {

// Rule 3: a flat skeleton, one root bone per name; world transforms serve as
// local poses under an identity root.
rig::Skeleton flatSkeleton(const std::vector<std::string>& names)
{
    rig::Skeleton skeleton;
    skeleton.bones.reserve(names.size());
    for (const std::string& name : names) {
        skeleton.bones.push_back(rig::Bone{name, -1, {}});
    }
    return skeleton;
}

std::string validateInputs(const PoseClip& clip, const ValidationSettings& settings)
{
    if (settings.scales.empty()) {
        return "settings: scales must not be empty";
    }
    for (const double scale : settings.scales) {
        if (!(scale > 0.0) || !std::isfinite(scale)) {
            return "settings: every scale must be finite and > 0";
        }
    }
    if (!(clip.sampleRateHz > 0.0) || !std::isfinite(clip.sampleRateHz)) {
        return "clip: sampleRateHz must be finite and > 0";
    }
    if (clip.frameCount() == 0 || clip.actors[1].frames.size() != clip.frameCount()) {
        return "clip: both actors need the same, non-zero number of frames";
    }
    return {};
}

// Rule 3: the frame for a marker, round(startS * rate) clamped to the clip.
std::size_t frameFor(const InteractionMarker& marker, const PoseClip& clip)
{
    const double exact = std::round(marker.window.startS * clip.sampleRateHz);
    const double last = static_cast<double>(clip.frameCount() - 1);
    // NaN uses frame 0; +/-infinity clamp to the last / first frame like any
    // other out-of-range time.
    const double clamped = std::isnan(exact) ? 0.0 : std::clamp(exact, 0.0, last);
    return static_cast<std::size_t>(clamped);
}

ValidationReport validateUnchecked(const MarkerSet& markers, const PoseClip& clip,
                                   const std::array<BodyMapping, 2>& bodyMaps,
                                   const ValidationSettings& settings)
{
    ValidationReport report;
    if (std::string error = validateInputs(clip, settings); !error.empty()) {
        report.error = std::move(error);
        return report;
    }

    // Root alignment only: IK chains are removed (A4c extends this).
    std::array<BodyMapping, 2> maps = bodyMaps;
    for (BodyMapping& map : maps) {
        map.ikChains.clear();
    }
    const std::array<rig::Skeleton, 2> skeletons{flatSkeleton(clip.actors[0].bones),
                                                 flatSkeleton(clip.actors[1].bones)};

    for (const InteractionMarker& marker : markers.markers) {
        MarkerValidation result;
        result.id = marker.id;
        const std::size_t f = frameFor(marker, clip);
        for (const double s0 : settings.scales) {
            for (const double s1 : settings.scales) {
                const std::vector<math::Transform> pose0 =
                    scaleActorPose(clip.actors[0].frames[f], s0);
                const std::vector<math::Transform> pose1 =
                    scaleActorPose(clip.actors[1].frames[f], s1);
                const ActorInput actor0{skeletons[0], maps[0], pose0, {}, 1.0};
                const ActorInput actor1{skeletons[1], maps[1], pose1, {}, 1.0};
                const AlignmentResult aligned =
                    alignPair(actor0, actor1, markers, marker.window.startS);

                FailedPairing pairing{s0, s1, 0.0, false, {}};
                if (!aligned.valid) {
                    pairing.error = aligned.error;
                    result.failures.push_back(std::move(pairing));
                    continue;
                }
                const auto own =
                    std::find_if(aligned.markers.begin(), aligned.markers.end(),
                                 [&marker](const MarkerResidual& r) { return r.id == marker.id; });
                if (own == aligned.markers.end()) {
                    pairing.error = "marker is not active at its own window start";
                    result.failures.push_back(std::move(pairing));
                    continue;
                }
                result.worstResidualM = std::max(result.worstResidualM, own->distanceM);
                if (!own->met || aligned.rootShiftClamped) {
                    pairing.residualM = own->distanceM;
                    pairing.clamped = aligned.rootShiftClamped;
                    result.failures.push_back(std::move(pairing));
                }
            }
        }
        report.markers.push_back(std::move(result));
    }
    report.valid = true;
    return report;
}

} // namespace

std::vector<math::Transform> scaleActorPose(std::span<const math::Transform> frame, double scale)
{
    std::vector<math::Transform> out(frame.begin(), frame.end());
    if (frame.empty()) {
        return out;
    }
    double lowest = frame[0].translation.z;
    for (const math::Transform& t : frame) {
        lowest = std::min(lowest, t.translation.z);
    }
    const math::Vec3 ground{frame[0].translation.x, frame[0].translation.y, lowest};
    for (math::Transform& t : out) {
        t.translation = ground + scale * (t.translation - ground);
    }
    return out;
}

bool ValidationReport::allPassed() const
{
    return valid && std::all_of(markers.begin(), markers.end(),
                                [](const MarkerValidation& m) { return m.passed(); });
}

ValidationReport validateMarkers(const MarkerSet& markers, const PoseClip& clip,
                                 const std::array<BodyMapping, 2>& bodyMaps,
                                 const ValidationSettings& settings)
{
    try {
        return validateUnchecked(markers, clip, bodyMaps, settings);
    } catch (const std::exception& e) {
        // Only allocation can throw here; the contract is "never throws".
        ValidationReport report;
        report.error = std::string("internal error: ") + e.what();
        return report;
    }
}

} // namespace opas::markers
