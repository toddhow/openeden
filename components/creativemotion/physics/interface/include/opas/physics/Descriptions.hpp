#pragma once

// Descriptions used to create bodies and joints, and motor settings that drive
// joints. All plain data; validated by the backend at create time.

#include "opas/physics/Types.hpp"

#include <cstdint>
#include <limits>
#include <variant>

namespace opas::physics {

struct BoxShape {
    Vec3 halfExtentsM{0.5f, 0.5f, 0.5f};
};

struct SphereShape {
    float radiusM = 0.5f;
};

// Capsule along the body's local Z axis (Z up). Total length is
// 2 * (halfHeightM + radiusM).
struct CapsuleShape {
    float halfHeightM = 0.5f;
    float radiusM = 0.25f;
};

using Shape = std::variant<BoxShape, SphereShape, CapsuleShape>;

enum class MotionType : std::uint8_t {
    Static,    // never moves
    Dynamic,   // simulated: gravity, contacts, joints, impulses
    Kinematic, // moved only by setKinematicTarget; pushes dynamic bodies
};

// Defaults for friction, restitution and damping match Jolt's
// BodyCreationSettings defaults at the pinned version.
struct BodyDesc {
    Shape shape = BoxShape{};
    MotionType motion = MotionType::Dynamic;
    Vec3 positionM;
    Quat rotation;
    float massKg = 1.0f; // used for Dynamic bodies only; must be > 0
    float friction = 0.2f;
    float restitution = 0.0f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    // Collision filtering (see PhysicsWorld::createCollisionGroup). An invalid
    // group (the default) collides with everything. In a group, subGroup must
    // be < the group's subGroupCount.
    CollisionGroupId collisionGroup;
    std::uint32_t subGroup = 0;
};

// The shape of a joint's swing limit (docs/tasks/0014).
// Cone: an elliptical cone centred on the creation pose, so each swing range
// must be symmetric (min == -max).
// Pyramid: each swing axis has its own range, which may be wider one way than
// the other (hip flexion 120 degrees, extension 30); each range must contain
// 0, so the joint starts inside its limits. Combined swings are limited by a
// box in the two angles, which has corners a cone would cut off.
enum class SwingShape : std::uint8_t { Cone, Pyramid };

// Swing-twist joint between two bodies, defined in world space at creation.
// The twist axis is the joint's main axis (e.g. along a bone). Angles are
// measured from the creation pose in the joint frame setJointTarget uses
// (X = twistAxis, Y = planeAxis x twistAxis, Z = planeAxis), positive by the
// right-hand rule:
// - plane swing is rotation about Y; it keeps the twist axis in the plane
//   containing planeAxis;
// - normal swing is rotation about Z (planeAxis); it keeps the twist axis in
//   the plane normal to planeAxis;
// - twist is rotation about X.
// Every value is in [-pi, pi] with min <= max. A hinge is a joint with both
// swing ranges [0, 0].
struct JointDesc {
    BodyId bodyA;
    BodyId bodyB;
    Vec3 anchorM;                     // world space
    Vec3 twistAxis{1.0f, 0.0f, 0.0f}; // world space, unit length
    Vec3 planeAxis{0.0f, 1.0f, 0.0f}; // world space, unit length, perpendicular to twistAxis
    SwingShape swingShape = SwingShape::Cone;
    float planeSwingMinRad = 0.0f;
    float planeSwingMaxRad = 0.0f;
    float normalSwingMinRad = 0.0f;
    float normalSwingMaxRad = 0.0f;
    float twistMinRad = 0.0f;
    float twistMaxRad = 0.0f;
};

// Angular spring that pulls a joint towards its target rotation:
// torque = stiffness * angle error - damping * relative angular velocity,
// clamped to maxTorqueNm. Damping alone (stiffness 0) resists motion without
// pulling towards the target. Stiffness and damping both zero means the motor
// is off. setJointMotor rejects negative or non-finite values.
struct JointMotor {
    float stiffnessNmPerRad = 0.0f;
    float dampingNmsPerRad = 0.0f;
    float maxTorqueNm = std::numeric_limits<float>::max();
};

} // namespace opas::physics
