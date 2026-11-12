#include "gltfclips.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

#include <components/animation/bones.hpp>
#include <components/animation/channel.hpp>
#include <components/animation/constant.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/interpolate.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/gameconventions.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/mwanimation/context.hpp>
#include <components/settings/values.hpp>

#include <opas/anim/ClipBinding.hpp>

#include "creativemotiondriver.hpp"

namespace MWRender::GltfClips
{
    namespace
    {
        template <class T>
        Anim::channel_ptr<T> makeChannel(std::map<float, T>&& keys)
        {
            if (keys.empty())
                return {};
            if (keys.size() == 1)
                return Anim::make_constant(keys.begin()->second);
            auto channel = Anim::make_channel<Anim::Linear<T>>();
            channel->keys = std::move(keys);
            return channel;
        }

        struct BoneKeys
        {
            std::map<float, vsg::quat> mRotations;
            std::map<float, vsg::vec3> mTranslations;
        };

        vsg::quat toVsg(const opas::math::Quat& q)
        {
            return vsg::quat(static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z),
                static_cast<float>(q.w));
        }

        vsg::vec3 toUnits(const opas::math::Vec3& v, double unitsPerMeter)
        {
            return vsg::vec3(static_cast<float>(v.x * unitsPerMeter), static_cast<float>(v.y * unitsPerMeter),
                static_cast<float>(v.z * unitsPerMeter));
        }

        std::string lower(std::string_view text)
        {
            return Misc::StringUtils::lowerCase(text);
        }

        const opas::anim::Clip* findClip(const opas::anim::GltfAnimations& clips, std::string_view group)
        {
            for (const opas::anim::Clip& clip : clips.clips)
                if (lower(clip.name) == group)
                    return &clip;
            return nullptr;
        }
    }

    std::optional<Source> parse(std::span<const std::byte> glb, std::string_view glbName,
        std::string_view profileText, std::string_view profileName, std::string& error)
    {
        Source source;
        opas::anim::GltfReadOptions options;
        options.loadSidecar = false; // readGltf reads none; the engine takes the text keys from the group names
        auto clips = opas::anim::readGltf(glb, glbName, options);
        if (!clips.value)
        {
            error = clips.error.toString();
            return std::nullopt;
        }
        auto profile = opas::anim::loadSkeletonProfile(profileText, profileName);
        if (!profile.value)
        {
            error = profile.error.toString();
            return std::nullopt;
        }
        source.mClips = std::move(*clips.value);
        source.mProfile = std::move(*profile.value);
        return source;
    }

    vsg::ref_ptr<Anim::ControllerMap> retarget(const Source& source, const RigBones& target, double unitsPerMeter,
        std::span<const std::string_view> groups, std::string& error)
    {
        using opas::math::Quat;
        const opas::rig::Skeleton& skeleton = target.mSkeleton;
        const std::size_t targetCount = skeleton.bones.size();

        // Bone names in the engine's skeleton are normalised (lower case); the profile's are Morrowind's own.
        opas::anim::SkeletonProfile profile = source.mProfile;
        for (auto& mapping : profile.bones)
            mapping.target = lower(mapping.target);
        for (auto& name : profile.legChain)
            name = lower(name);
        std::map<std::string, int> targetIndex;
        for (std::size_t i = 0; i < targetCount; ++i)
            targetIndex.emplace(lower(skeleton.bones[i].name), static_cast<int>(i));

        const std::vector<opas::math::Transform> targetWorldRest = opas::rig::computeWorldRest(skeleton);
        const Misc::GameConventions& game = Misc::gameConventions();
        const auto accumFound = targetIndex.find(lower(game.mRootBone));
        const int accumRoot = accumFound == targetIndex.end() ? -1 : accumFound->second;

        auto controllers = vsg::ref_ptr{ new Anim::ControllerMap };
        controllers->tags = vsg::ref_ptr{ new Anim::Tags };
        std::map<int, BoneKeys> keysByBone;
        float offset = 0.f;
        int baked = 0;
        std::string problems;

        for (const std::string_view group : groups)
        {
            const opas::anim::Clip* clip = findClip(source.mClips, group);
            if (clip == nullptr)
                continue;
            std::string bindError;
            const auto binding = opas::anim::ClipBinding::create(*clip, skeleton, profile, &bindError);
            if (!binding)
            {
                problems += std::string(group) + ": " + bindError + "; ";
                continue;
            }

            // Source bones and the target bones the profile pairs them with.
            const opas::rig::Skeleton& sourceRest = source.mClips.skeleton;
            const std::vector<opas::math::Transform> sourceWorldRest = opas::rig::computeWorldRest(sourceRest);
            std::vector<int> sourceToTarget(sourceRest.bones.size(), -1);
            std::vector<int> targetToSource(targetCount, -1);
            for (const auto& mapping : profile.bones)
            {
                const auto source_ = opas::rig::findBone(sourceRest, mapping.source);
                const auto found = targetIndex.find(mapping.target);
                if (!source_ || found == targetIndex.end())
                    continue;
                sourceToTarget[*source_] = found->second;
                targetToSource[static_cast<std::size_t>(found->second)] = static_cast<int>(*source_);
            }
            std::vector<char> untracked(targetCount, 0);
            for (const std::size_t i : binding->untrackedTargetBones())
                untracked[i] = 1;
            int rootTarget = -1;
            if (!clip->rootBone.empty())
                if (const auto rootSource = opas::rig::findBone(sourceRest, clip->rootBone))
                    rootTarget = sourceToTarget[*rootSource];

            const double duration = clip->durationS;
            const int steps = std::max(1, static_cast<int>(std::ceil(duration * sBakeRateHz)));
            std::vector<opas::math::Transform> pose(targetCount);
            std::vector<Quat> sourceWorld(sourceRest.bones.size());
            std::vector<Quat> targetWorld(targetCount);
            std::map<int, Quat> lastRotation;
            for (int k = 0; k <= steps; ++k)
            {
                const double t = std::min(duration, static_cast<double>(k) / sBakeRateHz);
                const float time = offset + static_cast<float>(t);
                binding->sample(t, opas::anim::TimeMode::Clamp, pose);

                // The clip's bones in model space: local rotations from the binding (tracked, mapped bones) or the
                // source rest, down the source skeleton.
                for (std::size_t j = 0; j < sourceRest.bones.size(); ++j)
                {
                    const auto& bone = sourceRest.bones[j];
                    Quat local = bone.localRest.rotation;
                    const int mapped = sourceToTarget[j];
                    if (mapped >= 0 && !untracked[static_cast<std::size_t>(mapped)])
                        local = pose[static_cast<std::size_t>(mapped)].rotation;
                    sourceWorld[j] = bone.parent < 0 ? local : opas::math::normalized(sourceWorld[bone.parent] * local);
                }

                // The target: a mapped bone turns by the same model-space amount from its own rest; the others keep
                // their rest under whatever drives their parent.
                for (std::size_t i = 0; i < targetCount; ++i)
                {
                    const auto& bone = skeleton.bones[i];
                    const Quat worldRest = targetWorldRest[i].rotation;
                    const int s = targetToSource[i];
                    Quat world;
                    if (s >= 0)
                    {
                        const Quat turned
                            = sourceWorld[static_cast<std::size_t>(s)]
                            * opas::math::conjugate(sourceWorldRest[static_cast<std::size_t>(s)].rotation);
                        world = opas::math::normalized(turned * worldRest);
                    }
                    else if (bone.parent < 0)
                        world = worldRest;
                    else
                        world = opas::math::normalized(targetWorld[static_cast<std::size_t>(bone.parent)] * bone.localRest.rotation);
                    targetWorld[i] = world;
                    if (s < 0)
                        continue;
                    Quat local = bone.parent < 0
                        ? world
                        : opas::math::normalized(opas::math::conjugate(targetWorld[static_cast<std::size_t>(bone.parent)]) * world);
                    // Keep neighbouring keys in one hemisphere so the interpolation takes the short way.
                    const auto previous = lastRotation.find(static_cast<int>(i));
                    if (previous != lastRotation.end() && opas::math::dot(previous->second, local) < 0.0)
                        local = { -local.x, -local.y, -local.z, -local.w };
                    lastRotation[static_cast<int>(i)] = local;
                    BoneKeys& keys = keysByBone[static_cast<int>(i)];
                    keys.mRotations[time] = toVsg(local);
                    if (static_cast<int>(i) == rootTarget)
                        keys.mTranslations[time] = toUnits(pose[i].translation, unitsPerMeter);
                }

                // Root motion goes to the accumulation root, where the engine reads it (MWAnim::Play).
                if (accumRoot >= 0)
                {
                    const opas::anim::RootMotionDelta motion = binding->rootMotion(0.0, t, opas::anim::TimeMode::Clamp);
                    const opas::math::Vec3 rest = skeleton.bones[static_cast<std::size_t>(accumRoot)].localRest.translation;
                    keysByBone[accumRoot].mTranslations[time]
                        = toUnits({ rest.x + motion.translationM.x, rest.y + motion.translationM.y, rest.z }, unitsPerMeter);
                }
            }

            controllers->tags->emplace(offset, lower(group) + ": start");
            controllers->tags->emplace(offset + static_cast<float>(duration), lower(group) + ": stop");
            offset += static_cast<float>(duration) + sClipGapSeconds;
            ++baked;
        }

        if (baked == 0)
        {
            error = problems.empty() ? "the file holds none of the wanted clips" : problems;
            return {};
        }
        if (!problems.empty())
            Log(Debug::Warning) << "glTF locomotion clips: " << problems;
        for (auto& [bone, keys] : keysByBone)
        {
            auto controller = Anim::TransformController::create();
            controller->rotate = makeChannel(std::move(keys.mRotations));
            controller->translate = makeChannel(std::move(keys.mTranslations));
            controllers->map.emplace(skeleton.bones[static_cast<std::size_t>(bone)].name, std::move(controller));
        }
        return controllers;
    }

    namespace
    {
        // One read and parse per process; a failure is remembered so that it is logged once.
        std::mutex sMutex;
        bool sTried = false;
        std::shared_ptr<const Source> sSource;
    }

    vsg::ref_ptr<Anim::ControllerMap> loadLocomotionClips(const MWAnim::Context& context, Anim::Bones& bones)
    {
        const std::string& setting = Settings::values().mGame.mInertialLocomotion.get();
        if (setting.empty() || setting == "off" || !context.readFile)
            return {};

        std::shared_ptr<const Source> source;
        {
            const std::lock_guard lock(sMutex);
            if (!sTried)
            {
                sTried = true;
                std::string error;
                try
                {
                    const std::string clips = context.readFile(std::string(sClipFile));
                    const std::string profile = context.readFile(std::string(sProfileFile));
                    if (clips.empty() || profile.empty())
                        error = "animations/humanoid-locomotion.glb or its skeleton profile is not in the VFS";
                    else if (auto parsed = parse(std::as_bytes(std::span(clips)), sClipFile, profile, sProfileFile, error))
                        sSource = std::make_shared<const Source>(std::move(*parsed));
                }
                catch (const std::exception& e)
                {
                    error = e.what();
                }
                if (!sSource)
                    Log(Debug::Warning) << "glTF locomotion clips are not available: " << error;
            }
            source = sSource;
        }
        if (!source)
            return {};

        std::optional<RigBones> rig = buildRigBones(bones);
        if (!rig)
            return {};
        std::string error;
        auto result = retarget(*source, *rig, Misc::gameConventions().mUnitsPerMeter, sLocomotionGroups, error);
        if (!result)
        {
            // Once per process: the first-person arms skeleton and the beasts' do not take the clips, and every
            // rebuild of every such actor would say it again.
            static std::atomic<bool> warned{ false };
            if (!warned.exchange(true))
                Log(Debug::Warning) << "glTF locomotion clips: not used for a skeleton: " << error;
            return {};
        }
        static std::atomic<bool> reported{ false };
        if (!reported.exchange(true))
            Log(Debug::Info) << "glTF locomotion clips: baked for a skeleton of " << rig->mSkeleton.bones.size()
                             << " bones (" << result->tags->begin()->second << " ...)";
        return result;
    }
}
