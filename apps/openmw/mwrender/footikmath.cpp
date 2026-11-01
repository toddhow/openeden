#include "footikmath.hpp"

#include <opas/ik/TwoBoneIk.hpp>
#include <opas/math/Vec3.hpp>

namespace MWRender::FootIk
{
    namespace
    {
        vsg::mat4 inverseOf(const vsg::mat4& matrix)
        {
            vsg::mat4 result;
            Misc::VsgMath::invert(matrix, result);
            return result;
        }

        vsg::mat4 rotationMatrix(const vsg::dquat& rotation)
        {
            return vsg::mat4(Misc::VsgMath::toMatrix(rotation));
        }

        vsg::vec3 toVsg(const opas::math::Vec3& v)
        {
            return vsg::vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
        }

        opas::math::Vec3 toOpas(const vsg::vec3& v)
        {
            return { v.x, v.y, v.z };
        }
    }

    LegSolution solveLegPose(
        const LegWorld& leg, const vsg::vec3& target, const vsg::vec3& pole, const vsg::vec3& footNormal, float weight)
    {
        LegSolution result;

        const vsg::vec3 hip(Misc::VsgMath::getTrans(leg.mThigh));
        const vsg::vec3 knee(Misc::VsgMath::getTrans(leg.mCalf));
        const vsg::vec3 ankle(Misc::VsgMath::getTrans(leg.mFoot));

        const opas::ik::TwoBoneChain chain{ toOpas(hip), toOpas(knee), toOpas(ankle) };
        const opas::ik::HingeLimits limits{ 0.0, 2.8 };
        const opas::ik::TwoBoneResult ik = opas::ik::solveTwoBone(chain, toOpas(target), toOpas(pole), limits);
        if (!ik.valid)
            return result;

        result.mNewKnee = toVsg(ik.mid);
        result.mNewAnkle = toVsg(ik.end);

        // World-space joint rotations derived from CreativeMotion's solved joint positions.
        result.mHipRotation = rotationMatrix(Misc::VsgMath::makeRotate(knee - hip, result.mNewKnee - hip));
        const vsg::vec3 calfDirAfterHip = Misc::VsgMath::transform3x3(ankle - knee, result.mHipRotation);
        result.mKneeRotation
            = rotationMatrix(Misc::VsgMath::makeRotate(calfDirAfterHip, result.mNewAnkle - result.mNewKnee));

        const vsg::dquat tilt = Misc::VsgMath::makeRotate(vsg::vec3(0.f, 0.f, 1.f), footNormal);
        result.mFootTilt = rotationMatrix(Misc::VsgMath::slerp(weight, vsg::dquat(), tilt));
        result.mValid = true;
        return result;
    }

    LegLocalRotations solveLocalRotations(const LegSolution& solution, const LegRotations& rotations)
    {
        const vsg::mat4 newThigh = solution.mHipRotation * rotations.mThigh;
        const vsg::mat4 newCalf = solution.mKneeRotation * (solution.mHipRotation * rotations.mCalf);
        // The foot keeps its animated world orientation, then is tilted to the ground normal.
        const vsg::mat4 newFoot = solution.mFootTilt * rotations.mFoot;

        LegLocalRotations result;
        result.mThigh = inverseOf(rotations.mParent) * newThigh;
        result.mCalf = inverseOf(newThigh) * newCalf;
        result.mFoot = inverseOf(newCalf) * newFoot;
        return result;
    }

    void restoreBase(const BonePose& pose, const BonePose::Bone& bone, BoneBase& base)
    {
        if (!bone)
            return;

        const vsg::mat4 rotation = pose.getLocalRotation(bone);
        if (base.mRotationWritten && Misc::VsgMath::compareEqual(rotation, base.mWrittenRotation))
            pose.setLocalRotationMatrix(bone, base.mBaseRotation);
        else
            base.mBaseRotation = rotation;

        const vsg::vec3 translation = pose.getLocalTranslation(bone);
        if (base.mTranslationWritten && translation == base.mWrittenTranslation)
            pose.setLocalTranslation(bone, base.mBaseTranslation);
        else
            base.mBaseTranslation = translation;

        base.mRotationWritten = false;
        base.mTranslationWritten = false;
    }

    void markRotationWritten(const BonePose& pose, const BonePose::Bone& bone, BoneBase& base)
    {
        base.mWrittenRotation = pose.getLocalRotation(bone);
        base.mRotationWritten = true;
    }

    void markTranslationWritten(const BonePose& pose, const BonePose::Bone& bone, BoneBase& base)
    {
        base.mWrittenTranslation = pose.getLocalTranslation(bone);
        base.mTranslationWritten = true;
    }

    void applyLeg(const BonePose& pose, const BonePose::Bone& thigh, const BonePose::Bone& calf,
        const BonePose::Bone& foot, const LegSolution& solution)
    {
        if (!solution.mValid)
            return;
        const LegLocalRotations rotations = solveLocalRotations(solution,
            { pose.getParentWorldRotation(thigh), pose.getWorldRotation(thigh), pose.getWorldRotation(calf),
                pose.getWorldRotation(foot) });
        pose.setLocalRotationMatrix(thigh, rotations.mThigh);
        pose.setLocalRotationMatrix(calf, rotations.mCalf);
        pose.setLocalRotationMatrix(foot, rotations.mFoot);
    }

    void applyLean(
        const BonePose& pose, const BonePose::Bone& bone, const vsg::dquat& actorRotation, const vsg::dquat& leanRot)
    {
        const vsg::dquat parentRot = Misc::VsgMath::rotationOf(pose.getParentWorldRotation(bone));
        const vsg::dquat worldLean = actorRotation * leanRot * Misc::VsgMath::inverse(actorRotation);
        const vsg::dquat localLean = Misc::VsgMath::inverse(parentRot) * worldLean * parentRot;
        const vsg::dquat local = Misc::VsgMath::rotationOf(pose.getLocalRotation(bone));
        pose.setLocalRotationMatrix(bone, vsg::mat4(Misc::VsgMath::toMatrix(localLean * local)));
    }
}
