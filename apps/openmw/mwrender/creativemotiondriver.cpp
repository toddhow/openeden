#include "creativemotiondriver.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/animset.hpp>
#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/gameconventions.hpp>
#include <components/misc/strings/lower.hpp>

#include <opas/anim/Interpolate.hpp>

#include <vsg/maths/transform.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <set>
#include <type_traits>
#include <unordered_map>

namespace MWRender
{
    namespace
    {
        // Longest group name kept without allocating when the active group changes (Morrowind's are shorter).
        constexpr std::size_t sGroupNameCapacity = 64;
        opas::math::Vec3 toMetres(const vsg::vec3& v)
        {
            // The length unit is the game's convention (task 0085, move 1).
            const double invScale = 1.0 / Misc::gameConventions().mUnitsPerMeter;
            return { v.x * invScale, v.y * invScale, v.z * invScale };
        }

        opas::math::Quat toOpas(const vsg::quat& q)
        {
            opas::math::Quat result{ q.x, q.y, q.z, q.w };
            if (result.x == 0.0 && result.y == 0.0 && result.z == 0.0 && result.w == 0.0)
                return { 0.0, 0.0, 0.0, 1.0 };
            return opas::math::normalized(result);
        }

        opas::math::Transform localTransform(const Anim::Transform& transform)
        {
            vsg::vec3 translation;
            vsg::quat rotation;
            vsg::vec3 scale;
            vsg::decompose(transform.t_transform(vsg::mat4()), translation, rotation, scale);
            return { toMetres(transform.translation), toOpas(rotation) };
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
            const opas::math::Vec3& a = track.values[k.index];
            const opas::math::Vec3& b = track.values[k.index + 1];
            return a + (b - a) * k.fraction;
        }

        opas::math::Quat sampleQuat(const opas::anim::QuatTrack& track, double t)
        {
            KeyPos k = locateKey(track.timesS, t);
            if (k.atEnd || track.interpolation == opas::anim::Interpolation::Step)
                return track.values[k.index];
            return opas::anim::slerp(track.values[k.index], track.values[k.index + 1], k.fraction);
        }
    }

    std::string describeBoneRotations(Anim::Bones& bones, const std::vector<std::string_view>& normalizedNames)
    {
        std::string text;
        char buffer[96];
        for (const std::string_view name : normalizedNames)
        {
            Anim::Bone* bone = bones.search(std::string(name));
            if (bone == nullptr || bone->path.empty())
                continue;
            vsg::vec3 translation;
            vsg::quat rotation;
            vsg::vec3 scale;
            vsg::decompose(bone->node()->t_transform(vsg::mat4()), translation, rotation, scale);
            const int length = std::snprintf(buffer, sizeof(buffer), " %.*s=(%.4f %.4f %.4f %.4f)",
                static_cast<int>(name.size()), name.data(), rotation.x, rotation.y, rotation.z, rotation.w);
            if (length > 0)
                text.append(buffer, std::min<std::size_t>(static_cast<std::size_t>(length), sizeof(buffer) - 1));
        }
        return text;
    }

    CreativeMotionPoseDriver::CreativeMotionPoseDriver(Anim::Bones& bones)
    {
        for (auto& name : mLastGroupNames)
            name.reserve(sGroupNameCapacity);
        for (auto& name : mPreviousGroupNames)
            name.reserve(sGroupNameCapacity);
        mValid = buildSkeleton(bones);
    }

    CreativeMotionPoseDriver::~CreativeMotionPoseDriver() = default;

    std::optional<RigBones> buildRigBones(Anim::Bones& bones)
    {
        struct Entry
        {
            std::string mName;
            Anim::Bone* mBone;
        };
        std::vector<Entry> entries;
        std::unordered_map<const Anim::Transform*, std::string> namesByNode;
        bones.forEach([&](const std::string& name, Anim::Bone& bone) {
            if (name.empty() || bone.path.empty())
                return;
            entries.push_back({ name, &bone });
            namesByNode.emplace(bone.node(), name);
        });
        if (entries.empty())
            return std::nullopt;

        // Parents first: by depth, then by name, so the order does not depend on the bones' hash map.
        std::sort(entries.begin(), entries.end(), [](const Entry& lhs, const Entry& rhs) {
            if (lhs.mBone->path.size() != rhs.mBone->path.size())
                return lhs.mBone->path.size() < rhs.mBone->path.size();
            return lhs.mName < rhs.mName;
        });

        RigBones result;
        std::map<std::string, int> indexByName;
        for (const Entry& entry : entries)
        {
            opas::rig::Bone bone;
            bone.name = entry.mName;
            bone.parent = -1;
            const Anim::BonePath& path = entry.mBone->path;
            for (std::size_t i = path.size() - 1; i-- > 0;)
            {
                const auto found = namesByNode.find(path[i].get());
                if (found != namesByNode.end())
                {
                    bone.parent = indexByName.at(found->second);
                    break;
                }
            }
            Anim::Transform* node = entry.mBone->node();
            bone.localRest = localTransform(*node);
            indexByName.emplace(entry.mName, static_cast<int>(result.mSkeleton.bones.size()));
            result.mSkeleton.bones.push_back(std::move(bone));
            result.mNodes.push_back(node);
            result.mGroups.push_back(entry.mBone->group);
        }

        const std::string error = opas::rig::validateSkeleton(result.mSkeleton);
        if (!error.empty())
        {
            Log(Debug::Warning) << "CreativeMotion: skeleton validation failed: " << error;
            return std::nullopt;
        }
        return result;
    }

    bool CreativeMotionPoseDriver::buildSkeleton(Anim::Bones& bones)
    {
        std::optional<RigBones> rig = buildRigBones(bones);
        if (!rig)
            return false;
        mSkeleton = std::move(rig->mSkeleton);
        mBoneNodes = std::move(rig->mNodes);
        mBoneGroups = std::move(rig->mGroups);
        for (std::size_t i = 0; i < mSkeleton.bones.size(); ++i)
            mBoneNameToIndex.emplace(mSkeleton.bones[i].name, static_cast<int>(i));

        // The root bone names are the game's conventions (task 0085, move 1).
        const Misc::GameConventions& game = Misc::gameConventions();
        // An empty name means the game has no such bone (a game may say "none"); never look it up.
        const int rootIndex = game.mRootBone.empty() ? -1 : getBoneIndex(game.mRootBone);
        if (rootIndex >= 0)
            mAccumRoot = rootIndex;
        else if (!game.mRootBoneFallback.empty())
            mAccumRoot = getBoneIndex(game.mRootBoneFallback);

        const std::size_t numBones = mSkeleton.bones.size();
        mRestPose.resize(numBones);
        for (std::size_t i = 0; i < numBones; ++i)
            mRestPose[i] = mSkeleton.bones[i].localRest;
        for (std::size_t g = 0; g < sNumBlendMasks; ++g)
        {
            mGroupPoses[g] = mRestPose;
            mGroupPrevPoses[g] = mRestPose;
            mInertializers[g] = std::make_unique<opas::anim::Inertializer>(numBones);
        }
        mIncoming = mRestPose;
        mStyleLayer = std::make_unique<CreativeMotionStyleLayer>(mSkeleton);
        mBoneTranslations.resize(numBones);
        mBoneRotations.resize(numBones);
        mAnimated.assign(numBones, 0);
        for (std::size_t i = 0; i < numBones; ++i)
        {
            mBoneTranslations[i] = mBoneNodes[i]->translation;
            const auto& r = mRestPose[i].rotation;
            mBoneRotations[i] = vsg::quat(
                static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.z), static_cast<float>(r.w));
        }
        return true;
    }

    void CreativeMotionPoseDriver::setRecordTargets(bool enabled)
    {
        mRecordTargets = enabled && mValid;
        if (!mRecordTargets)
            return;
        for (std::size_t g = 0; g < sNumBlendMasks; ++g)
            mGroupTargets[g] = mRestPose;
    }

    std::string CreativeMotionPoseDriver::describeTargetRotations(
        const std::vector<std::string_view>& normalizedNames) const
    {
        if (!mRecordTargets)
            return {};
        std::string text = " tgt=[";
        bool first = true;
        char buffer[96];
        for (const std::string_view name : normalizedNames)
        {
            const int bone = getBoneIndex(name);
            if (bone < 0 || mAnimated[bone] == 0)
                continue;
            const uint32_t group = mBoneGroups[bone] < sNumBlendMasks ? mBoneGroups[bone] : 0;
            const opas::math::Quat& r = mGroupTargets[group][bone].rotation;
            const int length = std::snprintf(buffer, sizeof(buffer), "%s%.*s=(%.4f %.4f %.4f %.4f)", first ? "" : ";",
                static_cast<int>(name.size()), name.data(), static_cast<float>(r.x), static_cast<float>(r.y),
                static_cast<float>(r.z), static_cast<float>(r.w));
            if (length > 0)
                text.append(buffer, std::min<std::size_t>(static_cast<std::size_t>(length), sizeof(buffer) - 1));
            first = false;
        }
        text += "]";
        return text;
    }

    int CreativeMotionPoseDriver::getBoneIndex(std::string_view normalizedName) const
    {
        const auto it = mBoneNameToIndex.find(normalizedName);
        return it != mBoneNameToIndex.end() ? it->second : -1;
    }

    bool CreativeMotionPoseDriver::registerSource(const Anim::ControllerMap& source)
    {
        if (!mValid)
            return false;
        if (const auto it = mClips.find(&source); it != mClips.end())
            return it->second.mClip != nullptr;
        return bind(source, buildAnimClip(source));
    }

    bool CreativeMotionPoseDriver::registerSource(const Anim::AnimSet& set)
    {
        if (!mValid || set.controllers == nullptr)
            return false;
        if (const auto it = mClips.find(set.controllers.get()); it != mClips.end())
            return it->second.mClip != nullptr;
        return bind(*set.controllers, animClipOf(set));
    }

    bool CreativeMotionPoseDriver::bind(const Anim::ControllerMap& source, std::shared_ptr<const AnimClip> clip)
    {
        const vsg::ref_ptr<const Anim::ControllerMap> keepAlive(&source);
        auto bound = std::make_unique<BoundClip>();
        if (clip != nullptr)
        {
            bound->mHasTrack.assign(mSkeleton.bones.size(), 0);
            for (std::size_t t = 0; t < clip->mClip.tracks.size(); ++t)
            {
                // A track of a bone this skeleton does not have is skipped.
                const int bone = getBoneIndex(clip->mClip.tracks[t].bone);
                bound->mTrackToBone.push_back(bone);
                if (bone >= 0)
                    bound->mHasTrack[bone] = 1;
            }
            if (std::none_of(bound->mTrackToBone.begin(), bound->mTrackToBone.end(), [](int b) { return b >= 0; }))
                clip = nullptr;
        }
        if (clip == nullptr)
        {
            mClips.emplace(&source, CachedClip{ keepAlive, nullptr });
            return false;
        }
        bound->mShared = std::move(clip);
        mClips.emplace(&source, CachedClip{ keepAlive, std::move(bound) });
        return true;
    }

    const CreativeMotionPoseDriver::BoundClip* CreativeMotionPoseDriver::findClip(
        const Anim::ControllerMap* source) const
    {
        if (source == nullptr)
            return nullptr;
        const auto it = mClips.find(source);
        return it != mClips.end() ? it->second.mClip.get() : nullptr;
    }

    void CreativeMotionPoseDriver::sampleClip(
        const BoundClip& clip, float timeS, std::vector<opas::math::Transform>& outPose) const
    {
        std::copy(mRestPose.begin(), mRestPose.end(), outPose.begin());
        for (std::size_t t = 0; t < clip.clip().tracks.size(); ++t)
        {
            const int bone = clip.mTrackToBone[t];
            if (bone < 0)
                continue;
            const auto& track = clip.clip().tracks[t];
            if (track.rotation.has_value())
                outPose[bone].rotation = sampleQuat(*track.rotation, timeS);
            if (track.translation.has_value())
                outPose[bone].translation = sampleVec3(*track.translation, timeS);
        }
        // The accumulation root's accumulated movement goes to the physics system, not the pose (MWAnim::Play).
        if (mAccumRoot >= 0 && clip.mHasTrack[mAccumRoot])
        {
            opas::math::Vec3& translation = outPose[mAccumRoot].translation;
            if (mAccumulate.x != 0.f)
                translation.x = 0.0;
            if (mAccumulate.y != 0.f)
                translation.y = 0.0;
            if (mAccumulate.z != 0.f)
                translation.z = 0.0;
        }
    }

    void CreativeMotionPoseDriver::update(
        float duration, const std::array<CreativeMotionActiveSource, sNumBlendMasks>& active, float transitionDuration)
    {
        if (!mValid)
            return;

        // The style layer: the effective profile for this frame, and whether the style itself changed (a new style,
        // or the layer switched on or off), which blends like a change of animation.
        const opas::style::StyleProfile* style = (mStyleEnabled && mStyleLayer != nullptr) ? mStyleProfile : nullptr;
        if (style != nullptr)
        {
            mStyleTimeS += static_cast<double>(duration);
            makeEffectiveProfile(*style, styleWeights(mActorState), mStyleSeed, mEffectiveStyle);
        }
        const bool styleChanged = style != mLastStyleApplied;
        mLastStyleApplied = style;

        for (std::size_t g = 0; g < sNumBlendMasks; ++g)
        {
            const CreativeMotionActiveSource& current = active[g];
            if (current.mSource != nullptr && mClips.find(current.mSource) == mClips.end())
            {
                if (current.mSet != nullptr)
                    registerSource(*current.mSet);
                else
                    registerSource(*current.mSource);
            }
            const BoundClip* clip = findClip(current.mSource);

            if (clip != nullptr)
                sampleClip(*clip, current.mTime, mIncoming);
            else
                std::copy(mRestPose.begin(), mRestPose.end(), mIncoming.begin());

            if (style != nullptr)
                mStyleLayer->apply(mIncoming, mEffectiveStyle, mStyleTimeS);

            const bool groupChanged = current.mSource != mLastSources[g] || current.mGroupName != mLastGroupNames[g];
            const bool changed = groupChanged || styleChanged;
            const bool playedBefore = mLastSources[g] != nullptr;
            float thisTransition = transitionDuration;
            if (groupChanged && mTransitionRules)
            {
                const MotionKind fromKind = classifyGroup(mLastGroupNames[g]);
                const MotionKind toKind = classifyGroup(current.mGroupName);
                if (mIntentTransitions)
                {
                    const TransitionIntent intent{ mActorState.mStarting, mActorState.mStopping,
                        mActorState.mTurnRate };
                    thisTransition = transitionSeconds(fromKind, toKind, transitionDuration, intent, mTransitionTimes);
                }
                else
                    thisTransition = transitionSeconds(fromKind, toKind, transitionDuration);
            }
            if (changed && playedBefore && thisTransition > 0.f)
            {
                // From what this group showed in the last two frames (with any running transition) to the new
                // animation: right after the switch the output is last frame's pose, keeping its velocity.
                mInertializers[g]->start(mGroupPrevPoses[g], mGroupPoses[g], mIncoming, static_cast<double>(duration),
                    static_cast<double>(thisTransition));
                mTransitionDurations[g] = thisTransition;
            }
            else if (changed)
                mInertializers[g]->start({}, {}, {}, 0.0, 0.0); // a pose of the wrong size ends any transition

            if (mRecordTargets)
                std::copy(mIncoming.begin(), mIncoming.end(), mGroupTargets[g].begin());

            // Keep last frame's output as the previous pose; this frame's output is the new sample plus the offset.
            std::swap(mGroupPrevPoses[g], mGroupPoses[g]);
            std::copy(mIncoming.begin(), mIncoming.end(), mGroupPoses[g].begin());
            if (mInertializers[g]->active())
                mInertializers[g]->advanceAndApply(changed ? 0.0 : static_cast<double>(duration), mGroupPoses[g]);

            if (groupChanged)
            {
                mPreviousGroupNames[g].assign(mLastGroupNames[g]);
                mLastSources[g] = current.mSource;
                mLastGroupNames[g].assign(current.mGroupName);
            }
        }

        const double unitsPerMeter = Misc::gameConventions().mUnitsPerMeter;
        for (std::size_t i = 0; i < mSkeleton.bones.size(); ++i)
        {
            const uint32_t group = mBoneGroups[i] < sNumBlendMasks ? mBoneGroups[i] : 0;
            const BoundClip* clip = findClip(active[group].mSource);
            mAnimated[i] = clip != nullptr && clip->mHasTrack[i];
            const opas::math::Transform& t = mAnimated[i] ? mGroupPoses[group][i] : mRestPose[i];
            mBoneTranslations[i] = vsg::vec3(static_cast<float>(t.translation.x * unitsPerMeter),
                static_cast<float>(t.translation.y * unitsPerMeter),
                static_cast<float>(t.translation.z * unitsPerMeter));
            mBoneRotations[i] = vsg::quat(static_cast<float>(t.rotation.x), static_cast<float>(t.rotation.y),
                static_cast<float>(t.rotation.z), static_cast<float>(t.rotation.w));
        }
    }

    void CreativeMotionPoseDriver::setStyle(const opas::style::StyleProfile* profile, std::uint64_t actorSeed)
    {
        mStyleProfile = profile;
        mStyleSeed = actorSeed;
        // Size the working copy now, so that update() allocates nothing while the style stays the same.
        if (profile != nullptr)
            mEffectiveStyle = *profile;
    }

    CreativeMotionPoseDriver::StyleStatus CreativeMotionPoseDriver::getStyleStatus() const
    {
        StyleStatus status;
        status.mProfile = mStyleProfile;
        status.mEnabled = mStyleEnabled;
        status.mWeights = styleWeights(mActorState);
        if (mStyleProfile != nullptr && mStyleEnabled)
            status.mCadence = cadenceFactor(*mStyleProfile, status.mWeights);
        return status;
    }

    CreativeMotionPoseDriver::TransitionStatus CreativeMotionPoseDriver::getTransitionStatus(size_t group) const
    {
        TransitionStatus status;
        if (group >= sNumBlendMasks)
            return status;
        status.mGroup = mLastGroupNames[group];
        status.mPreviousGroup = mPreviousGroupNames[group];
        status.mActive = mInertializers[group] != nullptr && mInertializers[group]->active();
        status.mElapsed = status.mActive ? static_cast<float>(mInertializers[group]->elapsedS()) : 0.f;
        status.mDuration = mTransitionDurations[group];
        return status;
    }

    void CreativeMotionPoseDriver::apply() const
    {
        if (!mValid)
            return;
        for (std::size_t i = 0; i < mBoneNodes.size(); ++i)
        {
            if (!mAnimated[i])
                continue;
            mBoneNodes[i]->translation = mBoneTranslations[i];
            mBoneNodes[i]->setAttitude(mBoneRotations[i]);
        }
    }

    const CreativeMotionPoseDriver::CompareExtremes& CreativeMotionPoseDriver::compareExtremes(size_t group) const
    {
        static const CompareExtremes none;
        return group < sNumBlendMasks ? mCompareExtremes[group] : none;
    }

    std::string CreativeMotionPoseDriver::describeCompareExtremes() const
    {
        std::string text;
        char buffer[160];
        for (std::size_t g = 0; g < sNumBlendMasks; ++g)
        {
            const CompareExtremes& e = mCompareExtremes[g];
            if (e.mBone.empty())
                continue;
            const int length = std::snprintf(buffer, sizeof(buffer), " g%zu=%.5fm/%.5frad(%s)", g, e.mTranslation,
                e.mRotation, e.mBone.c_str());
            if (length > 0)
                text.append(buffer, std::min<std::size_t>(static_cast<std::size_t>(length), sizeof(buffer) - 1));
        }
        return text;
    }

    bool CreativeMotionPoseDriver::compareWithCurrentPose(
        float& outMaxTransDiff, float& outMaxRotDiff, std::string& outMaxDiffBone)
    {
        if (!mValid)
            return false;

        outMaxTransDiff = 0.0f;
        outMaxRotDiff = 0.0f;
        outMaxDiffBone.clear();

        const float invUnitsPerMeter = 1.0f / Misc::gameConventions().mUnitsPerMeter;
        for (std::size_t i = 0; i < mBoneNodes.size(); ++i)
        {
            if (!mAnimated[i])
                continue;
            const opas::math::Transform current = localTransform(*mBoneNodes[i]);
            const vsg::vec3 translation = mBoneNodes[i]->translation;
            const float transDiff = vsg::length(translation - mBoneTranslations[i]) * invUnitsPerMeter;

            const vsg::quat& rotation = mBoneRotations[i];
            double dot = std::abs(current.rotation.x * rotation.x + current.rotation.y * rotation.y
                + current.rotation.z * rotation.z + current.rotation.w * rotation.w);
            dot = std::clamp(dot, 0.0, 1.0);
            const float rotDiff = static_cast<float>(2.0 * std::acos(dot));

            if (transDiff > outMaxTransDiff || rotDiff > outMaxRotDiff)
            {
                outMaxTransDiff = std::max(outMaxTransDiff, transDiff);
                outMaxRotDiff = std::max(outMaxRotDiff, rotDiff);
                outMaxDiffBone = mSkeleton.bones[i].name;
            }

            // The extremes of the bone's group over the whole run. The bone named is the one with the larger
            // difference when measured as a share of its group's thresholds (5 mm, about 1.1 degrees).
            CompareExtremes& extremes = mCompareExtremes[mBoneGroups[i] < sNumBlendMasks ? mBoneGroups[i] : 0];
            const bool larger = transDiff / 0.005f + rotDiff / 0.02f > extremes.mTranslation / 0.005f + extremes.mRotation / 0.02f;
            if (larger || extremes.mBone.empty())
                extremes.mBone = mSkeleton.bones[i].name;
            extremes.mTranslation = std::max(extremes.mTranslation, transDiff);
            extremes.mRotation = std::max(extremes.mRotation, rotDiff);
        }
        return true;
    }
}
