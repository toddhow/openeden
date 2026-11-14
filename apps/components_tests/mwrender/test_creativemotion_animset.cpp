// Tests of stage C2 of task 0124: the pose driver plays the clips of a shared Anim::AnimSet (apps/openmw/mwrender/
// animclip.*), and reports the largest difference to the keyframe controllers' pose per bone group. Written from
// the headers and the task text; the conversion rules themselves are pinned by test_creativemotion_runtime.cpp.

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/animclip.hpp>
#include <apps/openmw/mwrender/creativemotiondriver.hpp>

#include <components/animation/animset.hpp>
#include <components/animation/bones.hpp>
#include <components/animation/channel.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/interpolate.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/transform.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/misc/gameconventions.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>
#include <vsg/nodes/Group.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using MWRender::AnimClip;
    using MWRender::CreativeMotionActiveSource;
    using MWRender::CreativeMotionPoseDriver;
    using ActiveArray = std::array<CreativeMotionActiveSource, MWRender::sNumBlendMasks>;

    std::string lower(const std::string& s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
        return out;
    }

    const std::map<std::string, uint32_t> sGroups = { { "bip01 spine1", 1 } };

    // Bip01 -> Pelvis -> Spine1 -> Neck, as a detached Anim::Bones.
    struct Rig
    {
        vsg::ref_ptr<vsg::Group> root = vsg::Group::create();
        std::unique_ptr<Anim::Bones> bones;

        Rig()
        {
            auto bip01 = add(*root, "Bip01", { 0.f, 0.f, 0.f });
            auto pelvis = add(*bip01, "Bip01 Pelvis", { 0.f, 0.f, 66.f });
            auto spine1 = add(*pelvis, "Bip01 Spine1", { 0.f, 0.f, 8.f });
            add(*spine1, "Bip01 Neck", { 0.f, 0.f, 15.f });
            bones = std::make_unique<Anim::Bones>(lower, *root, sGroups);
        }

        static vsg::ref_ptr<Anim::Transform> add(vsg::Group& parent, const std::string& name, const vsg::vec3& t)
        {
            auto node = Anim::Transform::create();
            vsgUtil::setName(*node, name);
            node->translation = t;
            parent.addChild(node);
            return node;
        }
    };

    struct Source
    {
        vsg::ref_ptr<Anim::ControllerMap> map{ new Anim::ControllerMap };

        Source() { map->tags = vsg::ref_ptr<Anim::Tags>(new Anim::Tags); }

        void add(
            const std::string& bone, Anim::channel_ptr<vsg::vec3> translate, Anim::channel_ptr<vsg::quat> rotate = {})
        {
            vsg::ref_ptr<Anim::TransformController> controller(new Anim::TransformController);
            controller->translate = translate;
            controller->rotate = rotate;
            map->map[bone] = controller;
        }
    };

    vsg::ref_ptr<Anim::Linear<vsg::vec3>> ramp(const vsg::vec3& from, const vsg::vec3& to, float duration)
    {
        auto channel = Anim::make_channel<Anim::Linear<vsg::vec3>>();
        channel->keys = { { 0.f, from }, { duration, to } };
        return channel;
    }

    vsg::ref_ptr<Anim::Linear<vsg::quat>> turn(float radians)
    {
        auto channel = Anim::make_channel<Anim::Linear<vsg::quat>>();
        channel->keys
            = { { 0.f, vsg::quat(0.f, 0.f, 0.f, 1.f) }, { 1.f, vsg::quat(radians, vsg::vec3(0.f, 0.f, 1.f)) } };
        return channel;
    }

    Source twoBoneSource()
    {
        Source source;
        source.add("Bip01 Pelvis", ramp({ 0.f, 0.f, 60.f }, { 0.f, 0.f, 70.f }, 1.f));
        source.add("Bip01 Spine1", ramp({ 0.f, 0.f, 8.f }, { 0.f, 4.f, 8.f }, 1.f), turn(0.5f));
        source.map->tags->emplace(0.f, "walk: start");
        source.map->tags->emplace(1.f, "walk: stop");
        return source;
    }

    ActiveArray activeFromSet(const Anim::AnimSet& set, float time)
    {
        ActiveArray active{};
        for (auto& a : active)
            a = CreativeMotionActiveSource{ set.controllers.get(), "walk", time, &set };
        return active;
    }

    ActiveArray activeFromSource(const Anim::ControllerMap& source, float time)
    {
        ActiveArray active{};
        for (auto& a : active)
            a = CreativeMotionActiveSource{ &source, "walk", time };
        return active;
    }
}

// --- the clip ----------------------------------------------------------------------------------------------------

TEST(AnimClip, has_one_track_per_animated_bone_named_in_lower_case_in_name_order_and_in_metres)
{
    Source source = twoBoneSource();
    const auto clip = MWRender::buildAnimClip(*source.map);
    ASSERT_NE(clip, nullptr);
    ASSERT_EQ(clip->mClip.tracks.size(), 2u);
    EXPECT_EQ(clip->mClip.tracks[0].bone, "bip01 pelvis");
    EXPECT_EQ(clip->mClip.tracks[1].bone, "bip01 spine1");
    ASSERT_TRUE(clip->mClip.tracks[0].translation.has_value());
    EXPECT_FALSE(clip->mClip.tracks[0].rotation.has_value());
    ASSERT_TRUE(clip->mClip.tracks[1].rotation.has_value());
    const double metres = 70.0 / Misc::gameConventions().mUnitsPerMeter;
    EXPECT_NEAR(clip->mClip.tracks[0].translation->values.back().z, metres, 1e-9);
    EXPECT_NEAR(clip->mClip.tracks[0].translation->timesS.back(), 1.0, 1e-9);
}

TEST(AnimClip, a_source_that_animates_nothing_has_no_clip)
{
    Source empty;
    EXPECT_EQ(MWRender::buildAnimClip(*empty.map), nullptr);

    Source noChannels;
    noChannels.add("Bip01 Pelvis", {}, {});
    EXPECT_EQ(MWRender::buildAnimClip(*noChannels.map), nullptr);
}

TEST(AnimClip, the_duration_covers_the_text_keys_and_the_keys)
{
    Source source;
    source.add("Bip01 Pelvis", ramp({ 0.f, 0.f, 1.f }, { 0.f, 0.f, 2.f }, 1.5f));
    EXPECT_NEAR(MWRender::buildAnimClip(*source.map)->mClip.durationS, 1.5, 1e-6);
    source.map->tags->emplace(4.f, "walk: stop");
    EXPECT_NEAR(MWRender::buildAnimClip(*source.map)->mClip.durationS, 4.0, 1e-6);
}

TEST(AnimClip, the_set_makes_the_clip_once_and_hands_the_same_one_to_everybody)
{
    Source source = twoBoneSource();
    const auto set = Anim::AnimSet::forSource(source.map, "twobone");
    const auto first = MWRender::animClipOf(*set);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(MWRender::animClipOf(*set), first);
}

TEST(AnimClip, many_threads_asking_a_set_at_once_get_one_clip)
{
    Source source = twoBoneSource();
    const auto set = Anim::AnimSet::forSource(source.map, "threads");
    std::vector<std::shared_ptr<const AnimClip>> results(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < results.size(); ++i)
        threads.emplace_back([&, i] { results[i] = MWRender::animClipOf(*set); });
    for (auto& t : threads)
        t.join();
    ASSERT_NE(results[0], nullptr);
    for (const auto& r : results)
        EXPECT_EQ(r, results[0]);
}

TEST(AnimClip, a_set_of_a_source_that_animates_nothing_answers_null)
{
    Source empty;
    const auto set = Anim::AnimSet::forSource(empty.map, "empty");
    EXPECT_EQ(MWRender::animClipOf(*set), nullptr);
    EXPECT_EQ(MWRender::animClipOf(*set), nullptr);
}

// --- sharing the set ---------------------------------------------------------------------------------------------

TEST(AnimSetShare, the_same_source_gives_the_same_set_while_one_holder_is_alive)
{
    Source source = twoBoneSource();
    const auto a = Anim::AnimSet::forSource(source.map, "first");
    const auto b = Anim::AnimSet::forSource(source.map, "second");
    EXPECT_EQ(a.get(), b.get());
    EXPECT_EQ(a->name, "first") << "the name of the set that was made";
}

TEST(AnimSetShare, different_sources_get_different_sets)
{
    Source one = twoBoneSource();
    Source two = twoBoneSource();
    EXPECT_NE(Anim::AnimSet::forSource(one.map).get(), Anim::AnimSet::forSource(two.map).get());
}

TEST(AnimSetShare, a_set_nobody_holds_is_made_again_and_still_works)
{
    Source source = twoBoneSource();
    {
        const auto gone = Anim::AnimSet::forSource(source.map, "gone");
        EXPECT_TRUE(gone->hasGroup("walk"));
    }
    const auto again = Anim::AnimSet::forSource(source.map, "again");
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->name, "again");
    EXPECT_TRUE(again->hasGroup("walk"));
}

TEST(AnimSetShare, many_threads_asking_for_one_source_get_one_set)
{
    Source source = twoBoneSource();
    std::vector<vsg::ref_ptr<const Anim::AnimSet>> results(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < results.size(); ++i)
        threads.emplace_back([&, i] { results[i] = Anim::AnimSet::forSource(source.map, "t"); });
    for (auto& t : threads)
        t.join();
    for (const auto& r : results)
        EXPECT_EQ(r.get(), results[0].get());
}

// --- the driver --------------------------------------------------------------------------------------------------

TEST(CreativeMotionAnimSet, a_driver_given_the_set_samples_the_same_pose_as_one_given_the_source)
{
    Rig rigA, rigB;
    CreativeMotionPoseDriver fromSet(*rigA.bones);
    CreativeMotionPoseDriver fromSource(*rigB.bones);
    ASSERT_TRUE(fromSet.isValid());
    Source source = twoBoneSource();
    const auto set = Anim::AnimSet::forSource(source.map, "twobone");
    ASSERT_TRUE(fromSet.registerSource(*set));
    ASSERT_TRUE(fromSource.registerSource(*source.map));

    for (const float t : { 0.f, 0.1f, 0.5f, 0.77f, 1.f, 1.4f })
    {
        fromSet.update(1.f / 60.f, activeFromSet(*set, t), 0.f);
        fromSource.update(1.f / 60.f, activeFromSource(*source.map, t), 0.f);
        for (const char* name : { "bip01 pelvis", "bip01 spine1", "bip01 neck", "bip01" })
        {
            const int a = fromSet.getBoneIndex(name);
            const int b = fromSource.getBoneIndex(name);
            ASSERT_GE(a, 0);
            ASSERT_EQ(a, b);
            EXPECT_EQ(fromSet.isBoneAnimated(a), fromSource.isBoneAnimated(b)) << name << " at " << t;
            EXPECT_EQ(fromSet.getBoneTranslation(a), fromSource.getBoneTranslation(b)) << name << " at " << t;
            EXPECT_EQ(fromSet.getBoneRotation(a), fromSource.getBoneRotation(b)) << name << " at " << t;
        }
    }
}

TEST(CreativeMotionAnimSet, a_set_is_registered_on_the_fly_by_update)
{
    Rig rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    Source source = twoBoneSource();
    const auto set = Anim::AnimSet::forSource(source.map, "twobone");
    driver.update(1.f / 60.f, activeFromSet(*set, 0.5f), 0.f);
    const int pelvis = driver.getBoneIndex("bip01 pelvis");
    EXPECT_TRUE(driver.isBoneAnimated(pelvis));
    EXPECT_NEAR(driver.getBoneTranslation(pelvis).z, 65.f, 1e-3f);
}

TEST(CreativeMotionAnimSet, two_drivers_share_the_sets_clip)
{
    Rig rigA, rigB;
    CreativeMotionPoseDriver a(*rigA.bones), b(*rigB.bones);
    Source source = twoBoneSource();
    const auto set = Anim::AnimSet::forSource(source.map, "twobone");
    const long before = MWRender::animClipOf(*set).use_count();
    ASSERT_TRUE(a.registerSource(*set));
    ASSERT_TRUE(b.registerSource(*set));
    // The set's own hold, plus one for each driver (and the temporary of this very call).
    EXPECT_GE(MWRender::animClipOf(*set).use_count(), before + 2);
}

TEST(CreativeMotionAnimSet, a_set_that_animates_no_bone_of_the_skeleton_is_refused_and_remembered)
{
    Rig rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    Source source;
    source.add("Bip01 Tail", ramp({ 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f }, 1.f));
    const auto set = Anim::AnimSet::forSource(source.map, "tail");
    EXPECT_FALSE(driver.registerSource(*set));
    EXPECT_FALSE(driver.registerSource(*set));

    Source none;
    const auto emptySet = Anim::AnimSet::forSource(none.map, "none");
    EXPECT_FALSE(driver.registerSource(*emptySet));
}

TEST(CreativeMotionAnimSet, the_comparison_keeps_the_largest_difference_of_each_bone_group)
{
    Rig rig;
    CreativeMotionPoseDriver driver(*rig.bones);
    Source source = twoBoneSource();
    ASSERT_TRUE(driver.registerSource(*source.map));
    const int pelvis = driver.getBoneIndex("bip01 pelvis"); // group 0
    const int spine = driver.getBoneIndex("bip01 spine1"); // group 1
    ASSERT_GE(pelvis, 0);
    ASSERT_GE(spine, 0);

    driver.update(1.f / 60.f, activeFromSource(*source.map, 1.f), 0.f);
    float trans = 0.f, rot = 0.f;
    std::string bone;
    ASSERT_TRUE(driver.compareWithCurrentPose(trans, rot, bone));
    EXPECT_GT(trans, 0.f) << "the bones are still at their rest";

    // Group 0's pelvis moved 4 units further than the rest (66 -> 70) and group 1's spine turned 0.5 rad and moved 4.
    const auto& zero = driver.compareExtremes(0);
    const auto& one = driver.compareExtremes(1);
    EXPECT_EQ(zero.mBone, "bip01 pelvis");
    EXPECT_NEAR(zero.mTranslation, 4.f / Misc::gameConventions().mUnitsPerMeter, 1e-4f);
    EXPECT_EQ(one.mBone, "bip01 spine1");
    EXPECT_NEAR(one.mRotation, 0.5f, 1e-3f);
    EXPECT_EQ(driver.compareExtremes(2).mBone, "") << "a group with no animated bone reports nothing";

    // An earlier, larger difference stays; a smaller one later does not replace it.
    driver.update(1.f / 60.f, activeFromSource(*source.map, 0.f), 0.f);
    ASSERT_TRUE(driver.compareWithCurrentPose(trans, rot, bone));
    EXPECT_NEAR(driver.compareExtremes(1).mRotation, 0.5f, 1e-3f);

    // After apply() the current pose is the driver's own: no new difference, the extremes remain.
    driver.apply();
    ASSERT_TRUE(driver.compareWithCurrentPose(trans, rot, bone));
    EXPECT_NEAR(trans, 0.f, 1e-6f);
    EXPECT_NEAR(driver.compareExtremes(1).mRotation, 0.5f, 1e-3f);
}
