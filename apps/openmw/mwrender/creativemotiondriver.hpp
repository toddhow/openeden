#ifndef GAME_RENDER_CREATIVEMOTIONDRIVER_H
#define GAME_RENDER_CREATIVEMOTIONDRIVER_H

#include <components/misc/constants.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/nifosg/matrixtransform.hpp>
#include <components/sceneutil/keyframe.hpp>
#include <components/sceneutil/nodecallback.hpp>
#include <osg/MatrixTransform>

#include <opas/anim/Clip.hpp>
#include <opas/anim/ClipBinding.hpp>
#include <opas/anim/Inertialization.hpp>
#include <opas/anim/PoseBlend.hpp>
#include <opas/math/Transform.hpp>
#include <opas/rig/Skeleton.hpp>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "blendmask.hpp"

namespace MWRender
{
    class CreativeMotionPoseDriver;

    /// Lightweight callback attached to each animated bone node that copies
    /// the precalculated transform from CreativeMotionPoseDriver.
    class CreativeMotionPoseWriterCallback : public SceneUtil::NodeCallback<CreativeMotionPoseWriterCallback>
    {
    public:
        CreativeMotionPoseWriterCallback(CreativeMotionPoseDriver* driver, size_t boneIndex)
            : mDriver(driver)
            , mBoneIndex(boneIndex)
        {
        }

        void operator()(osg::Node* node, osg::NodeVisitor* nv);

    private:
        CreativeMotionPoseDriver* mDriver;
        size_t mBoneIndex;
    };

    class CreativeMotionPoseDriver
    {
    public:
        using NodeMap = std::unordered_map<std::string, osg::ref_ptr<osg::MatrixTransform>,
            Misc::StringUtils::CiHash, Misc::StringUtils::CiEqual>;

        CreativeMotionPoseDriver(const NodeMap& nodeMap, osg::Node* objectRoot);
        ~CreativeMotionPoseDriver();

        bool isValid() const { return mValid; }

        /// Converts and binds a KeyframeHolder into an internal opas::anim::Clip
        bool registerKeyframes(const SceneUtil::KeyframeHolder* holder);

        /// Notification that an active state changed on a blend mask, triggering inertialization.
        void onActiveStateChanged(size_t blendMask, std::string_view groupname,
            const SceneUtil::KeyframeHolder* newHolder, float newTime, float transitionDuration);

        /// Advance inertialization and layer the active poses for all 4 blend masks.
        void update(float duration, const std::array<std::shared_ptr<float>, sNumBlendMasks>& timePtrs,
            const std::array<const SceneUtil::KeyframeHolder*, sNumBlendMasks>& activeHolders);

        const osg::Vec3f& getBoneTranslation(size_t boneIndex) const { return mBoneTranslations[boneIndex]; }
        const osg::Quat& getBoneRotation(size_t boneIndex) const { return mBoneRotations[boneIndex]; }

        int getBoneIndex(const std::string& lowerBoneName) const
        {
            auto it = mBoneNameToIndex.find(lowerBoneName);
            return (it != mBoneNameToIndex.end()) ? it->second : -1;
        }

        const opas::rig::Skeleton& getSkeleton() const { return mSkeleton; }

        /// Developer comparison mode: calculates largest difference between driver output and node transforms.
        bool compareWithCurrentPose(float& outMaxTransDiff, float& outMaxRotDiff, std::string& outMaxDiffBone) const;

    private:
        bool buildSkeleton(const NodeMap& nodeMap, osg::Node* objectRoot);
        void buildBoneMasks();

        struct BoundClipInfo
        {
            opas::anim::Clip mClip;
            std::vector<int> mTrackToSkeletonBone;
        };

        void sampleClipIntoPose(const BoundClipInfo& clipInfo, float timeS,
            std::span<opas::math::Transform> outPose) const;

        bool mValid = false;

        opas::rig::Skeleton mSkeleton;
        std::unordered_map<std::string, int> mBoneNameToIndex;
        std::vector<NifOsg::MatrixTransform*> mBoneNodes;

        // Preallocated pose buffers per mask and for layering
        std::vector<opas::math::Transform> mRestPose;
        std::array<std::vector<opas::math::Transform>, sNumBlendMasks> mMaskPoses;
        std::array<std::vector<opas::math::Transform>, sNumBlendMasks> mMaskPrevPoses;
        std::vector<opas::math::Transform> mLayeredPose;

        // Output engine-space transforms for writer callbacks
        std::vector<osg::Vec3f> mBoneTranslations;
        std::vector<osg::Quat> mBoneRotations;

        // Inertialization and blending masks
        std::array<opas::anim::BoneMask, sNumBlendMasks> mBoneMasks;
        std::array<std::unique_ptr<opas::anim::Inertializer>, sNumBlendMasks> mInertializers;

        // Clip cache per KeyframeHolder
        std::unordered_map<const SceneUtil::KeyframeHolder*, BoundClipInfo> mClipCache;

        // Active state tracking per mask
        std::array<const SceneUtil::KeyframeHolder*, sNumBlendMasks> mLastHolders = { nullptr, nullptr, nullptr, nullptr };
        std::array<std::string, sNumBlendMasks> mLastGroupNames;
    };
}

#endif
