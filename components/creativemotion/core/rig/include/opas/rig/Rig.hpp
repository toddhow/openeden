#pragma once

// The rig: a character's physical body. A skeleton (what animates), rigid
// bodies attached to some of its bones (what is simulated) and swing-twist
// joints between those bodies. File format: docs/formats/rig.md.
//
// Shapes and joint limits mirror opas::physics::Shape and JointDesc one to one
// so M3 can build a ragdoll from a rig, but core/rig includes no physics
// headers: the conversion lives with the code that builds the ragdoll.

#include "opas/math/Transform.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/rig/HelperBone.hpp"
#include "opas/rig/Skeleton.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace opas::rig {

// Capsule along the body's local Z axis. Total length 2 * (halfHeightM + radiusM).
struct CapsuleShape {
    double halfHeightM = 0.0;
    double radiusM = 0.0;
    friend bool operator==(const CapsuleShape&, const CapsuleShape&) = default;
};

struct BoxShape {
    math::Vec3 halfExtentsM;
    friend bool operator==(const BoxShape&, const BoxShape&) = default;
};

struct SphereShape {
    double radiusM = 0.0;
    friend bool operator==(const SphereShape&, const SphereShape&) = default;
};

using Shape = std::variant<CapsuleShape, BoxShape, SphereShape>;

// A rigid body attached to one bone. `local` places the body's centre and
// orientation in that bone's frame.
struct Body {
    std::string bone;
    Shape shape;
    math::Transform local;
    double massKg = 0.0;
    friend bool operator==(const Body&, const Body&) = default;
};

// The shape of a joint's swing limit, as opas::physics::SwingShape.
// Cone: symmetric about the rest pose (version 1 files have only cones).
// Pyramid: each swing axis has its own range, which may be wider one way than
// the other; each range contains 0 (docs/formats/rig.md, version 2).
enum class SwingShape : std::uint8_t { Cone, Pyramid };

// Swing-twist joint between the body on `bone` and the body of the nearest
// ancestor bone that has one. The anchor is `bone`'s origin in the rest pose.
// Axes are unit vectors in `bone`'s frame; the twist axis is the joint's main
// axis. Angles are measured from the rest pose in the joint frame
// (X = twistAxis, Y = planeAxis x twistAxis, Z = planeAxis), positive by the
// right-hand rule, as in opas::physics::JointDesc:
// - plane swing is rotation about Y (the twist axis moves in the plane
//   containing planeAxis);
// - normal swing is rotation about Z = planeAxis (the twist axis moves in the
//   plane normal to it);
// - twist is rotation about X.
// A cone stores its half-angles as min = -half, max = +half.
struct Joint {
    std::string bone;
    math::Vec3 twistAxis;
    math::Vec3 planeAxis;
    SwingShape swing = SwingShape::Cone;
    double planeSwingMinRad = 0.0;
    double planeSwingMaxRad = 0.0;
    double normalSwingMinRad = 0.0;
    double normalSwingMaxRad = 0.0;
    double twistMinRad = 0.0;
    double twistMaxRad = 0.0;
    friend bool operator==(const Joint&, const Joint&) = default;
};

struct Rig {
    std::string name;
    Skeleton skeleton;
    std::vector<Body> bodies;
    std::vector<Joint> joints;
    std::vector<HelperRule> helpers;
    friend bool operator==(const Rig&, const Rig&) = default;
};

// Empty string if the rig is valid (every rule in docs/formats/rig.md);
// otherwise "<JSON pointer>: <message>", e.g.
// "/bodies/3/massKg: must be > 0". The pointer names the field as it appears
// in a rig file, so programmatic rigs and loaded files report alike.
std::string validateRig(const Rig& rig);

// Index into rig.bodies of the body on `bone`, if any.
std::optional<std::size_t> findBody(const Rig& rig, std::string_view bone);

// Index into rig.joints of the joint on `bone`, if any.
std::optional<std::size_t> findJoint(const Rig& rig, std::string_view bone);

// Index into rig.bodies of the body a joint on bodies[bodyIndex] connects to:
// the body of the nearest ancestor bone that has one. Empty for the root body.
// The rig must be valid.
std::optional<std::size_t> parentBody(const Rig& rig, std::size_t bodyIndex);

} // namespace opas::rig
