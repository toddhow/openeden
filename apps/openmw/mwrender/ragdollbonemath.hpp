#ifndef OPENEDEN_MWRENDER_RAGDOLLBONEMATH_H
#define OPENEDEN_MWRENDER_RAGDOLLBONEMATH_H

#include <components/misc/vsgmath.hpp>

namespace MWRender::RagdollBones
{
    /// Pure rotation maths of the ragdoll bone driver (task 0106). Quaternions are unit, world space. No state, no
    /// allocation.
    ///
    /// vsg::dquat's operator* is OSG's: a * b applies a FIRST and then b (opas' and the textbook Hamilton product
    /// are the other way round). Everything here is written with compose(), "outer after inner" in the textbook
    /// sense, so that world = compose(parentWorld, local) as a bone's matrices multiply.
    ///
    /// The ragdoll's bone frames and the actor's skeleton bone frames do not agree (the rig has identity rest
    /// rotations, Morrowind's bones have their own axes). On the frame the ragdoll is made, the offset between the two
    /// is captured once, and every later frame turns the actor's bone by whatever the ragdoll's bone turned from its
    /// own start.

    /// The rotation that applies inner first and then outer (textbook outer * inner).
    inline vsg::dquat compose(const vsg::dquat& outer, const vsg::dquat& inner)
    {
        return inner * outer;
    }

    /// The fixed offset between a ragdoll bone's world rotation and the actor bone's world rotation, both read on the
    /// spawn frame: ragdollBone(0) * delta == actorBone(0).
    inline vsg::dquat bindDelta(const vsg::dquat& ragdollBoneAtSpawn, const vsg::dquat& actorBoneAtSpawn)
    {
        return compose(Misc::VsgMath::inverse(ragdollBoneAtSpawn), actorBoneAtSpawn);
    }

    /// Task 0106, second slice: the rotation of a ragdoll bone, relative to the rig's rest pose, that matches the
    /// actor's bone now. The rig's rest rotations are identity, so with F the actor bone's rest rotation (the same
    /// space as the actor's rotation now) the ragdoll bone R must satisfy "R after F == actor now", which is the
    /// driver's own relation actor = R * delta with delta = F.
    inline vsg::dquat restRelative(const vsg::dquat& actorBoneNow, const vsg::dquat& actorBoneAtRest)
    {
        return compose(actorBoneNow, Misc::VsgMath::inverse(actorBoneAtRest));
    }

    /// The actor bone's world rotation to draw this frame.
    inline vsg::dquat drivenWorldRotation(const vsg::dquat& ragdollBoneNow, const vsg::dquat& delta)
    {
        return compose(ragdollBoneNow, delta);
    }

    /// The bone's local rotation (relative to its parent) that gives the wanted world rotation.
    inline vsg::dquat localFromWorld(const vsg::dquat& parentWorld, const vsg::dquat& wantedWorld)
    {
        return compose(Misc::VsgMath::inverse(parentWorld), wantedWorld);
    }

    /// Where the pelvis bone's origin should be in world space: its position on the spawn frame plus how far the
    /// ragdoll's pelvis has moved since its own spawn frame.
    inline vsg::dvec3 drivenPelvisPosition(const vsg::dvec3& actorPelvisAtSpawn, const vsg::dvec3& ragdollPelvisAtSpawn,
        const vsg::dvec3& ragdollPelvisNow)
    {
        return actorPelvisAtSpawn + (ragdollPelvisNow - ragdollPelvisAtSpawn);
    }
}

#endif
