#pragma once

// Pair alignment for paired animations (A2): shift two actors' roots so the
// active markers come within reach, then bend limbs with two-bone IK to meet
// them while keeping limbs out of the other actor's body capsules.
// Design and rules: docs/tasks/0003-a2-alignment-solver.md.

#include "opas/markers/BodyMeasurements.hpp"
#include "opas/markers/InteractionMarker.hpp"
#include "opas/math/Quat.hpp"
#include "opas/math/Transform.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/rig/Skeleton.hpp"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace opas::markers {

// One actor as the solver sees it. Holds references: the referenced data must
// outlive the alignPair call, and nothing is kept afterwards.
struct ActorInput {
    const rig::Skeleton& skeleton;
    const BodyMapping& mapping;
    std::span<const math::Transform> localPose; // one per bone, relative to the parent
    math::Transform root;                       // model -> world
    double alignmentWeight = 1.0; // share of the root correction this actor takes; >= 0
};

struct AlignmentSettings {
    // Largest horizontal root shift (relative between the actors) per call.
    // Guards against teleporting when markers are badly authored.
    double maxRootShiftM = 0.5;
    // Pole directions tried when a limb penetrates the other actor (>= 1;
    // 1 means only the chain's own pole).
    int poleSamples = 16;
};

struct LimbCorrection {
    int actor = 0;
    std::string chain;    // IkChain::name
    std::string markerId; // the marker that drove it
    // World-space delta rotations for the chain's root and mid bones (apply on
    // top of their posed world rotations), as ik::TwoBoneResult defines them.
    math::Quat rootBoneDelta;
    math::Quat midBoneDelta;
    bool reached = false;
    double penetrationM = 0.0; // remaining penetration after pole search
};

struct MarkerResidual {
    std::string id;
    double distanceM = 0.0; // after root shift and limb IK
    bool met = false;       // distanceM <= the marker's toleranceM
};

struct AlignmentResult {
    bool valid = false;
    std::string error;                      // set when valid is false
    std::array<math::Vec3, 2> rootShiftM{}; // world-space translation to add to each actor's root
    bool rootShiftClamped = false;
    std::vector<LimbCorrection> limbs;   // actor 0 chains first, each in mapping order
    std::vector<MarkerResidual> markers; // active markers, in MarkerSet order
};

// Aligns actor 0 and actor 1 for the markers active at clipTimeS.
// Deterministic; never throws.
AlignmentResult alignPair(const ActorInput& actor0, const ActorInput& actor1,
                          const MarkerSet& markers, double clipTimeS,
                          const AlignmentSettings& settings = {});

} // namespace opas::markers
