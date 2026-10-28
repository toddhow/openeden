#include "creativemotiondriver.hpp"

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/nifosg/controller.hpp>
#include <components/nifosg/matrixtransform.hpp>

#include <opas/anim/Interpolate.hpp>

#include <osg/NodeVisitor>

#include <algorithm>
#include <cmath>
#include <queue>

namespace MWRender
{
    namespace
    {
        opas::math::Vec3 lerpVec3(opas::math::Vec3 a, opas::math::Vec3 b, double t)
        {
            return a + (b - a) * t;
        }

        struct KeyPos
        {
            size_t index = 0;
            double fraction = 0.0;
            bool atEnd = true;
        };

        KeyPos locateKey(const std::vector<double>& times, double t)
        {
            if (times.empty() || !(t > times.front()))
                return { 0, 0.0, true };
            if (t >= times.back())
                return { times.size() - 1, 0.0, true };
            auto next = std::upper_bound(times.begin(), times.end(), t);
            size_t i = static_cast<size_t>(std::distance(times.begin(), next) - 1);
            return { i, (t - times[i]) / (times[i + 1] - times[i]), false };
        }

        opas::math::Vec3 sampleVec3(const opas::anim::Vec3Track& track, double t)
        {
            KeyPos k = locateKey(track.timesS, t);
            if (k.atEnd || track.interpolation == opas::anim::Interpolation::Step)
                return track.values[k.index];
            return lerpVec3(track.values[k.index], track.values[k.index + 1], k.fraction);
        }

        opas::math::Quat sampleQuat(const opas::anim::QuatTrack& track, double t)
        {
            KeyPos k = locateKey(track.timesS, t);
            if (k.atEnd || track.interpolation == opas::anim::Interpolation::Step)
                return opas::math::normalized(track.values[k.index]);
            return opas::anim::slerp(track.values[k.index], track.values[k.index + 1], k.fraction);
        }
    }

    void CreativeMotionPoseWriterCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        auto* transform = static_cast<NifOsg::MatrixTransform*>(node);
        transform->setTranslation(mDriver->getBoneTranslation(mBoneIndex));
        transform->setRotation(mDriver->getBoneRotation(mBoneIndex));
        traverse(node, nv);
    }

    CreativeMotionPoseDriver::CreativeMotionPoseDriver(const NodeMap& nodeMap, osg::Node* objectRoot)
    {
        if (buildSkeleton(nodeMap, objectRoot))
        {
            buildBoneMasks();
            mValid = true;
        }
    }

    CreativeMotionPoseDriver::~CreativeMotionPoseDriver() = default;

    bool CreativeMotionPoseDriver::buildSkeleton(const NodeMap& nodeMap, osg::Node* objectRoot)
    {
        if (!objectRoot || nodeMap.empty())
            return false;

        // Traverse hierarchy breadth-first / parent-first from objectRoot to discover all bones in ancestor order.
        struct NodeEntry
        {
            osg::Node* node;
            int parentBoneIndex;
        };

        std::queue<NodeEntry> queue;
        queue.push({ objectRoot, -1 });

        while (!queue.empty())
        {
            NodeEntry entry = queue.front();
            queue.pop();

            osg::Node* current = entry.node;
            int parentIdx = entry.parentBoneIndex;

            auto* nifTransform = dynamic_cast<NifOsg::MatrixTransform*>(current);
            if (nifTransform)
            {
                std::string lowerName = Misc::StringUtils::lowerCase(current->getName());
                if (!lowerName.empty() && mBoneNameToIndex.find(lowerName) == mBoneNameToIndex.end())
                {
                    auto found = nodeMap.find(lowerName);
                    if (found != nodeMap.end() && found->second == current)
                    {
                        int myIdx = static_cast<int>(mSkeleton.bones.size());
                        opas::rig::Bone bone;
                        bone.name = lowerName;
                        bone.parent = parentIdx;

                        osg::Vec3f trans = nifTransform->getMatrix().getTrans();
                        osg::Quat rot = nifTransform->getMatrix().getRotate();
                        if (rot.x() == 0.0f && rot.y() == 0.0f && rot.z() == 0.0f && rot.w() == 0.0f)
                            rot.set(0.0f, 0.0f, 0.0f, 1.0f);

                        const float invScale = 1.0f / Constants::UnitsPerMeter;
                        bone.localRest.translation = { trans.x() * invScale, trans.y() * invScale, trans.z() * invScale };
                        bone.localRest.rotation = opas::math::normalized({ rot.x(), rot.y(), rot.z(), rot.w() });

                        mSkeleton.bones.push_back(bone);
                        mBoneNameToIndex[lowerName] = myIdx;
                        mBoneNodes.push_back(nifTransform);

                        parentIdx = myIdx;
                    }
                }
            }

            auto* group = current->asGroup();
            if (group)
            {
                for (unsigned int i = 0; i < group->getNumChildren(); ++i)
                    queue.push({ group->getChild(i), parentIdx });
            }
        }

        if (mSkeleton.bones.empty())
            return false;

        std::string valErr = opas::rig::validateSkeleton(mSkeleton);
        if (!valErr.empty())
        {
            Log(Debug::Warning) << "CreativeMotionPoseDriver: skeleton validation failed: " << valErr;
            return false;
        }

        const size_t numBones = mSkeleton.bones.size();
        mRestPose.resize(numBones);
        for (size_t i = 0; i < numBones; ++i)
            mRestPose[i] = mSkeleton.bones[i].localRest;

        for (size_t i = 0; i < sNumBlendMasks; ++i)
        {
            mMaskPoses[i] = mRestPose;
            mMaskPrevPoses[i] = mRestPose;
            mInertializers[i] = std::make_unique<opas::anim::Inertializer>(numBones);
        }

        mLayeredPose = mRestPose;
        mBoneTranslations.resize(numBones);
        mBoneRotations.resize(numBones);

        for (size_t i = 0; i < numBones; ++i)
        {
            mBoneTranslations[i] = mBoneNodes[i]->getMatrix().getTrans();
            mBoneRotations[i] = mBoneNodes[i]->getMatrix().getRotate();
        }

        return true;
    }

    void CreativeMotionPoseDriver::buildBoneMasks()
    {
        const size_t numBones = mSkeleton.bones.size();

        // Mask 0: Lower body / all bones
        mBoneMasks[0].weights.assign(numBones, 1.0);

        static const std::string_view sMaskRootNames[sNumBlendMasks] = {
            "",
            "bip01 spine1",
            "bip01 l clavicle",
            "bip01 r clavicle"
        };

        for (size_t m = 1; m < sNumBlendMasks; ++m)
        {
            std::string rootName(sMaskRootNames[m]);
            auto maskOpt = opas::anim::makeBoneMask(mSkeleton, std::span<const std::string>(&rootName, 1), true, 1.0);
            if (maskOpt.has_value())
                mBoneMasks[m] = *maskOpt;
            else
            {
                // Root bone not found on this skeleton, set all-zero weights
                mBoneMasks[m].weights.assign(numBones, 0.0);
            }
        }
    }

    bool CreativeMotionPoseDriver::registerKeyframes(const SceneUtil::KeyframeHolder* holder)
    {
        if (!mValid || !holder)
            return false;

        if (mClipCache.find(holder) != mClipCache.end())
            return true;

        const auto& ctrlMap = holder->mKeyframeControllers;
        if (ctrlMap.empty())
            return false;

        // Determine duration across controllers and text keys
        float maxTime = 0.0f;
        for (const auto& [time, text] : holder->mTextKeys)
            maxTime = std::max(maxTime, time);

        for (const auto& [boneName, ctrl] : ctrlMap)
        {
            const auto* nifCtrl = dynamic_cast<const NifOsg::KeyframeController*>(ctrl.get());
            if (nifCtrl)
            {
                const auto& trans = nifCtrl->getTranslationInterpolator();
                if (!trans.empty() && trans.getKeys())
                    maxTime = std::max(maxTime, trans.getKeys()->mKeys.rbegin()->first);

                const auto& rots = nifCtrl->getRotationInterpolator();
                if (!rots.empty() && rots.getKeys())
                    maxTime = std::max(maxTime, rots.getKeys()->mKeys.rbegin()->first);
            }
        }

        if (maxTime <= 0.0f)
            maxTime = 0.1f;

        BoundClipInfo clipInfo;
        clipInfo.mClip.name = "source_clip";
        clipInfo.mClip.durationS = maxTime;

        const float invScale = 1.0f / Constants::UnitsPerMeter;
        constexpr float sampleDt = 1.0f / 60.0f;

        for (const auto& [boneName, ctrl] : ctrlMap)
        {
            std::string lower = Misc::StringUtils::lowerCase(boneName);
            auto it = mBoneNameToIndex.find(lower);
            if (it == mBoneNameToIndex.end())
                continue;

            int boneIdx = it->second;
            const auto* nifCtrl = dynamic_cast<const NifOsg::KeyframeController*>(ctrl.get());
            if (!nifCtrl)
                continue;

            std::vector<double> sampleTimes;
            for (float t = 0.0f; t <= maxTime; t += sampleDt)
                sampleTimes.push_back(t);
            if (sampleTimes.empty() || sampleTimes.back() < maxTime)
                sampleTimes.push_back(maxTime);

            opas::anim::BoneTrack track;
            track.bone = lower;

            opas::anim::Vec3Track vTrack;
            opas::anim::QuatTrack qTrack;

            vTrack.timesS = sampleTimes;
            qTrack.timesS = sampleTimes;

            vTrack.values.reserve(sampleTimes.size());
            qTrack.values.reserve(sampleTimes.size());

            for (double t : sampleTimes)
            {
                auto xf = nifCtrl->getTransform(static_cast<float>(t));
                osg::Vec3f pos = xf.mTranslation.value_or(mBoneNodes[boneIdx]->getMatrix().getTrans());
                osg::Quat rot = xf.mRotation.value_or(mBoneNodes[boneIdx]->getMatrix().getRotate());
                if (rot.x() == 0.0f && rot.y() == 0.0f && rot.z() == 0.0f && rot.w() == 0.0f)
                    rot.set(0.0f, 0.0f, 0.0f, 1.0f);

                vTrack.values.push_back({ pos.x() * invScale, pos.y() * invScale, pos.z() * invScale });
                qTrack.values.push_back(opas::math::normalized({ rot.x(), rot.y(), rot.z(), rot.w() }));
            }

            track.translation = std::move(vTrack);
            track.rotation = std::move(qTrack);

            clipInfo.mClip.tracks.push_back(std::move(track));
            clipInfo.mTrackToSkeletonBone.push_back(boneIdx);
        }

        mClipCache[holder] = std::move(clipInfo);
        return true;
    }

    void CreativeMotionPoseDriver::sampleClipIntoPose(const BoundClipInfo& clipInfo, float timeS,
        std::span<opas::math::Transform> outPose) const
    {
        std::copy(mRestPose.begin(), mRestPose.end(), outPose.begin());

        for (size_t t = 0; t < clipInfo.mClip.tracks.size(); ++t)
        {
            int boneIdx = clipInfo.mTrackToSkeletonBone[t];
            if (boneIdx < 0 || static_cast<size_t>(boneIdx) >= outPose.size())
                continue;

            const auto& track = clipInfo.mClip.tracks[t];
            if (track.rotation.has_value())
                outPose[boneIdx].rotation = sampleQuat(*track.rotation, timeS);
            if (track.translation.has_value())
                outPose[boneIdx].translation = sampleVec3(*track.translation, timeS);
        }
    }

    void CreativeMotionPoseDriver::onActiveStateChanged(size_t blendMask, std::string_view groupname,
        const SceneUtil::KeyframeHolder* newHolder, float newTime, float transitionDuration)
    {
        if (!mValid || blendMask >= sNumBlendMasks)
            return;

        if (newHolder == mLastHolders[blendMask] && groupname == mLastGroupNames[blendMask])
            return;

        if (transitionDuration > 0.001f && (!mLastGroupNames[blendMask].empty() || mLastHolders[blendMask] != nullptr))
        {
            std::vector<opas::math::Transform> incoming(mSkeleton.bones.size());
            if (newHolder)
            {
                registerKeyframes(newHolder);
                auto it = mClipCache.find(newHolder);
                if (it != mClipCache.end())
                    sampleClipIntoPose(it->second, newTime, incoming);
                else
                    incoming = mRestPose;
            }
            else
                incoming = mRestPose;

            mInertializers[blendMask]->start(
                mMaskPrevPoses[blendMask],
                mMaskPoses[blendMask],
                incoming,
                0.016,
                static_cast<double>(transitionDuration));
        }

        mLastHolders[blendMask] = newHolder;
        mLastGroupNames[blendMask] = groupname;
    }

    void CreativeMotionPoseDriver::update(float duration,
        const std::array<std::shared_ptr<float>, sNumBlendMasks>& timePtrs,
        const std::array<const SceneUtil::KeyframeHolder*, sNumBlendMasks>& activeHolders)
    {
        if (!mValid)
            return;

        const size_t numBones = mSkeleton.bones.size();

        for (size_t m = 0; m < sNumBlendMasks; ++m)
        {
            mMaskPrevPoses[m] = mMaskPoses[m];

            auto holder = activeHolders[m];
            if (holder && timePtrs[m])
            {
                registerKeyframes(holder);
                auto it = mClipCache.find(holder);
                if (it != mClipCache.end())
                {
                    float timeS = *timePtrs[m];
                    sampleClipIntoPose(it->second, timeS, mMaskPoses[m]);
                }
                else
                    mMaskPoses[m] = mRestPose;
            }
            else
                mMaskPoses[m] = mRestPose;

            if (mInertializers[m]->active())
                mInertializers[m]->advanceAndApply(static_cast<double>(duration), mMaskPoses[m]);
        }

        // Layer the masks
        mLayeredPose = mMaskPoses[0];

        for (size_t m = 1; m < sNumBlendMasks; ++m)
        {
            if (activeHolders[m])
            {
                opas::anim::applyMaskedLayer(mLayeredPose, mMaskPoses[m], mBoneMasks[m], mLayeredPose);
            }
        }

        // Convert layered pose to engine-space translations and rotations
        const float unitsPerMeter = Constants::UnitsPerMeter;
        for (size_t i = 0; i < numBones; ++i)
        {
            const auto& t = mLayeredPose[i];
            mBoneTranslations[i] = osg::Vec3f(
                static_cast<float>(t.translation.x * unitsPerMeter),
                static_cast<float>(t.translation.y * unitsPerMeter),
                static_cast<float>(t.translation.z * unitsPerMeter));

            mBoneRotations[i] = osg::Quat(
                static_cast<float>(t.rotation.x),
                static_cast<float>(t.rotation.y),
                static_cast<float>(t.rotation.z),
                static_cast<float>(t.rotation.w));
        }
    }

    bool CreativeMotionPoseDriver::compareWithCurrentPose(float& outMaxTransDiff, float& outMaxRotDiff,
        std::string& outMaxDiffBone) const
    {
        if (!mValid || mBoneNodes.empty())
            return false;

        outMaxTransDiff = 0.0f;
        outMaxRotDiff = 0.0f;
        outMaxDiffBone.clear();

        const float invUnitsPerMeter = 1.0f / Constants::UnitsPerMeter;
        for (size_t i = 0; i < mBoneNodes.size(); ++i)
        {
            if (!mBoneNodes[i])
                continue;

            osg::Vec3f nodeTrans = mBoneNodes[i]->getMatrix().getTrans();
            osg::Quat nodeRot = mBoneNodes[i]->getMatrix().getRotate();

            float transDiff = (nodeTrans - mBoneTranslations[i]).length() * invUnitsPerMeter;

            double dot = std::abs(nodeRot.x() * mBoneRotations[i].x() +
                                  nodeRot.y() * mBoneRotations[i].y() +
                                  nodeRot.z() * mBoneRotations[i].z() +
                                  nodeRot.w() * mBoneRotations[i].w());
            dot = std::clamp(dot, 0.0, 1.0);
            float rotDiff = static_cast<float>(2.0 * std::acos(dot));

            if (transDiff > outMaxTransDiff || rotDiff > outMaxRotDiff)
            {
                outMaxTransDiff = std::max(outMaxTransDiff, transDiff);
                outMaxRotDiff = std::max(outMaxRotDiff, rotDiff);
                outMaxDiffBone = mSkeleton.bones[i].name;
            }
        }

        return true;
    }
}
