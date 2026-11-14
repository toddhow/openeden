// Tests for CreativeMotion's glTF locomotion clips played by the engine (task 0123): apps/openmw/mwrender/gltfclips.*.
// Written from the spec before the retargeting existed (the stub returned nothing).
//
// In plain words: the shipped humanoid-locomotion.glb is read with CreativeMotion's reader, then each wanted clip is
// retargeted onto Morrowind's Bip01 skeleton and baked into the engine's animation channels with text keys the
// inertial locomotion looks for. The two rigs' local bone axes differ, so the transfer is "turn by the same amount in
// model space" and not "copy the local rotation".

#include "apps/openmw/mwrender/gltfclips.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

#include <components/animation/channel.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/misc/gameconventions.hpp>

#include <opas/math/Quat.hpp>
#include <opas/rig/Skeleton.hpp>

#include "apps/openmw/mwrender/creativemotiondriver.hpp"

namespace
{
    using MWRender::RigBones;
    namespace GltfClips = MWRender::GltfClips;
    using opas::math::Quat;

    std::string readFile(const std::string& relative)
    {
        std::ifstream in(
            std::string(reinterpret_cast<const char*>(OPENMW_PROJECT_SOURCE_DIR)) + "/" + relative, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    std::optional<GltfClips::Source> loadShipped(std::string& error)
    {
        static const std::string glb = readFile("components/creativemotion/data/clips/humanoid-locomotion.glb");
        static const std::string profile
            = readFile("components/creativemotion/data/profiles/humanoid-to-eden.skeletonprofile.json");
        if (glb.empty() || profile.empty())
        {
            error = "the shipped files are missing";
            return std::nullopt;
        }
        return GltfClips::parse(std::as_bytes(std::span(glb)), "humanoid-locomotion.glb", profile,
            "humanoid-to-eden.skeletonprofile.json", error);
    }

    Quat quatFrom(unsigned seed)
    {
        // A fixed pseudo-random unit quaternion per seed (a linear congruential generator, no <random> distribution
        // whose output differs between standard libraries).
        unsigned state = seed * 2654435761u + 12345u;
        const auto next = [&state] {
            state = state * 1664525u + 1013904223u;
            return static_cast<double>(state >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
        };
        return opas::math::normalized(Quat{ next(), next(), next(), next() + 1.5 });
    }

    // Morrowind's bone names (lower case, as the engine's normalised bones) with a plausible tree and rests.
    // `scrambled` gives every bone but Bip01 a rest rotation of its own, the way the real skeleton's axes differ from
    // CreativeMotion's.
    RigBones makeBip01(bool scrambled)
    {
        struct Row
        {
            const char* mName;
            int mParent;
            opas::math::Vec3 mOffset;
        };
        // Morrowind's tree: Bip01 > Pelvis > Spine > Spine1 > (Neck > Head, clavicles > arms), Pelvis > legs.
        const Row rows[] = {
            { "bip01", -1, { 0.0, 0.0, 0.0 } },
            { "bip01 pelvis", 0, { 0.0, 0.0, 0.95 } },
            { "bip01 spine", 1, { 0.0, 0.0, 0.1 } },
            { "bip01 spine1", 2, { 0.0, 0.0, 0.15 } },
            { "bip01 neck", 3, { 0.0, 0.0, 0.2 } },
            { "bip01 head", 4, { 0.0, 0.0, 0.07 } },
            { "bip01 l thigh", 1, { 0.09, 0.0, -0.05 } },
            { "bip01 l calf", 6, { 0.0, 0.0, -0.43 } },
            { "bip01 l foot", 7, { 0.0, 0.0, -0.43 } },
            { "bip01 r thigh", 1, { -0.09, 0.0, -0.05 } },
            { "bip01 r calf", 9, { 0.0, 0.0, -0.43 } },
            { "bip01 r foot", 10, { 0.0, 0.0, -0.43 } },
            { "bip01 l upperarm", 3, { 0.21, 0.0, 0.15 } },
            { "bip01 l forearm", 12, { 0.29, 0.0, 0.0 } },
            { "bip01 l hand", 13, { 0.26, 0.0, 0.0 } },
            { "bip01 r upperarm", 3, { -0.21, 0.0, 0.15 } },
            { "bip01 r forearm", 15, { -0.29, 0.0, 0.0 } },
            { "bip01 r hand", 16, { -0.26, 0.0, 0.0 } },
        };
        RigBones rig;
        unsigned seed = 1;
        for (const Row& row : rows)
        {
            opas::rig::Bone bone;
            bone.name = row.mName;
            bone.parent = row.mParent;
            bone.localRest.translation = row.mOffset;
            if (scrambled && row.mParent >= 0 && row.mParent != 0)
                bone.localRest.rotation = quatFrom(seed);
            ++seed;
            rig.mSkeleton.bones.push_back(bone);
            rig.mGroups.push_back(0);
        }
        return rig;
    }

    // The baked local rotation of a bone at a time (rest when the map has no channel for it).
    Quat bakedLocal(const Anim::ControllerMap& map, const opas::rig::Bone& bone, float time)
    {
        const auto found = map.map.find(bone.name);
        if (found == map.map.end() || !found->second->rotate)
            return bone.localRest.rotation;
        const vsg::quat q = found->second->rotate->value(time);
        return { q.x, q.y, q.z, q.w };
    }

    // World rotations of the whole target skeleton at a time.
    std::vector<Quat> bakedWorld(const Anim::ControllerMap& map, const opas::rig::Skeleton& skeleton, float time)
    {
        std::vector<Quat> world(skeleton.bones.size());
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
        {
            const Quat local = bakedLocal(map, skeleton.bones[i], time);
            world[i] = skeleton.bones[i].parent < 0
                ? local
                : opas::math::normalized(world[static_cast<std::size_t>(skeleton.bones[i].parent)] * local);
        }
        return world;
    }

    float tagTime(const Anim::Tags& tags, const std::string& key)
    {
        for (auto it = tags.begin(); it != tags.end(); ++it)
            if (it->second == key)
                return it->first;
        return -1.f;
    }

    double angDiff(Quat a, Quat b)
    {
        return opas::math::angleBetween(a, b);
    }

    vsg::vec3 translationAt(const Anim::ControllerMap& map, const std::string& bone, float time)
    {
        const auto found = map.map.find(bone);
        EXPECT_TRUE(found != map.map.end()) << bone;
        if (found == map.map.end() || !found->second->translate)
            return {};
        return found->second->translate->value(time);
    }

    struct Baked
    {
        vsg::ref_ptr<Anim::ControllerMap> mMap;
        RigBones mRig;
    };

    Baked bake(bool scrambled, double unitsPerMeter = 70.0)
    {
        std::string error;
        const auto source = loadShipped(error);
        EXPECT_TRUE(source.has_value()) << error;
        Baked baked;
        baked.mRig = makeBip01(scrambled);
        if (!source)
            return baked;
        baked.mMap = GltfClips::retarget(*source, baked.mRig, unitsPerMeter, GltfClips::sLocomotionGroups, error);
        EXPECT_TRUE(baked.mMap) << error;
        return baked;
    }

    TEST(MWRenderGltfClips, ShippedFilesParseAndHoldTheCreativeMotionRig)
    {
        std::string error;
        const auto source = loadShipped(error);
        ASSERT_TRUE(source.has_value()) << error;
        EXPECT_EQ(source->mClips.skeleton.bones.size(), 19u);
        EXPECT_EQ(source->mProfile.bones.size(), 17u);
        std::set<std::string> names;
        for (const auto& clip : source->mClips.clips)
            names.insert(clip.name);
        EXPECT_TRUE(names.count("WalkStart"));
        EXPECT_TRUE(names.count("WalkStart.001"));
    }

    TEST(MWRenderGltfClips, ParseReportsABrokenFile)
    {
        std::string error;
        const std::string notGltf = "not a glb";
        EXPECT_FALSE(GltfClips::parse(std::as_bytes(std::span(notGltf)), "bad.glb", "{}", "p.json", error));
        EXPECT_FALSE(error.empty());
    }

    TEST(MWRenderGltfClips, TextKeysNameExactlyTheFiveClipsTheFileHasInLowerCase)
    {
        const Baked baked = bake(false);
        ASSERT_TRUE(baked.mMap);
        ASSERT_TRUE(baked.mMap->tags);
        std::set<std::string> groups;
        for (auto it = baked.mMap->tags->begin(); it != baked.mMap->tags->end(); ++it)
            groups.insert(it->second.substr(0, it->second.find(": ")));
        // runstart and walkstop are not in the file; idle, walk and wave are not wanted (idle is a Morrowind group and
        // a later source wins); the .001 copies are not the clips.
        EXPECT_EQ(groups, (std::set<std::string>{ "walkstart", "runstop", "runskid", "turnleft90", "turnright90" }));
        EXPECT_TRUE(baked.mMap->tags->findGroup("walkstart") != baked.mMap->tags->end());
        EXPECT_TRUE(baked.mMap->tags->findGroup("runstart") == baked.mMap->tags->end());
    }

    TEST(MWRenderGltfClips, EachGroupStartsBeforeItStopsByTheClipsDurationAndGroupsDoNotOverlap)
    {
        const Baked baked = bake(false);
        ASSERT_TRUE(baked.mMap);
        const Anim::Tags& tags = *baked.mMap->tags;
        const float start = tagTime(tags, "walkstart: start");
        const float stop = tagTime(tags, "walkstart: stop");
        EXPECT_FLOAT_EQ(start, 0.f);
        EXPECT_NEAR(stop - start, 0.5833f, 0.01f); // WalkStart is 7/12 s in the file
        float previousStop = -1.f;
        for (const char* group : { "walkstart", "runstop", "runskid", "turnleft90", "turnright90" })
        {
            const float s = tagTime(tags, std::string(group) + ": start");
            const float e = tagTime(tags, std::string(group) + ": stop");
            EXPECT_GT(e, s) << group;
            EXPECT_GT(s, previousStop) << group;
            previousStop = e;
        }
    }

    TEST(MWRenderGltfClips, EveryMapHasAControllerForBip01BecauseThePlayerNeedsOne)
    {
        // MWAnim::Play::addSingleAnimSource throws "!findCtrl" for a source with no controller on the accumulation
        // root.
        const Baked baked = bake(false);
        ASSERT_TRUE(baked.mMap);
        const auto bip01 = baked.mMap->map.find("bip01");
        ASSERT_TRUE(bip01 != baked.mMap->map.end());
        EXPECT_TRUE(bip01->second->translate);
    }

    TEST(MWRenderGltfClips, WalkStartCarriesForwardRootMotionAlongPlusYInGameUnits)
    {
        const Baked baked = bake(false, 70.0);
        ASSERT_TRUE(baked.mMap);
        const float stop = tagTime(*baked.mMap->tags, "walkstart: stop");
        const vsg::vec3 begin = translationAt(*baked.mMap, "bip01", 0.f);
        const vsg::vec3 end = translationAt(*baked.mMap, "bip01", stop);
        EXPECT_FLOAT_EQ(begin.x, 0.f);
        EXPECT_FLOAT_EQ(begin.y, 0.f);
        // Morrowind's actors face +Y. A walk start covers a few tenths of a metre: between 5 and 150 units.
        EXPECT_GT(end.y - begin.y, 5.f);
        EXPECT_LT(end.y - begin.y, 150.f);
        EXPECT_LT(std::abs(end.x - begin.x), 0.25f * (end.y - begin.y));
        EXPECT_FLOAT_EQ(end.z, begin.z); // height is not root motion
    }

    TEST(MWRenderGltfClips, ARotatedBip01IsBakedAndThePelvisTranslationIsInItsFrame)
    {
        // Morrowind's real Bip01 has a rotating rest; ClipBinding refuses such an ancestor of the root bone, so the
        // bake neutralises it for the binding only. The pelvis translation key lives in Bip01's frame.
        std::string error;
        const auto source = loadShipped(error);
        ASSERT_TRUE(source.has_value()) << error;
        const RigBones plain = makeBip01(false);
        RigBones rotated = makeBip01(false);
        const Quat turn = opas::math::fromAxisAngle({ 1.0, 0.0, 0.0 }, 0.5 * 3.14159265358979323846);
        rotated.mSkeleton.bones[0].localRest.rotation = turn;
        // The same model-space rest: the pelvis offset is expressed in the rotated Bip01's frame.
        rotated.mSkeleton.bones[1].localRest.translation
            = opas::math::rotate(opas::math::conjugate(turn), plain.mSkeleton.bones[1].localRest.translation);
        const auto a = GltfClips::retarget(*source, plain, 70.0, GltfClips::sLocomotionGroups, error);
        ASSERT_TRUE(a) << error;
        const auto b = GltfClips::retarget(*source, rotated, 70.0, GltfClips::sLocomotionGroups, error);
        ASSERT_TRUE(b) << error;
        const float stop = tagTime(*a->tags, "walkstart: stop");
        const vsg::vec3 p = translationAt(*a, "bip01 pelvis", stop * 0.5f);
        const vsg::vec3 q = translationAt(*b, "bip01 pelvis", stop * 0.5f);
        const opas::math::Vec3 back = opas::math::rotate(turn, { q.x, q.y, q.z });
        EXPECT_NEAR(back.x, p.x, 1e-3);
        EXPECT_NEAR(back.y, p.y, 1e-3);
        EXPECT_NEAR(back.z, p.z, 1e-3);
    }

    TEST(MWRenderGltfClips, TranslationsFollowTheUnitsPerMeter)
    {
        const Baked metres = bake(false, 1.0);
        const Baked units = bake(false, 70.0);
        ASSERT_TRUE(metres.mMap && units.mMap);
        const float stop = tagTime(*metres.mMap->tags, "walkstart: stop");
        const vsg::vec3 a = translationAt(*metres.mMap, "bip01", stop);
        const vsg::vec3 b = translationAt(*units.mMap, "bip01", stop);
        EXPECT_NEAR(b.y, 70.f * a.y, 1e-3f);
        const vsg::vec3 pa = translationAt(*metres.mMap, "bip01 pelvis", 0.2f);
        const vsg::vec3 pb = translationAt(*units.mMap, "bip01 pelvis", 0.2f);
        EXPECT_NEAR(pb.z, 70.f * pa.z, 1e-2f);
    }

    TEST(MWRenderGltfClips, TheModelSpaceTurnOfEveryMappedBoneDoesNotDependOnTheTargetsLocalAxes)
    {
        // The point of the retarget: whatever rest rotations the target's bones have, a bone turns away from its own
        // rest by the same model-space amount the clip's bone does.
        const Baked plain = bake(false);
        const Baked scrambled = bake(true);
        ASSERT_TRUE(plain.mMap && scrambled.mMap);
        const auto restPlain = opas::rig::computeWorldRest(plain.mRig.mSkeleton);
        const auto restScrambled = opas::rig::computeWorldRest(scrambled.mRig.mSkeleton);
        double largest = 0.0;
        for (const float time : { 0.1f, 0.3f, 0.5f })
        {
            const auto a = bakedWorld(*plain.mMap, plain.mRig.mSkeleton, time);
            const auto b = bakedWorld(*scrambled.mMap, scrambled.mRig.mSkeleton, time);
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (!plain.mMap->map.count(plain.mRig.mSkeleton.bones[i].name))
                    continue;
                const Quat turnA = a[i] * opas::math::conjugate(restPlain[i].rotation);
                const Quat turnB = b[i] * opas::math::conjugate(restScrambled[i].rotation);
                EXPECT_NEAR(angDiff(turnA, turnB), 0.0, 1e-4) << plain.mRig.mSkeleton.bones[i].name << " at " << time;
                largest = std::max(largest, angDiff(turnA, Quat{}));
            }
        }
        EXPECT_GT(largest, 0.05) << "the clip should move some bone by more than 3 degrees";
    }

    TEST(MWRenderGltfClips, BonesTheProfileDoesNotMapGetNoChannel)
    {
        const Baked baked = bake(false);
        ASSERT_TRUE(baked.mMap);
        // The profile maps pelvis, spine, spine1, neck, head, arms and legs; Bip01 has root motion only.
        for (const char* name : { "bip01 pelvis", "bip01 spine", "bip01 spine1", "bip01 neck", "bip01 head",
                 "bip01 l thigh", "bip01 r foot", "bip01 l upperarm", "bip01 r hand" })
            EXPECT_TRUE(baked.mMap->map.count(name)) << name;
        const auto bip01 = baked.mMap->map.find("bip01");
        ASSERT_TRUE(bip01 != baked.mMap->map.end());
        EXPECT_FALSE(bip01->second->rotate) << "Bip01 keeps its rotation: the actor's yaw is the engine's";
    }

    TEST(MWRenderGltfClips, ASkeletonTheProfileCannotBindIsRefusedWithAReason)
    {
        std::string error;
        const auto source = loadShipped(error);
        ASSERT_TRUE(source.has_value()) << error;
        RigBones rig;
        opas::rig::Bone only;
        only.name = "bip01";
        rig.mSkeleton.bones.push_back(only);
        rig.mGroups.push_back(0);
        const auto map = GltfClips::retarget(*source, rig, 70.0, GltfClips::sLocomotionGroups, error);
        EXPECT_FALSE(map);
        EXPECT_FALSE(error.empty());
    }

    TEST(MWRenderGltfClips, AGroupTheFileLacksIsLeftOutAndAnUnknownNameBakesNothing)
    {
        std::string error;
        const auto source = loadShipped(error);
        ASSERT_TRUE(source.has_value()) << error;
        const RigBones rig = makeBip01(false);
        constexpr std::string_view onlyMissing[] = { "runstart", "walkstop" };
        EXPECT_FALSE(GltfClips::retarget(*source, rig, 70.0, onlyMissing, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(MWRenderGltfClips, TheDotZeroZeroOneCopiesAreCopiesOrDifferWhereTheNameSays)
    {
        // Reported in the task's stage log: what the .001 clips are. A clip with the same name plus ".001" is a
        // separate action in the file; the engine takes the un-suffixed one, which is the name the events file uses.
        std::string error;
        const auto source = loadShipped(error);
        ASSERT_TRUE(source.has_value()) << error;
        int same = 0;
        int different = 0;
        for (const auto& clip : source->mClips.clips)
        {
            if (clip.name.size() <= 4 || clip.name.substr(clip.name.size() - 4) != ".001")
                continue;
            for (const auto& other : source->mClips.clips)
            {
                if (other.name != clip.name.substr(0, clip.name.size() - 4))
                    continue;
                const bool equal = other.durationS == clip.durationS && other.tracks == clip.tracks;
                (equal ? same : different)++;
                RecordProperty(clip.name, equal ? "identical to the original" : "differs from the original");
            }
        }
        EXPECT_EQ(same + different, 8);
    }
}
