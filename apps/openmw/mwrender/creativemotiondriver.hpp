#ifndef GAME_RENDER_CREATIVEMOTIONDRIVER_H
#define GAME_RENDER_CREATIVEMOTIONDRIVER_H

#include <vsg/core/ref_ptr.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

#include <opas/anim/Clip.hpp>
#include <opas/anim/Inertialization.hpp>
#include <opas/math/Transform.hpp>
#include <opas/rig/Skeleton.hpp>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "animclip.hpp"
#include "blendmask.hpp"
#include "creativemotionstyle.hpp"

namespace Anim
{
    class AnimSet;
    class Bones;
    class ControllerMap;
    class Transform;
}

namespace MWRender
{
    /// A skeleton's bones as CreativeMotion sees them: parents first, rests in metres (task 0123 lifted this out of
    /// the pose driver so the glTF clip retargeting builds the same skeleton).
    struct RigBones
    {
        opas::rig::Skeleton mSkeleton;
        std::vector<Anim::Transform*> mNodes;
        std::vector<uint32_t> mGroups;
    };

    /// Empty when there are no bones or the skeleton does not validate (logged). The nodes are not owned.
    std::optional<RigBones> buildRigBones(Anim::Bones& bones);

    /// What one bone group plays this frame: the animation source of its active state, that state's group name and
    /// its time on the source's timeline (MWAnim::Play::getActive). A null source means the group plays nothing.
    struct CreativeMotionActiveSource
    {
        const Anim::ControllerMap* mSource = nullptr;
        std::string_view mGroupName;
        float mTime = 0.f;
        /// The source's shared AnimSet (task 0124, stage C2), when the caller has it (MWAnim::Play::getActiveState):
        /// the driver then plays the set's one shared clip instead of converting the source itself. Must be last: the
        /// struct is built with positional initializers.
        const Anim::AnimSet* mSet = nullptr;
    };

    /// Developer trace (setting "creativemotion animation debug"): the local rotation of each named bone as a
    /// quaternion "name=(x y z w)", read from the bones' transforms as they are now. A bone that is not there is
    /// skipped. Used with and without the pose driver, so that both can be compared.
    std::string describeBoneRotations(Anim::Bones& bones, const std::vector<std::string_view>& normalizedNames);

    /**
     * @brief Samples, layers and blends an actor's animations with CreativeMotion's runtime and writes the pose to the
     * actor's bones (task 0007, rebuilt on vsgopenmw's animation system in task 0011).
     *
     * The skeleton is every named bone of the actor's Anim::Bones, parent first; each bone belongs to the bone group
     * (blend mask) Anim::Bones assigned it, as in MWAnim::Play. Each animation source (Anim::ControllerMap, one per
     * .kf file) is converted once to an opas::anim::Clip on the source's own timeline. Each frame, every group samples
     * its active source's clip at the active state's time; when a group's active source or group name changes, the
     * change is inertialized over the transition duration; each bone then takes its own group's pose. apply() writes
     * the bones that the active sources animate, in place of MWAnim::Play's keyframe controllers.
     *
     * Units: CreativeMotion works in metres; translations convert at this class's boundary (Constants::UnitsPerMeter).
     *
     * Owned by MWMechanics::CharacterController, one per actor; update() and apply() run where the actor's animation
     * is advanced (CharacterController::runAnimation, possibly on an animation thread) and touch only this actor's
     * bones. update() and apply() allocate nothing; registerSource() allocates (once per source).
     */
    class CreativeMotionPoseDriver
    {
    public:
        /// Rate at which channels with no exact clip equivalent are resampled (task 0007, open question 5).
        static constexpr float sResampleRate = sClipResampleRate;

        /// Builds the skeleton from bones. The driver keeps pointers to the bones' transforms, so bones must outlive
        /// it. Invalid when there is no named bone or the skeleton fails opas::rig::validateSkeleton.
        explicit CreativeMotionPoseDriver(Anim::Bones& bones);
        ~CreativeMotionPoseDriver();

        bool isValid() const { return mValid; }

        /// Converts source to a clip, once; later calls return the cached result. Returns false when the driver is
        /// invalid or the source animates no bone of the skeleton.
        bool registerSource(const Anim::ControllerMap& source);

        /// The same for a source's AnimSet (task 0124, stage C2): the clip is the set's, made once for every actor
        /// that plays it; this driver only binds the clip's tracks to its own bones. Cached under the set's
        /// ControllerMap, so it and the overload above answer for the same source alike.
        bool registerSource(const Anim::AnimSet& set);

        /// Axes on which the accumulation root's movement is accumulated (taken out of the pose, as MWAnim::Play's
        /// reset does): each component that is not 0 sets that axis of the root's sampled translation to 0. The root is
        /// "bip01", or "root bone" when there is no "bip01". Defaults to (1, 1, 0), as MWAnim::Play.
        void setAccumulation(const vsg::vec3& accumulate) { mAccumulate = accumulate; }

        /// Samples every group's active source at its time, inertializing changes of a group's source or group name
        /// over transitionDuration seconds (no transition when it is 0 or the group played nothing before).
        void update(float duration, const std::array<CreativeMotionActiveSource, sNumBlendMasks>& active,
            float transitionDuration);

        /// Writes the translation and rotation of every animated bone (see isBoneAnimated) to its transform.
        void apply() const;

        /// The pose computed by the last update(), in engine units, per skeleton bone.
        const vsg::vec3& getBoneTranslation(size_t boneIndex) const { return mBoneTranslations[boneIndex]; }
        const vsg::quat& getBoneRotation(size_t boneIndex) const { return mBoneRotations[boneIndex]; }

        /// True when the last update()'s active source of the bone's group has a track for the bone.
        bool isBoneAnimated(size_t boneIndex) const { return mAnimated[boneIndex] != 0; }

        /// What one bone group is doing, for the developer trace (setting "creativemotion animation debug"): the group
        /// it plays now, the group it played before the last change, and how far the transition from one to the other
        /// has got (mElapsed of mDuration seconds; mActive is false when none is running).
        struct TransitionStatus
        {
            std::string_view mGroup;
            std::string_view mPreviousGroup;
            bool mActive = false;
            float mElapsed = 0.f;
            float mDuration = 0.f;
        };
        TransitionStatus getTransitionStatus(size_t group) const;

        /// For the developer trace (task 0067, stage 1 review): keep each group's sample before the transition's
        /// offset is added, so that the offset can be measured. Allocates once when switched on; off by default.
        void setRecordTargets(bool enabled);

        /// " tgt=[<bone>=(x y z w);...]" with the recorded target rotation of each named bone that the last update()
        /// animated, in the convention of describeBoneRotations; empty when targets are not recorded.
        std::string describeTargetRotations(const std::vector<std::string_view>& normalizedNames) const;

        /// Skeleton index of the bone with that normalized (Anim::Bones) name, or -1.
        int getBoneIndex(std::string_view normalizedName) const;

        const opas::rig::Skeleton& getSkeleton() const { return mSkeleton; }

        /// The style layer (task 0053). A driver that is never given an enabled style produces exactly the pose it did
        /// without the layer. The profile must outlive the driver (StyleLibrary never frees its profiles).
        void setStyle(const opas::style::StyleProfile* profile, std::uint64_t actorSeed);
        void setStyleEnabled(bool enabled) { mStyleEnabled = enabled; }
        /// What the actor is doing; read by the next update().
        void setActorState(const ActorState& state) { mActorState = state; }
        /// Transition durations by the kinds of motion changed between (classifyGroup, transitionSeconds).
        void setTransitionRules(bool enabled) { mTransitionRules = enabled; }
        /// With the transition rules on: also look at the actor's intent (ActorState::mStarting, mStopping and
        /// mTurnRate) and take the durations from times (task 0067, stage 1).
        void setIntentTransitions(bool enabled, const TransitionTimes& times)
        {
            mIntentTransitions = enabled;
            mTransitionTimes = times;
        }

        struct StyleStatus
        {
            const opas::style::StyleProfile* mProfile = nullptr;
            bool mEnabled = false;
            StyleWeights mWeights;
            float mCadence = 1.f; // factor for the movement animation's speed; 1 without an enabled style
        };
        StyleStatus getStyleStatus() const;

        /// Developer comparison mode: the largest difference between the driver's pose and the bones' current local
        /// transforms over animated bones (translation in metres, rotation in radians), and that bone's name. False
        /// when the driver is invalid.
        bool compareWithCurrentPose(float& outMaxTransDiff, float& outMaxRotDiff, std::string& outMaxDiffBone);

        /// The largest differences compareWithCurrentPose has seen so far in one bone group, over every call (task
        /// 0124, stage C2: the comparison over a whole scenario). mBone is empty until a group had an animated bone.
        struct CompareExtremes
        {
            float mTranslation = 0.f; // metres
            float mRotation = 0.f; // radians
            std::string mBone; // the bone of the larger relative difference when it was seen
        };
        const CompareExtremes& compareExtremes(size_t group) const;

        /// " g0=<m>m/<rad>rad(<bone>) g1=..." for the groups that have a difference, or empty (the CMCOMPARE line).
        std::string describeCompareExtremes() const;

    private:
        struct BoundClip
        {
            /// The (shared) clip this driver plays; its tracks are named by bone.
            std::shared_ptr<const AnimClip> mShared;
            const opas::anim::Clip& clip() const { return mShared->mClip; }
            std::vector<int> mTrackToBone;
            /// Per skeleton bone: 1 when the clip has a track for it.
            std::vector<char> mHasTrack;
        };

        bool buildSkeleton(Anim::Bones& bones);
        /// Binds a clip to this skeleton under the source it came from; false when it animates none of the bones.
        bool bind(const Anim::ControllerMap& source, std::shared_ptr<const AnimClip> clip);
        const BoundClip* findClip(const Anim::ControllerMap* source) const;
        void sampleClip(const BoundClip& clip, float timeS, std::vector<opas::math::Transform>& outPose) const;

        bool mValid = false;

        opas::rig::Skeleton mSkeleton;
        std::map<std::string, int, std::less<>> mBoneNameToIndex;
        std::vector<Anim::Transform*> mBoneNodes;
        std::vector<uint32_t> mBoneGroups;
        int mAccumRoot = -1;
        vsg::vec3 mAccumulate{ 1.f, 1.f, 0.f };

        // Preallocated pose buffers, one per group, and the previous frame's for inertialization.
        std::vector<opas::math::Transform> mRestPose;
        std::array<std::vector<opas::math::Transform>, sNumBlendMasks> mGroupPoses;
        std::array<std::vector<opas::math::Transform>, sNumBlendMasks> mGroupPrevPoses;
        std::vector<opas::math::Transform> mIncoming;
        // The developer trace's targets: each group's sample before the inertializer's offset (setRecordTargets).
        bool mRecordTargets = false;
        std::array<std::vector<opas::math::Transform>, sNumBlendMasks> mGroupTargets;
        std::array<std::unique_ptr<opas::anim::Inertializer>, sNumBlendMasks> mInertializers;

        // Output, in engine units.
        std::vector<vsg::vec3> mBoneTranslations;
        std::vector<vsg::quat> mBoneRotations;
        std::vector<char> mAnimated;

        // Clips by source; a source maps to no clip when it animates no bone. Each entry keeps its source alive, so a
        // released source's address can never be reused by another source while its clip is cached.
        struct CachedClip
        {
            vsg::ref_ptr<const Anim::ControllerMap> mSource;
            std::unique_ptr<BoundClip> mClip;
        };
        std::map<const Anim::ControllerMap*, CachedClip> mClips;
        std::array<CompareExtremes, sNumBlendMasks> mCompareExtremes;

        // Last active source and group name per group, to detect changes.
        std::array<const Anim::ControllerMap*, sNumBlendMasks> mLastSources{};
        std::array<std::string, sNumBlendMasks> mLastGroupNames;
        // For the developer trace: the group name before the last change, and the duration of the last transition.
        std::array<std::string, sNumBlendMasks> mPreviousGroupNames;
        std::array<float, sNumBlendMasks> mTransitionDurations{};

        // The style layer (task 0053). The profile is owned by the StyleLibrary.
        std::unique_ptr<CreativeMotionStyleLayer> mStyleLayer;
        const opas::style::StyleProfile* mStyleProfile = nullptr;
        const opas::style::StyleProfile* mLastStyleApplied = nullptr; // what the last update() applied
        std::uint64_t mStyleSeed = 0;
        bool mStyleEnabled = false;
        bool mTransitionRules = false;
        bool mIntentTransitions = false;
        TransitionTimes mTransitionTimes;
        ActorState mActorState;
        double mStyleTimeS = 0.0; // accumulated frame time, never a clock
        opas::style::StyleProfile mEffectiveStyle; // preallocated by setStyle
    };
}

#endif
