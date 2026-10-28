#pragma once

// Body measurements from a skeleton's rest pose: limb lengths and simple
// surface capsules. The alignment solver uses them to compare two actors of
// different sizes and to keep limbs from passing through bodies.
//
// Which bones form which limb is data (a BodyMapping), because skeletons name
// their bones differently.

#include "opas/ik/TwoBoneIk.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/rig/Skeleton.hpp"

#include <optional>
#include <string>
#include <vector>

namespace opas::markers {

// A limb is a chain of bones from proximal to distal, e.g.
// {"RightUpperArm", "RightForearm", "RightHand"}; its length is the sum of the
// distances between consecutive bones' world rest positions. Needs >= 2 bones.
struct LimbDefinition {
    std::string name;
    std::vector<std::string> bones;
};

// A capsule from `bone` to `endBone` (world rest positions), named as a body
// region, with radius = radiusRatio * its length. radiusRatio must be > 0.
struct CapsuleRule {
    std::string region; // e.g. "RightForearm"; what InteractionMarker::targetRegion names
    std::string bone;
    std::string endBone;
    double radiusRatio = 0.15;
};

// A two-bone IK chain the alignment solver may bend, e.g. an arm
// {rootBone "RightUpperArm", midBone "RightForearm", endBone "RightHand"}.
// endBone is what a marker's sourceBone names. midBone must be below rootBone
// in the skeleton and endBone below midBone, at any depth: bones in between
// (e.g. XPMSSE's CME nodes) are allowed and move rigidly with their parents.
struct IkChain {
    std::string name;
    std::string rootBone;
    std::string midBone;
    std::string endBone;
    ik::HingeLimits limits;
    // Where the mid joint (elbow, knee) points, in the actor's root frame.
    math::Vec3 poleDirection{0.0, -1.0, 0.0};
    double limbRadiusM = 0.04; // limb thickness for penetration tests, > 0
};

// A finger for finger contact (A5c, docs/tasks/0009): its joints from the
// knuckle outward, the IK chain whose end bone is its hand, the axis each
// joint bends about in that joint's own frame, and each joint's curl limit.
struct FingerDefinition {
    std::string name;
    std::string chain;                   // an IkChain::name in the same map
    std::vector<std::string> bones;      // >= 2, knuckle first
    math::Vec3 hingeAxis{0.0, 0.0, 1.0}; // in each joint's local frame; not zero
    std::vector<double> maxCurlRad;      // one per bone, each in [0, pi]
};

struct BodyMapping {
    std::vector<LimbDefinition> limbs;
    std::vector<CapsuleRule> capsules;
    std::vector<IkChain> ikChains;
    std::vector<FingerDefinition> fingers; // body map format version 2 only
};

struct LimbLength {
    std::string name;
    double lengthM = 0.0;
};

struct SurfaceCapsule {
    std::string region;
    math::Vec3 startM; // model space, rest pose
    math::Vec3 endM;
    double radiusM = 0.0;
};

struct BodyMeasurements {
    std::vector<LimbLength> limbs;        // same order as BodyMapping::limbs
    std::vector<SurfaceCapsule> capsules; // same order as BodyMapping::capsules
    double heightM = 0.0;                 // highest minus lowest bone world rest z
};

struct MeasureResult {
    std::optional<BodyMeasurements> value;
    std::string error; // names the bad limb, capsule or bone when value is empty
};

// The skeleton must pass rig::validateSkeleton (an invalid one is rejected).
MeasureResult measureBody(const rig::Skeleton& skeleton, const BodyMapping& mapping);

} // namespace opas::markers
