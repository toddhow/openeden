// Tests for the keyframes of placed scenery (components/mwanimation/sceneryanim.hpp, task 0096 stage f): the rule that
// decides which model gets them, the group it loops, the phase taken from the reference number, and the driver that
// turns an auto-play group's timer into steps of a Play. No device.

#include <components/animation/controllermap.hpp>
#include <components/animation/tags.hpp>
#include <components/mwanimation/play.hpp>
#include <components/mwanimation/sceneryanim.hpp>

#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <set>

namespace
{
    using namespace MWAnim;

    TEST(SceneryKeyframesRule, applies_only_with_the_setting_a_file_and_no_animation_of_its_own)
    {
        for (int bits = 0; bits < 8; ++bits)
        {
            const bool setting = (bits & 1) != 0;
            const bool file = (bits & 2) != 0;
            const bool own = (bits & 4) != 0;
            EXPECT_EQ(SceneryKeyframes::applies(setting, file, own), setting && file && !own) << bits;
        }
    }

    TEST(SceneryKeyframesGroup, idle_wins_when_the_file_has_it)
    {
        Anim::Tags tags;
        tags.emplace(0.f, "flap: start");
        tags.emplace(1.f, "flap: stop");
        tags.emplace(0.f, "idle: start");
        tags.emplace(2.f, "idle: stop");
        EXPECT_EQ(SceneryKeyframes::chooseGroup(tags), "idle");
    }

    // A placed Activator plays "idle" and nothing else (CharacterController::refreshIdleAnims,
    // idleStateToAnimGroup); a far copy must stay still where the near object is still.
    TEST(SceneryKeyframesGroup, without_idle_nothing_is_chosen_as_a_near_object_plays_only_idle)
    {
        Anim::Tags tags;
        tags.emplace(0.f, "wave: start");
        tags.emplace(1.f, "wave: stop");
        tags.emplace(0.f, "flap: start");
        tags.emplace(1.f, "flap: stop");
        EXPECT_TRUE(SceneryKeyframes::chooseGroup(tags).empty());
    }

    TEST(SceneryKeyframesGroup, a_file_with_no_groups_gives_none)
    {
        Anim::Tags tags;
        tags.emplace(0.f, "no separator here");
        EXPECT_TRUE(SceneryKeyframes::chooseGroup(tags).empty());
    }

    TEST(SceneryKeyframesPhase, is_in_the_unit_interval_and_depends_on_the_id_alone)
    {
        std::set<float> seen;
        for (std::uint64_t id = 0; id < 2000; ++id)
        {
            const float p = SceneryKeyframes::phase(id);
            EXPECT_GE(p, 0.f);
            EXPECT_LT(p, 1.f);
            EXPECT_EQ(p, SceneryKeyframes::phase(id));
            seen.insert(p);
        }
        // Neighbouring reference numbers must not wave in step.
        EXPECT_GT(seen.size(), 1990u);
    }

    TEST(SceneryKeyframesPhase, spreads_over_the_whole_loop)
    {
        std::array<int, 10> buckets{};
        for (std::uint64_t id = 0; id < 10000; ++id)
            ++buckets[static_cast<std::size_t>(SceneryKeyframes::phase(id) * 10.f)];
        for (int count : buckets)
        {
            EXPECT_GT(count, 800);
            EXPECT_LT(count, 1200);
        }
    }

    TEST(SceneryKeyframesPhase, a_content_file_in_the_high_half_of_the_id_changes_the_phase)
    {
        EXPECT_NE(SceneryKeyframes::phase(5), SceneryKeyframes::phase((std::uint64_t{ 1 } << 32) | 5));
    }

    TEST(SceneryKeyframesStart, a_play_without_the_group_is_not_started)
    {
        Play play;
        EXPECT_FALSE(SceneryKeyframes::start(play, "idle", 1));
        EXPECT_FALSE(SceneryKeyframes::start(play, "", 1));
    }

    // A real keyframe file of placed scenery has "idle: start" and "idle: stop" and no loop keys; the loop must run
    // between them, from the phase of the reference number, and keep going past the end.
    TEST(SceneryKeyframesStart, loops_a_group_that_has_only_start_and_stop_keys)
    {
        auto source = vsg::ref_ptr<Anim::ControllerMap>(new Anim::ControllerMap);
        source->tags = vsg::ref_ptr<Anim::Tags>(new Anim::Tags);
        source->tags->emplace(0.f, "idle: start");
        source->tags->emplace(2.f, "idle: stop");
        Play play;
        play.addSingleAnimSource(source, "test");
        ASSERT_TRUE(SceneryKeyframes::start(play, "idle", 7));
        ASSERT_TRUE(play.isPlaying("idle"));
        float complete = -1.f;
        ASSERT_TRUE(play.getInfo("idle", &complete));
        EXPECT_NEAR(complete, SceneryKeyframes::phase(7), 1e-5f);
        // Seven seconds is three and a half loops: it is still playing and the completion stays inside the loop.
        for (int i = 0; i < 420; ++i)
            play.runAnimation(1.f / 60.f);
        EXPECT_TRUE(play.isPlaying("idle"));
        ASSERT_TRUE(play.getInfo("idle", &complete));
        EXPECT_GE(complete, 0.f);
        EXPECT_LE(complete, 1.f);
    }

    TEST(SceneryKeyframesDriver, advances_the_play_by_the_timer_difference_only)
    {
        Play play;
        SceneryKeyframes::Driver driver(&play);
        EXPECT_TRUE(driver.hints.autoPlay);
        auto target = vsg::Group::create();
        // A Play with no states: running it must be harmless, also with a timer that does not advance or goes back.
        driver.run(*target, 0.016f);
        driver.run(*target, 0.016f);
        driver.run(*target, 0.010f);
        driver.run(*target, 0.5f);
        SUCCEED();
    }

    TEST(SceneryKeyframesDriver, moves_a_started_loop_by_the_time_that_passed)
    {
        auto source = vsg::ref_ptr<Anim::ControllerMap>(new Anim::ControllerMap);
        source->tags = vsg::ref_ptr<Anim::Tags>(new Anim::Tags);
        source->tags->emplace(0.f, "idle: start");
        source->tags->emplace(4.f, "idle: stop");
        Play play;
        play.addSingleAnimSource(source, "test");
        ASSERT_TRUE(SceneryKeyframes::start(play, "idle", 0));
        float before = 0.f;
        float after = 0.f;
        play.getInfo("idle", &before);
        SceneryKeyframes::Driver driver(&play);
        auto target = vsg::Group::create();
        driver.run(*target, 1.f); // the group's timer: one second since the start
        play.getInfo("idle", &after);
        EXPECT_NEAR(std::fmod(after - before + 1.f, 1.f), 0.25f, 1e-4f); // the loop may wrap
        driver.run(*target, 1.5f); // half a second more, not one and a half
        play.getInfo("idle", &before);
        EXPECT_NEAR(std::fmod(before - after + 1.f, 1.f), 0.125f, 1e-4f);
    }
}
