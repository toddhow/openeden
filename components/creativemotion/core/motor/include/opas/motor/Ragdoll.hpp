#pragma once

// Building a ragdoll from a rig and a profile in a physics world (M3,
// docs/tasks/0013).
//
// Ownership: a Ragdoll is a plain value owned by the host. It holds handles
// into the world, never pointers, so every call takes the world as a
// parameter. Call destroyRagdoll before destroying the world; nothing reaches
// into the world from a destructor.

#include "opas/math/Transform.hpp"
#include "opas/physics/PhysicsWorld.hpp"
#include "opas/rig/Profile.hpp"
#include "opas/rig/Rig.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace opas::motor {

enum class RootMotion : std::uint8_t {
    Dynamic,   // the root body is simulated like the rest
    Kinematic, // the root body follows the pose (a puppet held at the pelvis), until M5 can balance
};

struct RagdollOptions {
    RootMotion rootMotion = RootMotion::Dynamic;
};

// One physics body per rig body, in the same order as Rig::bodies.
struct RagdollBody {
    physics::BodyId id;
    std::size_t bone = 0;  // index into the skeleton
    math::Transform local; // the body in its bone's frame (from the rig)
    math::Quat restModel;  // rest rotation in model space (root = identity)
};

// One physics joint per rig joint, in the same order as Rig::joints.
struct RagdollJoint {
    physics::JointId id;
    std::size_t parentBody = 0; // indices into Ragdoll::bodies
    std::size_t childBody = 0;
    // The joint frame in model space at rest: X = twist axis, Y = plane axis
    // cross twist axis, Z = plane axis (the frame setJointTarget's targets are
    // written in; Jolt's constraint space).
    math::Quat frameModel;
};

// Move-only: a copy would share the world's handles, and destroying both
// copies would destroy the bodies twice.
struct Ragdoll {
    Ragdoll() = default;
    Ragdoll(const Ragdoll&) = delete;
    Ragdoll& operator=(const Ragdoll&) = delete;
    Ragdoll(Ragdoll&&) noexcept = default;
    Ragdoll& operator=(Ragdoll&&) noexcept = default;
    ~Ragdoll() = default;

    std::vector<RagdollBody> bodies;
    std::vector<RagdollJoint> joints;
    physics::CollisionGroupId collisionGroup;
    std::size_t rootBody = 0;
    bool kinematicRoot = false;
    // Where the model root sits in the world. A kinematic root body follows
    // rootWorld * (the pose of its bone); the host may move it between steps.
    math::Transform rootWorld;
    // Copy of the rig's skeleton (parents and rest translations) for forward
    // kinematics, and the scratch buffer it writes into, sized at build time
    // so driving the ragdoll never allocates.
    rig::Skeleton skeleton;
    std::vector<math::Transform> poseModel;
    // Previous joint target rotations in constraint space for velocity feed-forward.
    std::vector<math::Quat> lastJointTargets;
    bool hasLastJointTargets = false;
};

struct BuildResult {
    std::optional<Ragdoll> ragdoll; // set on success
    physics::ErrorCode error = physics::ErrorCode::None;
    std::string message; // why the build failed, naming the rig body or joint

    [[nodiscard]] bool ok() const { return ragdoll.has_value(); }
};

// Creates the ragdoll in its rest pose with the model root at `rootWorld`:
// one body per rig body (shape, mass, the profile's material), one joint per
// rig joint (anchor at the joint bone's rest origin, the rig's axes and
// limits, the profile's motor gains) and one collision group in which
// jointed pairs do not collide. Rig and profile are validated again; on any
// failure everything already created is destroyed and the error returned.
BuildResult buildRagdoll(physics::PhysicsWorld& world, const rig::Rig& rig,
                         const rig::Profile& profile, const math::Transform& rootWorld,
                         const RagdollOptions& options = {});

// Destroys the ragdoll's bodies (which destroys its joints) and its
// collision group, and leaves `ragdoll` empty. Returns false if anything was
// already gone (a stale handle) or the call came from a step listener.
bool destroyRagdoll(physics::PhysicsWorld& world, Ragdoll& ragdoll);

// Death (task 0018): turns every joint motor off (stiffness and damping 0)
// and, when the profile has `death`, sets each joint's position steps and each
// dynamic body's damping from it; without it, only the motors change. Returns
// false if any call failed (the others are still applied). Allocates nothing.
// Remove the ragdoll's driver first: setting joint targets every step keeps
// the bodies awake, so a driven ragdoll never sleeps.
bool makeLimp(physics::PhysicsWorld& world, const Ragdoll& ragdoll, const rig::Profile& profile);

// The rotation of a joint's child body relative to its parent body, written
// in the joint frame as setJointTarget expects it, from the two bodies'
// rotations (world or model space, as long as both are in the same space).
// Identity at the rest pose. Used for targets (from a target pose) and for
// measuring (from the bodies' actual rotations). Unit, with w >= 0.
math::Quat jointRotation(const Ragdoll& ragdoll, std::size_t jointIndex, math::Quat parentBody,
                         math::Quat childBody);

// Applies an incremental root motion delta in the character's local horizontal frame
// to ragdoll.rootWorld:
// rootWorld_{t+dt} = rootWorld_t * deltaRoot
// Allocates nothing (C4b).
void applyRootMotion(Ragdoll& ragdoll, const math::Transform& deltaRoot);
void applyRootMotion(Ragdoll& ragdoll, const math::Vec3& deltaTranslationM, double deltaYawRad);

} // namespace opas::motor
