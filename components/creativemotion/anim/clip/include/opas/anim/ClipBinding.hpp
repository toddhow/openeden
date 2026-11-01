#pragma once

// A clip bound to a target skeleton through a skeleton profile (task 0020,
// stage C3a): samples the clip into the target's local pose and reports root
// motion. Binding resolves names and buffers once; sampling allocates nothing.
//
// Ownership: a plain value. It copies the clip it binds and keeps no pointer to
// the skeleton or profile. The spans it returns point into it and end when it
// moves or dies.

#include "opas/anim/Clip.hpp"
#include "opas/anim/SkeletonProfile.hpp"
#include "opas/math/Transform.hpp"
#include "opas/rig/Skeleton.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace opas::anim
{

    // Loop wraps time into [0, durationS); Clamp holds the clip's ends.
    enum class TimeMode
    {
        Clamp,
        Loop
    };

    // Root motion between two times: model space, rotated into the character's
    // frame at the start time (by -yaw there); translationM.z is always 0.
    struct RootMotionDelta
    {
        math::Vec3 translationM;
        double yawRad = 0.0;
    };

    class ClipBinding
    {
    public:
        // Binds a valid clip to a valid target skeleton through a valid profile.
        // Fails with a message if any of the three is invalid, or a mapping names a
        // target bone the skeleton lacks, or an ancestor of the root bone rotates:
        // in clip.sourceRest (a rest rotation or a rotation track) or in the target
        // (a rest rotation or a mapped rotation track). Allocates here only.
        static std::optional<ClipBinding> create(const Clip& clip, const rig::Skeleton& target,
            const SkeletonProfile& profile, std::string* error = nullptr);

        // Clip tracks whose bone the profile does not map (ignored when sampling).
        [[nodiscard]] std::span<const std::string> unmappedClipBones() const;
        // Target bones with no track; they keep their rest pose.
        [[nodiscard]] std::span<const std::size_t> untrackedTargetBones() const;
        // Leg-length ratio target / source applied to the root's translation; 1
        // without a legChain or without the clip's sourceRest.
        [[nodiscard]] double rootTranslationScale() const;
        // Bones in the target skeleton: the size sample() fills (task 0020, C3c).
        [[nodiscard]] std::size_t targetBoneCount() const { return targetRest_.size(); }

        // Fills localPose (one transform per target bone) at timeS. Untracked bones
        // get their rest pose. Tracked bones take the clip's local rotation; only
        // the target bone mapped from the clip's root bone takes the clip's
        // translation (scaled by rootTranslationScale), others keep the target's
        // rest translation. With a rootBone, its horizontal translation and yaw are
        // removed: it keeps the target's rest x and y and the clip's (scaled)
        // height, and its rotation becomes R(+Z, -yaw) * clip rotation. Allocates
        // nothing; does nothing if localPose has fewer entries than target bones.
        // A non-finite timeS samples time 0.
        void sample(double timeS, TimeMode mode, std::span<math::Transform> localPose) const;

        // The root's motion from fromS to toS (toS >= fromS), scaled like the pose.
        // Yaw is the twist about +Z of the root bone's model rotation, unwrapped.
        // With Loop, whole cycles in between add one cycle's motion (0 to
        // durationS) each, at a cost that does not grow with their number. Zero
        // without a rootBone or with a non-finite time. Allocates nothing.
        [[nodiscard]] RootMotionDelta rootMotion(double fromS, double toS, TimeMode mode) const;

        [[nodiscard]] const Clip& clip() const { return clip_; }

    private:
        ClipBinding() = default;

        // One bone on the path from the clip skeleton's top down to the root bone:
        // its track (or -1) and its source rest transform (identity if unknown).
        struct ChainLink
        {
            int track = -1;
            math::Transform rest;
        };

        // The root bone's model transform at a time already wrapped or clamped.
        [[nodiscard]] math::Transform rootModel(double timeS) const;
        // Unwrapped yaw at a wrapped or clamped time (continuous across +-pi).
        [[nodiscard]] double unwrappedYaw(double timeS) const;
        // Root motion over [a, b] within one cycle, in the frame at a.
        [[nodiscard]] RootMotionDelta segment(double aS, double bS) const;

        Clip clip_;
        std::vector<math::Transform> targetRest_; // target local rest pose
        std::vector<int> trackOfTarget_; // per target bone: clip track index or -1
        std::vector<std::string> unmappedClipBones_;
        std::vector<std::size_t> untrackedTargetBones_;
        int rootTrack_ = -1; // clip track of the root bone, or -1
        int rootTarget_ = -1; // target bone of the root, or -1
        std::vector<ChainLink> rootChain_; // clip skeleton top down to the root bone
        std::vector<double> yawKeyTimesS_; // times where the unwrapped yaw is stored
        std::vector<double> yawKeyRad_; // wrapped yaw at those times
        std::vector<double> yawKeyUnwrappedRad_;
        double rootScale_ = 1.0;
    };

} // namespace opas::anim
