#ifndef OPENEDEN_MWRENDER_FOOTIKMATH_H
#define OPENEDEN_MWRENDER_FOOTIKMATH_H

#include "bonepose.hpp"

#include <components/misc/vsgmath.hpp>

namespace MWRender::FootIk
{
    /// The pure maths of ActorIkController::solveLeg, with no world, bones or physics, so that unit tests can run it
    /// (task 0011). All matrices are VSG column-vector matrices in world space (world = parent * local), in Morrowind
    /// units; nothing here reads a clock or allocates.
    ///
    /// Foot IK changes bone rotations only. An actor's bones are scaled (race weight scales x and y), so a world-space
    /// rotation written back as a full local matrix has shear, and the decomposition turned that shear into wild local
    /// scales that stayed in the bones from frame to frame (the exploded NPC mesh). The joint positions come from the
    /// full world matrices; the rotations are applied to the bones' scale-free rotation chain.
    struct LegWorld
    {
        vsg::mat4 mThigh; // hip joint
        vsg::mat4 mCalf; // knee joint
        vsg::mat4 mFoot; // ankle joint
    };

    struct LegSolution
    {
        bool mValid = false;
        vsg::vec3 mNewKnee;
        vsg::vec3 mNewAnkle;
        vsg::mat4 mHipRotation; // world rotation of the whole leg about the hip
        vsg::mat4 mKneeRotation; // further world rotation of the shin about the new knee
        vsg::mat4 mFootTilt; // world rotation of the foot to the ground normal, weighted
    };

    /// World rotations of the leg's bones with every scale left out (the product of the bones' rotations).
    struct LegRotations
    {
        vsg::mat4 mParent; // the thigh's parent
        vsg::mat4 mThigh;
        vsg::mat4 mCalf;
        vsg::mat4 mFoot;
    };

    /// Pure rotation matrices of the bones' new local rotations.
    struct LegLocalRotations
    {
        vsg::mat4 mThigh;
        vsg::mat4 mCalf;
        vsg::mat4 mFoot;
    };

    /// @param target where the ankle should go (world); @param pole bend direction hint for the knee;
    /// @param footNormal the (already clamped) ground normal the foot is tilted to; @param weight 0..1 IK blend.
    LegSolution solveLegPose(
        const LegWorld& leg, const vsg::vec3& target, const vsg::vec3& pole, const vsg::vec3& footNormal, float weight);

    /// The local rotations that give the thigh, calf and foot the solved world rotations. The foot keeps its animated
    /// world orientation, tilted.
    LegLocalRotations solveLocalRotations(const LegSolution& solution, const LegRotations& rotations);

    /// What the IK pass remembers about one bone it writes, so that it can undo its own write and never accumulates.
    /// Rotation and translation are tracked apart: an animation that rewrites only one of them must not leave the other
    /// to be taken for the animated value (the foot tilt would grow every frame).
    struct BoneBase
    {
        vsg::mat4 mBaseRotation; // the animated rotation of this frame
        vsg::mat4 mWrittenRotation; // the rotation the IK wrote, as read back
        vsg::vec3 mBaseTranslation;
        vsg::vec3 mWrittenTranslation;
        bool mRotationWritten = false;
        bool mTranslationWritten = false;
    };

    /// Undoes the IK's own write of the bone's rotation and translation if no animation wrote them again since;
    /// otherwise remembers what the animation wrote as the new base. Call at the start of every IK frame.
    void restoreBase(const BonePose& pose, const BonePose::Bone& bone, BoneBase& base);
    void markRotationWritten(const BonePose& pose, const BonePose::Bone& bone, BoneBase& base);
    void markTranslationWritten(const BonePose& pose, const BonePose::Bone& bone, BoneBase& base);

    /// Writes a solved leg: the thigh, calf and foot's local rotations only. Returns without a write if the solution is
    /// invalid.
    void applyLeg(const BonePose& pose, const BonePose::Bone& thigh, const BonePose::Bone& calf,
        const BonePose::Bone& foot, const LegSolution& solution);

    /// Rotates a bone by @a leanRot, a rotation in the actor's frame (@a actorRotation is the actor's world rotation),
    /// on top of its current local rotation. The bone's scale and translation stay.
    void applyLean(
        const BonePose& pose, const BonePose::Bone& bone, const vsg::dquat& actorRotation, const vsg::dquat& leanRot);
}

#endif
