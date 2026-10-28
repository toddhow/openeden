// Contact detection: the numbered rules in docs/tasks/0006-a4a-contact-detection.md.

#include "opas/markers/ContactDetection.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace opas::markers {
namespace {

using math::Quat;
using math::Transform;
using math::Vec3;

bool isFinite(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Below this length the mean contact normal counts as zero (rule 7 fallback).
constexpr double kZeroDirection = 1e-12;

// Rule 7: letters, digits, '-', '_' and '.' are kept; anything else is '_'.
std::string sanitized(const std::string& text)
{
    std::string out = text;
    for (char& c : out) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) {
            c = '_';
        }
    }
    return out;
}

// Closest point to p on segment [a, b] (a point segment gives a).
Vec3 closestOnSegment(Vec3 p, Vec3 a, Vec3 b)
{
    const Vec3 ab = b - a;
    const double len2 = math::lengthSquared(ab);
    if (!(len2 > 0.0)) {
        return a;
    }
    const double t = std::clamp(math::dot(p - a, ab) / len2, 0.0, 1.0);
    return a + t * ab;
}

std::optional<std::size_t> indexOf(const ClipActor& actor, const std::string& bone)
{
    const auto it = std::find(actor.bones.begin(), actor.bones.end(), bone);
    if (it == actor.bones.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - actor.bones.begin());
}

std::string validate(const PoseClip& clip, const DetectionSettings& s)
{
    const auto positiveFinite = [](double v) { return v > 0.0 && std::isfinite(v); };
    if (!positiveFinite(s.distanceThresholdM) || !positiveFinite(s.minDurationS) ||
        !positiveFinite(s.maxRelativeSpeedMps) || !positiveFinite(s.markerToleranceM)) {
        return "detection settings: every threshold must be finite and > 0";
    }
    if (!positiveFinite(clip.sampleRateHz)) {
        return "clip: sampleRateHz must be finite and > 0";
    }
    for (std::size_t a = 0; a < 2; ++a) {
        const ClipActor& actor = clip.actors[a];
        if (actor.frames.size() != clip.frameCount()) {
            return "clip: actor " + std::to_string(a) + " has a different number of frames";
        }
        for (const auto& frame : actor.frames) {
            if (frame.size() != actor.bones.size()) {
                return "clip: actor " + std::to_string(a) +
                       " has a frame whose size differs from its bone list";
            }
        }
    }
    return {};
}

// One candidate (source bone of s, region of t) evaluated over the clip.
struct Candidate {
    int source = 0;
    int target = 1;
    std::string bone;
    std::size_t boneIndex = 0;
    const CapsuleRule* region = nullptr;
    std::size_t regionBone = 0;
    std::size_t regionEnd = 0;
};

struct FrameSample {
    double distance = 0.0; // surface distance d
    Vec3 local;            // p in the region frame
    Vec3 inward;           // -n in the region frame (zero if p is on the axis)
};

std::vector<FrameSample> sample(const PoseClip& clip, const Candidate& c)
{
    const ClipActor& src = clip.actors[static_cast<std::size_t>(c.source)];
    const ClipActor& tgt = clip.actors[static_cast<std::size_t>(c.target)];
    std::vector<FrameSample> out(clip.frameCount());
    for (std::size_t f = 0; f < out.size(); ++f) {
        const Vec3 p = src.frames[f][c.boneIndex].translation;
        const Transform& regionFrame = tgt.frames[f][c.regionBone];
        const Vec3 a = regionFrame.translation;
        const Vec3 b = tgt.frames[f][c.regionEnd].translation;
        const double radius = c.region->radiusRatio * math::distance(a, b);
        const Vec3 closest = closestOnSegment(p, a, b);
        const Vec3 outward = p - closest;
        out[f].distance = math::length(outward) - radius;
        // A frame with any non-finite input is never a contact frame: NaN
        // fails `d <= threshold`, so the run ends there (review amendment).
        const Quat& q = regionFrame.rotation;
        if (!isFinite(p) || !isFinite(a) || !isFinite(b) || !std::isfinite(q.x) ||
            !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) {
            out[f].distance = std::numeric_limits<double>::quiet_NaN();
        }
        out[f].local = math::transformPoint(math::inverse(regionFrame), p);
        const Vec3 n = math::normalized(outward); // zero if p is on the axis
        out[f].inward = math::rotate(math::conjugate(regionFrame.rotation), -n);
    }
    return out;
}

// Rule 4: relative speed per frame, central differences inside, one-sided
// at the ends; 0 for a one-frame clip.
std::vector<double> speeds(const std::vector<FrameSample>& s, double rate)
{
    std::vector<double> out(s.size(), 0.0);
    if (s.size() < 2) {
        return out;
    }
    for (std::size_t f = 0; f < s.size(); ++f) {
        if (f == 0) {
            out[f] = math::distance(s[1].local, s[0].local) * rate;
        } else if (f + 1 == s.size()) {
            out[f] = math::distance(s[f].local, s[f - 1].local) * rate;
        } else {
            out[f] = math::distance(s[f + 1].local, s[f - 1].local) / (2.0 / rate);
        }
    }
    return out;
}

DetectionResult detectUnchecked(const PoseClip& clip, const std::array<BodyMapping, 2>& maps,
                                const DetectionSettings& settings)
{
    DetectionResult result;
    if (std::string error = validate(clip, settings); !error.empty()) {
        result.error = std::move(error);
        return result;
    }

    // Rule 1: candidates, in a fixed order (source actor, limb order, capsule order).
    std::vector<Candidate> candidates;
    for (int s = 0; s < 2; ++s) {
        const int t = 1 - s;
        const ClipActor& src = clip.actors[static_cast<std::size_t>(s)];
        const ClipActor& tgt = clip.actors[static_cast<std::size_t>(t)];
        std::vector<std::string> sourceBones;
        for (const LimbDefinition& limb : maps[static_cast<std::size_t>(s)].limbs) {
            if (!limb.bones.empty() && std::find(sourceBones.begin(), sourceBones.end(),
                                                 limb.bones.back()) == sourceBones.end()) {
                sourceBones.push_back(limb.bones.back());
            }
        }
        for (const std::string& bone : sourceBones) {
            const auto boneIndex = indexOf(src, bone);
            if (!boneIndex) {
                result.error = "actor " + std::to_string(s) + ": clip has no bone \"" + bone + "\"";
                return result;
            }
            for (const CapsuleRule& region : maps[static_cast<std::size_t>(t)].capsules) {
                const auto a = indexOf(tgt, region.bone);
                const auto b = indexOf(tgt, region.endBone);
                if (!a || !b) {
                    result.error = "actor " + std::to_string(t) + ": clip has no bone \"" +
                                   (a ? region.endBone : region.bone) + "\"";
                    return result;
                }
                candidates.push_back(Candidate{s, t, bone, *boneIndex, &region, *a, *b});
            }
        }
    }

    const double rate = clip.sampleRateHz;
    for (const Candidate& c : candidates) {
        const std::vector<FrameSample> s = sample(clip, c);
        const std::vector<double> v = speeds(s, rate);
        int runIndex = 0; // rule 7: counts every run of this pair, kept or not
        std::size_t f = 0;
        while (f < s.size()) {
            if (!(s[f].distance <= settings.distanceThresholdM)) {
                ++f;
                continue;
            }
            // Rule 5: a maximal run of contact frames [first, last].
            const std::size_t first = f;
            while (f + 1 < s.size() && s[f + 1].distance <= settings.distanceThresholdM) {
                ++f;
            }
            const std::size_t last = f;
            ++f;

            const double count = static_cast<double>(last - first + 1);
            double distanceSum = 0.0;
            double speedSum = 0.0;
            Vec3 localSum;
            Vec3 inwardSum;
            for (std::size_t i = first; i <= last; ++i) {
                distanceSum += s[i].distance;
                speedSum += v[i];
                localSum = localSum + s[i].local;
                inwardSum = inwardSum + s[i].inward;
            }
            DetectedContact contact;
            contact.durationS = count / rate;
            contact.meanDistanceM = distanceSum / count;
            contact.meanSpeedMps = speedSum / count;

            // Rule 6: tiers.
            const int passed =
                (contact.meanDistanceM <= settings.distanceThresholdM / 2.0 ? 1 : 0) +
                (contact.durationS >= settings.minDurationS ? 1 : 0) +
                (contact.meanSpeedMps <= settings.maxRelativeSpeedMps ? 1 : 0);
            contact.confidence = passed == 3   ? Confidence::High
                                 : passed == 2 ? Confidence::Medium
                                               : Confidence::Low;
            const int k = runIndex++;
            if (contact.confidence == Confidence::Low && !settings.includeLowConfidence) {
                continue;
            }

            // Rule 7: the marker.
            InteractionMarker& m = contact.marker;
            m.id = "a" + std::to_string(c.source) + "-" + sanitized(c.bone) + "-on-a" +
                   std::to_string(c.target) + "-" + sanitized(c.region->region) + "-" +
                   std::to_string(k);
            m.sourceActor = c.source;
            m.targetActor = c.target;
            m.sourceBone = c.bone;
            m.targetRegion = c.region->region;
            m.offsetM = localSum / count;
            const Vec3 meanInward = inwardSum / count;
            m.contactDirection = math::length(meanInward) > kZeroDirection
                                     ? math::normalized(meanInward)
                                     : Vec3{0.0, 0.0, -1.0};
            m.window =
                TimeWindow{static_cast<double>(first) / rate, static_cast<double>(last) / rate};
            m.priority = contact.confidence == Confidence::High     ? 2
                         : contact.confidence == Confidence::Medium ? 1
                                                                    : 0;
            m.toleranceM = settings.markerToleranceM;
            result.contacts.push_back(std::move(contact));
        }
    }

    // Rule 8: by window start, then id.
    // stable_sort: two different bone or region names can sanitize to the same
    // id, and equal keys must still come out in one fixed order everywhere.
    std::stable_sort(result.contacts.begin(), result.contacts.end(),
                     [](const DetectedContact& a, const DetectedContact& b) {
                         return std::tie(a.marker.window.startS, a.marker.id) <
                                std::tie(b.marker.window.startS, b.marker.id);
                     });
    result.valid = true;
    return result;
}

} // namespace

MarkerSet DetectionResult::markerSet() const
{
    MarkerSet set;
    set.markers.reserve(contacts.size());
    for (const DetectedContact& c : contacts) {
        set.markers.push_back(c.marker);
    }
    return set;
}

DetectionResult detectContacts(const PoseClip& clip, const std::array<BodyMapping, 2>& bodyMaps,
                               const DetectionSettings& settings)
{
    try {
        return detectUnchecked(clip, bodyMaps, settings);
    } catch (const std::exception& e) {
        // Only allocation can throw here; the contract is "never throws".
        DetectionResult result;
        result.error = std::string("internal error: ") + e.what();
        return result;
    }
}

} // namespace opas::markers
