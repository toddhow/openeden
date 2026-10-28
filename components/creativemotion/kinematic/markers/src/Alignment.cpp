#include "opas/markers/Alignment.hpp"

#include "opas/ik/TwoBoneIk.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opas::markers {
namespace {

using math::Quat;
using math::Transform;
using math::Vec3;

// How far a pose rotation may be from unit length (same rule as rest poses).
constexpr double kUnitTolerance = 1e-6;
// Penetrations smaller than this count as "none" when comparing pole samples,
// so float noise does not make the solver rotate a limb for nothing.
constexpr double kPenetrationEpsilonM = 1e-9;

bool isFinite(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool isValidTransform(const Transform& t)
{
    // Written as !(... <= tol) style comparisons so NaN fails.
    return isFinite(t.translation) && std::abs(math::length(t.rotation) - 1.0) <= kUnitTolerance;
}

// --- Geometry -----------------------------------------------------------------

// Shortest distance between segments [p1, q1] and [p2, q2]
// (Ericson, Real-Time Collision Detection, section 5.1.9). Handles segments
// that are points.
double segmentDistance(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2)
{
    const Vec3 d1 = q1 - p1;
    const Vec3 d2 = q2 - p2;
    const Vec3 r = p1 - p2;
    const double a = math::dot(d1, d1);
    const double e = math::dot(d2, d2);
    const double f = math::dot(d2, r);
    double s = 0.0;
    double t = 0.0;
    if (a <= 0.0 && e <= 0.0) {
        return math::length(r);
    }
    if (a <= 0.0) {
        t = std::clamp(f / e, 0.0, 1.0);
    } else {
        const double c = math::dot(d1, r);
        if (e <= 0.0) {
            s = std::clamp(-c / a, 0.0, 1.0);
        } else {
            const double b = math::dot(d1, d2);
            const double denom = a * e - b * b; // >= 0; 0 when parallel
            s = denom > 0.0 ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0.0) {
                t = 0.0;
                s = std::clamp(-c / a, 0.0, 1.0);
            } else if (t > 1.0) {
                t = 1.0;
                s = std::clamp((b - c) / a, 0.0, 1.0);
            }
        }
    }
    return math::distance(p1 + s * d1, p2 + t * d2);
}

// --- Per-actor working state --------------------------------------------------

struct PosedCapsule {
    std::string region;
    std::size_t bone = 0; // frame for marker offsets
    std::size_t endBone = 0;
    double radiusRatio = 0.0;
};

struct PosedChain {
    const IkChain* chain = nullptr;
    std::size_t root = 0;
    std::size_t mid = 0;
    std::size_t end = 0;
};

// Owns a copy of one actor's pose; world transforms are recomputed from it
// whenever a limb is bent.
struct ActorState {
    const ActorInput* input = nullptr;
    std::vector<Transform> local;
    Transform root;
    std::vector<Transform> world;
    std::vector<PosedCapsule> capsules;
    std::vector<PosedChain> chains;

    void updateWorld() { world = rig::computeWorldPose(input->skeleton, local, root); }

    [[nodiscard]] const PosedCapsule* findCapsule(const std::string& region) const
    {
        for (const PosedCapsule& c : capsules) {
            if (c.region == region) {
                return &c;
            }
        }
        return nullptr;
    }
};

// True when `bone` is a descendant of `ancestor` (at any depth). Parents come
// first in a valid skeleton, so the walk up always ends.
bool isBelow(const std::vector<rig::Bone>& bones, std::size_t bone, std::size_t ancestor)
{
    int parent = bones[bone].parent;
    while (parent >= 0) {
        if (static_cast<std::size_t>(parent) == ancestor) {
            return true;
        }
        parent = bones[static_cast<std::size_t>(parent)].parent;
    }
    return false;
}

std::optional<std::size_t> boneIndex(const rig::Skeleton& skeleton, const std::string& name)
{
    return rig::findBone(skeleton, name);
}

// Validates one actor and builds its working state. Empty string on success.
std::string prepareActor(const ActorInput& input, int index, ActorState& state)
{
    const std::string who = "actor " + std::to_string(index);
    if (const std::string error = rig::validateSkeleton(input.skeleton); !error.empty()) {
        return who + ": invalid skeleton: " + error;
    }
    // The whole mapping must be valid, including the limb definitions the
    // solver itself does not use: measureBody applies the same rules
    // (limbs, capsules, duplicate names) everywhere a mapping is accepted.
    if (const MeasureResult measured = measureBody(input.skeleton, input.mapping);
        !measured.value) {
        return who + ": invalid mapping: " + measured.error;
    }
    if (input.localPose.size() != input.skeleton.bones.size()) {
        return who + ": localPose has " + std::to_string(input.localPose.size()) +
               " transforms for " + std::to_string(input.skeleton.bones.size()) + " bones";
    }
    for (std::size_t i = 0; i < input.localPose.size(); ++i) {
        if (!isValidTransform(input.localPose[i])) {
            return who + ": localPose[" + std::to_string(i) +
                   "] must be finite with a unit rotation";
        }
    }
    if (!isValidTransform(input.root)) {
        return who + ": root must be finite with a unit rotation";
    }
    if (!(input.alignmentWeight >= 0.0) || !std::isfinite(input.alignmentWeight)) {
        return who + ": alignmentWeight must be >= 0";
    }

    state.input = &input;
    state.local.assign(input.localPose.begin(), input.localPose.end());
    state.root = input.root;

    for (const CapsuleRule& rule : input.mapping.capsules) {
        const std::optional<std::size_t> bone = boneIndex(input.skeleton, rule.bone);
        const std::optional<std::size_t> end = boneIndex(input.skeleton, rule.endBone);
        if (!bone || !end) {
            return who + ": capsule \"" + rule.region + "\": no bone named \"" +
                   (bone ? rule.endBone : rule.bone) + "\"";
        }
        if (!(rule.radiusRatio > 0.0) || !std::isfinite(rule.radiusRatio)) {
            return who + ": capsule \"" + rule.region + "\": radiusRatio must be > 0";
        }
        if (state.findCapsule(rule.region) != nullptr) {
            return who + ": capsule \"" + rule.region + "\": region is used by an earlier capsule";
        }
        state.capsules.push_back(PosedCapsule{rule.region, *bone, *end, rule.radiusRatio});
    }

    for (const IkChain& chain : input.mapping.ikChains) {
        const std::string what = who + ": ikChain \"" + chain.name + "\"";
        const std::optional<std::size_t> root = boneIndex(input.skeleton, chain.rootBone);
        const std::optional<std::size_t> mid = boneIndex(input.skeleton, chain.midBone);
        const std::optional<std::size_t> end = boneIndex(input.skeleton, chain.endBone);
        if (!root || !mid || !end) {
            const std::string& missing =
                !root ? chain.rootBone : (!mid ? chain.midBone : chain.endBone);
            std::string message = what;
            message += ": no bone named \"";
            message += missing;
            message += '"';
            return message;
        }
        // Bones in between are allowed (A4c-1): they keep their local
        // transforms and ride along. Only the order root > mid > end is required.
        const auto& bones = input.skeleton.bones;
        if (!isBelow(bones, *mid, *root) || !isBelow(bones, *end, *mid)) {
            return what + ": midBone must be below rootBone and endBone below midBone";
        }
        if (!(chain.limits.minBendRad >= 0.0) || !(chain.limits.maxBendRad <= std::numbers::pi) ||
            !(chain.limits.minBendRad <= chain.limits.maxBendRad)) {
            return what + ": limits must satisfy 0 <= minBendRad <= maxBendRad <= pi";
        }
        if (!isFinite(chain.poleDirection) || math::length(chain.poleDirection) <= 0.0) {
            return what + ": poleDirection must be finite and non-zero";
        }
        if (!(chain.limbRadiusM > 0.0) || !std::isfinite(chain.limbRadiusM)) {
            return what + ": limbRadiusM must be > 0";
        }
        // One chain per hand or foot: two chains ending at the same bone would
        // both be driven by the same marker and fight over the limb.
        for (const PosedChain& earlier : state.chains) {
            if (earlier.chain->name == chain.name || earlier.end == *end) {
                return what + ": name or endBone is used by an earlier chain";
            }
        }
        state.chains.push_back(PosedChain{&chain, *root, *mid, *end});
    }

    state.updateWorld();
    return {};
}

// Validates every marker (active or not) against the two actors.
std::string validateMarkers(const MarkerSet& set, const std::array<ActorState, 2>& actors)
{
    for (const InteractionMarker& m : set.markers) {
        const std::string what = "marker \"" + m.id + "\"";
        if (m.sourceActor < 0 || m.sourceActor > 1 || m.targetActor < 0 || m.targetActor > 1 ||
            m.sourceActor == m.targetActor) {
            return what + ": sourceActor and targetActor must be 0 and 1 (in either order)";
        }
        const ActorState& source = actors[static_cast<std::size_t>(m.sourceActor)];
        const ActorState& target = actors[static_cast<std::size_t>(m.targetActor)];
        if (!boneIndex(source.input->skeleton, m.sourceBone)) {
            return what + ": sourceBone \"" + m.sourceBone + "\" is not a bone of actor " +
                   std::to_string(m.sourceActor);
        }
        if (target.findCapsule(m.targetRegion) == nullptr) {
            return what + ": targetRegion \"" + m.targetRegion + "\" is not a region of actor " +
                   std::to_string(m.targetActor);
        }
        if (!isFinite(m.offsetM) || !std::isfinite(m.window.startS) ||
            !std::isfinite(m.window.endS) || !(m.toleranceM > 0.0)) {
            return what + ": offsetM, window and toleranceM must be finite (toleranceM > 0)";
        }
    }
    return {};
}

bool isActive(const InteractionMarker& m, double clipTimeS)
{
    return m.window.startS <= clipTimeS && clipTimeS <= m.window.endS;
}

Vec3 targetPoint(const ActorState& target, const InteractionMarker& m)
{
    const PosedCapsule* capsule = target.findCapsule(m.targetRegion);
    return math::transformPoint(target.world[capsule->bone], m.offsetM);
}

Vec3 sourcePoint(const ActorState& source, const InteractionMarker& m)
{
    // validateMarkers already checked the bone exists, so value_or never kicks in.
    const std::size_t bone = boneIndex(source.input->skeleton, m.sourceBone).value_or(0);
    return source.world[bone].translation;
}

// Summed penetration of a limb's two segments into the other actor's
// capsules, skipping the region the limb is meant to touch.
double penetration(Vec3 root, Vec3 mid, Vec3 end, double limbRadiusM, const ActorState& other,
                   const std::string& contactRegion)
{
    double total = 0.0;
    for (const PosedCapsule& c : other.capsules) {
        if (c.region == contactRegion) {
            continue;
        }
        const Vec3 a = other.world[c.bone].translation;
        const Vec3 b = other.world[c.endBone].translation;
        const double radius = c.radiusRatio * math::distance(a, b);
        for (const auto& [p, q] : {std::pair{root, mid}, std::pair{mid, end}}) {
            total += std::max(0.0, radius + limbRadiusM - segmentDistance(p, q, a, b));
        }
    }
    return total;
}

// The marker that drives a chain: highest priority, then smallest id.
const InteractionMarker* drivingMarker(const MarkerSet& set, double clipTimeS, int actor,
                                       const std::string& endBone)
{
    const InteractionMarker* best = nullptr;
    for (const InteractionMarker& m : set.markers) {
        if (!isActive(m, clipTimeS) || m.sourceActor != actor || m.sourceBone != endBone) {
            continue;
        }
        if (best == nullptr || m.priority > best->priority ||
            (m.priority == best->priority && m.id < best->id)) {
            best = &m;
        }
    }
    return best;
}

// Step 1: least-squares horizontal shift, split by weight (see the spec).
void alignRoots(std::array<ActorState, 2>& actors, const MarkerSet& set, double clipTimeS,
                const AlignmentSettings& settings, AlignmentResult& result)
{
    Vec3 sum;
    int count = 0;
    for (const InteractionMarker& m : set.markers) {
        if (!isActive(m, clipTimeS)) {
            continue;
        }
        const Vec3 error = targetPoint(actors[static_cast<std::size_t>(m.targetActor)], m) -
                           sourcePoint(actors[static_cast<std::size_t>(m.sourceActor)], m);
        // Moving actor 0 by d closes errors where actor 0 is the source and
        // opens those where it is the target, hence the sign.
        sum = sum + (m.sourceActor == 0 ? error : -error);
        ++count;
    }
    if (count == 0) {
        return;
    }
    Vec3 delta = sum / static_cast<double>(count);
    delta.z = 0.0; // both actors stay on the ground
    const double length = math::length(delta);
    if (length > settings.maxRootShiftM) {
        delta = delta * (settings.maxRootShiftM / length);
        result.rootShiftClamped = true;
    }
    const double w0 = actors[0].input->alignmentWeight;
    const double w1 = actors[1].input->alignmentWeight;
    result.rootShiftM[0] = delta * (w0 / (w0 + w1));
    result.rootShiftM[1] = delta * (-w1 / (w0 + w1));
    for (std::size_t i = 0; i < 2; ++i) {
        actors[i].root.translation = actors[i].root.translation + result.rootShiftM[i];
        actors[i].updateWorld();
    }
}

// Step 2 and 3 for one chain: IK towards the marker, then pole search.
LimbCorrection solveChain(ActorState& self, int selfIndex, const PosedChain& posed,
                          const InteractionMarker& marker, const ActorState& other,
                          const AlignmentSettings& settings)
{
    const IkChain& chain = *posed.chain;
    const ik::TwoBoneChain positions{self.world[posed.root].translation,
                                     self.world[posed.mid].translation,
                                     self.world[posed.end].translation};
    const Vec3 target = targetPoint(other, marker);
    const Vec3 basePole = math::rotate(self.root.rotation, chain.poleDirection);
    const Vec3 axis = math::normalized(target - positions.root);

    ik::TwoBoneResult best = ik::solveTwoBone(positions, target, basePole, chain.limits);
    double bestPenetration = penetration(positions.root, best.mid, best.end, chain.limbRadiusM,
                                         other, marker.targetRegion);

    // Try rotated poles in order of increasing rotation (+k before -k), keeping
    // a candidate only if it is strictly better, so ties keep the smaller turn.
    const int samples = settings.poleSamples;
    for (int k = 1; k <= samples / 2 && bestPenetration > kPenetrationEpsilonM; ++k) {
        for (const int sign : {1, -1}) {
            if (sign < 0 && 2 * k == samples) {
                continue; // +pi and -pi are the same pole
            }
            const double angle = sign * 2.0 * std::numbers::pi * k / samples;
            const Vec3 pole = math::rotate(math::fromAxisAngle(axis, angle), basePole);
            const ik::TwoBoneResult candidate =
                ik::solveTwoBone(positions, target, pole, chain.limits);
            const double p = penetration(positions.root, candidate.mid, candidate.end,
                                         chain.limbRadiusM, other, marker.targetRegion);
            if (p < bestPenetration - kPenetrationEpsilonM) {
                best = candidate;
                bestPenetration = p;
            }
        }
    }

    // Turn the world-space deltas into new local rotations, then recompute the
    // world pose so later steps (and the other actor's targets) see the bend.
    if (best.valid) {
        const auto parentWorldRotation = [&](std::size_t bone) {
            const int parent = self.input->skeleton.bones[bone].parent;
            return parent < 0 ? self.root.rotation
                              : self.world[static_cast<std::size_t>(parent)].rotation;
        };
        const Quat rootWorld =
            math::normalized(best.rootRotation * self.world[posed.root].rotation);
        const Quat midWorld = math::normalized(best.midRotation * self.world[posed.mid].rotation);
        self.local[posed.root].rotation =
            math::normalized(math::conjugate(parentWorldRotation(posed.root)) * rootWorld);
        const int midParent = self.input->skeleton.bones[posed.mid].parent;
        if (midParent == static_cast<int>(posed.root)) {
            // Direct child: its parent's new world rotation is rootWorld
            // exactly (kept this way so A2 results stay bit-identical).
            self.local[posed.mid].rotation =
                math::normalized(math::conjugate(rootWorld) * midWorld);
        } else {
            // Intermediate bones between root and mid (A4c-1): recompute the
            // pose so the mid bone's real parent has moved with the root,
            // then convert against that parent's new world rotation.
            self.updateWorld();
            self.local[posed.mid].rotation =
                math::normalized(math::conjugate(parentWorldRotation(posed.mid)) * midWorld);
        }
        self.updateWorld();
    }

    LimbCorrection correction;
    correction.actor = selfIndex;
    correction.chain = chain.name;
    correction.markerId = marker.id;
    correction.rootBoneDelta = best.rootRotation;
    correction.midBoneDelta = best.midRotation;
    correction.reached = best.reached;
    correction.penetrationM = bestPenetration;
    return correction;
}

AlignmentResult alignPairUnchecked(const ActorInput& actor0, const ActorInput& actor1,
                                   const MarkerSet& set, double clipTimeS,
                                   const AlignmentSettings& settings)
{
    AlignmentResult result;
    const auto fail = [&result](std::string message) {
        result = AlignmentResult{};
        result.error = std::move(message);
        return result;
    };

    if (!std::isfinite(clipTimeS)) {
        return fail("clipTimeS must be finite");
    }
    if (!(settings.maxRootShiftM >= 0.0) || !std::isfinite(settings.maxRootShiftM)) {
        return fail("settings.maxRootShiftM must be >= 0");
    }
    if (settings.poleSamples < 1) {
        return fail("settings.poleSamples must be >= 1");
    }

    std::array<ActorState, 2> actors;
    if (std::string error = prepareActor(actor0, 0, actors[0]); !error.empty()) {
        return fail(std::move(error));
    }
    if (std::string error = prepareActor(actor1, 1, actors[1]); !error.empty()) {
        return fail(std::move(error));
    }
    if (!(actor0.alignmentWeight + actor1.alignmentWeight > 0.0)) {
        return fail("at least one actor needs an alignmentWeight > 0");
    }
    if (std::string error = validateMarkers(set, actors); !error.empty()) {
        return fail(std::move(error));
    }

    alignRoots(actors, set, clipTimeS, settings, result);

    for (int a = 0; a < 2; ++a) {
        ActorState& self = actors[static_cast<std::size_t>(a)];
        const ActorState& other = actors[static_cast<std::size_t>(1 - a)];
        for (const PosedChain& posed : self.chains) {
            const InteractionMarker* marker =
                drivingMarker(set, clipTimeS, a, posed.chain->endBone);
            if (marker != nullptr) {
                result.limbs.push_back(solveChain(self, a, posed, *marker, other, settings));
            }
        }
    }

    for (const InteractionMarker& m : set.markers) {
        if (!isActive(m, clipTimeS)) {
            continue;
        }
        const double distance =
            math::distance(targetPoint(actors[static_cast<std::size_t>(m.targetActor)], m),
                           sourcePoint(actors[static_cast<std::size_t>(m.sourceActor)], m));
        result.markers.push_back(MarkerResidual{m.id, distance, distance <= m.toleranceM});
    }
    // Inputs are finite, but values near the double limit (e.g. 1e308 m) can
    // still overflow inside the math. Report that instead of returning NaN.
    const bool finite =
        isFinite(result.rootShiftM[0]) && isFinite(result.rootShiftM[1]) &&
        std::all_of(result.markers.begin(), result.markers.end(),
                    [](const MarkerResidual& r) { return std::isfinite(r.distanceM); }) &&
        std::all_of(result.limbs.begin(), result.limbs.end(),
                    [](const LimbCorrection& l) { return std::isfinite(l.penetrationM); });
    if (!finite) {
        return fail("input values are too large: the solution overflowed");
    }
    result.valid = true;
    return result;
}

} // namespace

AlignmentResult alignPair(const ActorInput& actor0, const ActorInput& actor1,
                          const MarkerSet& markers, double clipTimeS,
                          const AlignmentSettings& settings)
{
    try {
        return alignPairUnchecked(actor0, actor1, markers, clipTimeS, settings);
    } catch (const std::exception& e) {
        // Only allocation can throw here; the contract is "never throws".
        AlignmentResult result;
        result.error = std::string("internal error: ") + e.what();
        return result;
    }
}

} // namespace opas::markers
