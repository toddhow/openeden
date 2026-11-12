// Characterization tests of MWAnim::Play (task 0124, stage C0). They pin what the vanilla animation player does TODAY,
// before CreativeMotion takes over the animation runtime, so that every later stage can show it changed nothing while
// its new settings are off. They are written against components/mwanimation/play.cpp as it is at the start of the task;
// the expected numbers below are derived by hand from that code (each test says how) and then checked by running it.
//
// In plain words: a Play owns animation SOURCES (a text-key table plus one keyframe controller per bone, which is what
// the .kf, .dae and glTF readers all produce, Anim::ControllerMap), STATES (one per playing group) and bone GROUPS (the
// legs and the upper body can play different groups). These tests cover: how a group is found in the text keys, which
// state wins on which bone group (priority and blend mask), how loops and start/stop markers behave, when text keys
// fire, what root motion is returned and what is written to the bones.
//
// The rig: bone "bip01" (the root; group 0), under it "pelvis" (group 0) and "spine" (group 1). Every source animates
// "bip01" as (100 t, 10 t, t) and "spine" as (t, 0, 0), so time and root motion can be read from the numbers.

#include <components/animation/animset.hpp>
#include <components/animation/bones.hpp>
#include <components/animation/channel.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/tags.hpp>
#include <components/animation/transform.hpp>
#include <components/animation/transformcontroller.hpp>
#include <components/misc/gameconventions.hpp>
#include <components/mwanimation/play.hpp>
#include <components/testing/util.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgadapters/dae/daeclips.hpp>
#include <components/vsgadapters/nif/kf.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/io/Options.h>

#include "../resource/colladadocuments.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using Keys = std::vector<std::pair<float, std::string>>;

    // x = ax * t, y = ay * t, z = az * t.
    struct LinearTrack : Anim::Channel<vsg::vec3>
    {
        LinearTrack(float ax, float ay, float az)
            : mRate(ax, ay, az)
        {
        }
        vsg::vec3 value(float time) const override { return mRate * time; }
        vsg::vec3 mRate;
    };

    vsg::ref_ptr<Anim::ControllerMap> makeSource(const Keys& keys, bool withSpine = true)
    {
        auto map = vsg::ref_ptr<Anim::ControllerMap>(new Anim::ControllerMap);
        map->tags = vsg::ref_ptr<Anim::Tags>(new Anim::Tags);
        for (const auto& [time, text] : keys)
            map->tags->emplace(time, std::string(text));
        auto root = Anim::TransformController::create();
        root->translate = Anim::make_channel<LinearTrack>(100.f, 10.f, 1.f);
        map->map.emplace("bip01", root);
        if (withSpine)
        {
            auto spine = Anim::TransformController::create();
            spine->translate = Anim::make_channel<LinearTrack>(1.f, 0.f, 0.f);
            map->map.emplace("spine", spine);
        }
        return map;
    }

    // The main source: an idle (0 to 2), a walk with a loop section (2 to 4, loop 2.5 to 3.5) and an attack with a hit key.
    Keys mainKeys()
    {
        return { { 0.f, "idle: start" }, { 2.f, "idle: stop" }, { 2.f, "walkforward: start" },
            { 2.5f, "walkforward: loop start" }, { 3.5f, "walkforward: loop stop" }, { 4.f, "walkforward: stop" },
            { 5.f, "attack: start" }, { 5.5f, "attack: hit" }, { 6.f, "attack: stop" } };
    }

    // The group names of the encounteredTags, in order.
    std::vector<std::string> firedTexts(const MWAnim::Play& play)
    {
        std::vector<std::string> out;
        for (const auto& event : play.encounteredTags)
            out.push_back(event.key->second);
        return out;
    }

    struct PlayRig
    {
        vsg::ref_ptr<Anim::Transform> mScene = Anim::Transform::create();
        std::unique_ptr<MWAnim::Play> mPlay;

        PlayRig()
        {
            auto bip01 = Anim::Transform::create();
            auto pelvis = Anim::Transform::create();
            auto spine = Anim::Transform::create();
            vsgUtil::setName(*mScene, "scene");
            vsgUtil::setName(*bip01, "bip01");
            vsgUtil::setName(*pelvis, "pelvis");
            vsgUtil::setName(*spine, "spine");
            mScene->addChild(bip01);
            bip01->addChild(pelvis);
            bip01->addChild(spine);
            Anim::Bones bones(
                [](const std::string& name) {
                    std::string lower = name;
                    for (char& c : lower)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return lower;
                },
                *mScene, { { "spine", 1 } });
            mPlay = std::make_unique<MWAnim::Play>(bones, 2);
        }

        MWAnim::Play& play() { return *mPlay; }

        vsg::vec3 translation(const char* bone) { return mPlay->bones.search(bone)->node()->translation; }

        void addMain() { mPlay->addSingleAnimSource(makeSource(mainKeys()), "main"); }

        // Plays the group on the bone groups of the mask with one priority for all, as CharacterController does.
        void playGroup(std::string_view group, int priority = 1, int mask = 3, uint32_t loops = 0, float speed = 1.f,
            std::string_view start = "start", std::string_view stop = "stop", float startpoint = 0.f,
            bool loopFallback = false, bool autoDisable = true)
        {
            mPlay->play(group, MWAnim::Play::Priority(2, priority), mask, autoDisable, speed, start, stop, startpoint,
                loops, loopFallback);
        }
    };

    // ---- group lookup -------------------------------------------------------------------------------------------

    TEST(PlayCharacterization, a_group_is_known_when_a_text_key_names_it)
    {
        PlayRig rig;
        EXPECT_FALSE(rig.play().hasAnimation("idle")) << "no source yet";
        rig.addMain();
        EXPECT_TRUE(rig.play().hasAnimation("idle"));
        EXPECT_TRUE(rig.play().hasAnimation("walkforward"));
        EXPECT_TRUE(rig.play().hasAnimation("attack"));
        EXPECT_FALSE(rig.play().hasAnimation("sneak"));
        EXPECT_FALSE(rig.play().hasAnimation("walk")) << "a group is matched whole, not by prefix";
    }

    TEST(PlayCharacterization, the_start_time_is_the_first_key_of_the_group_and_unknown_is_minus_one)
    {
        PlayRig rig;
        rig.addMain();
        EXPECT_FLOAT_EQ(rig.play().getStartTime("idle"), 0.f);
        EXPECT_FLOAT_EQ(rig.play().getStartTime("walkforward"), 2.f);
        EXPECT_FLOAT_EQ(rig.play().getStartTime("attack"), 5.f);
        EXPECT_FLOAT_EQ(rig.play().getStartTime("sneak"), -1.f);
    }

    TEST(PlayCharacterization, the_last_added_source_wins_a_group_lookup_and_other_groups_fall_through)
    {
        PlayRig rig;
        rig.addMain();
        rig.play().addSingleAnimSource(
            makeSource({ { 10.f, "walkforward: start" }, { 12.f, "walkforward: stop" } }), "override");
        EXPECT_FLOAT_EQ(rig.play().getStartTime("walkforward"), 10.f);
        EXPECT_FLOAT_EQ(rig.play().getStartTime("attack"), 5.f) << "only the later source lacks it";
        EXPECT_FLOAT_EQ(rig.play().getTextKeyTime("walkforward: stop"), 12.f);
        EXPECT_FLOAT_EQ(rig.play().getTextKeyTime("attack: hit"), 5.5f);
        EXPECT_FLOAT_EQ(rig.play().getTextKeyTime("nothing"), -1.f);
    }

    TEST(PlayCharacterization, a_text_key_time_is_a_prefix_match_in_time_order)
    {
        PlayRig rig;
        rig.addMain();
        EXPECT_FLOAT_EQ(rig.play().getTextKeyTime("walkforward: loop"), 2.5f) << "loop start comes before loop stop";
        EXPECT_FLOAT_EQ(rig.play().getTextKeyTime("attack: h"), 5.5f);
    }

    TEST(PlayCharacterization, the_groups_are_listed_source_by_source_in_name_order)
    {
        PlayRig rig;
        rig.addMain();
        rig.play().addSingleAnimSource(makeSource({ { 0.f, "zzz: start" }, { 1.f, "zzz: stop" }, { 1.f, "aaa: start" },
                                           { 2.f, "aaa: stop" } }),
            "second");
        std::vector<std::string> groups;
        rig.play().listAnimationGroups([&](const std::string& name) { groups.push_back(name); });
        EXPECT_EQ(groups, (std::vector<std::string>{ "attack", "idle", "walkforward", "aaa", "zzz" }));
    }

    // ---- play, time and text keys -------------------------------------------------------------------------------

    TEST(PlayCharacterization, playing_a_group_nobody_has_does_nothing)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("sneak");
        EXPECT_FALSE(rig.play().isPlaying("sneak"));
        float complete = 7.f, speed = 7.f;
        uint32_t loops = 7;
        EXPECT_FALSE(rig.play().getInfo("sneak", &complete, &speed, &loops));
        EXPECT_EQ(complete, 0.f);
        EXPECT_EQ(speed, 0.f);
        EXPECT_EQ(loops, 0u);
        EXPECT_TRUE(rig.play().getActiveGroup(0).empty());
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("sneak"), -1.f);
    }

    TEST(PlayCharacterization, a_played_group_starts_at_its_start_key_and_reports_its_completion)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3, 0, 1.5f);
        EXPECT_TRUE(rig.play().isPlaying("idle"));
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 0.f);
        float complete = -1.f, speed = -1.f;
        uint32_t loops = 9;
        EXPECT_TRUE(rig.play().getInfo("idle", &complete, &speed, &loops));
        EXPECT_FLOAT_EQ(complete, 0.f);
        EXPECT_FLOAT_EQ(speed, 1.5f);
        EXPECT_EQ(loops, 0u);
        rig.play().runAnimation(0.5f); // 0.5 s at speed 1.5: time 0.75 of 2
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 0.75f);
        rig.play().getInfo("idle", &complete);
        EXPECT_FLOAT_EQ(complete, 0.375f);
        EXPECT_EQ(rig.play().getActiveGroup(0), "idle");
        EXPECT_EQ(rig.play().getActiveGroup(1), "idle");
        EXPECT_TRUE(rig.play().getActiveGroup(2).empty()) << "there are only two bone groups";
    }

    TEST(PlayCharacterization, the_key_at_the_start_time_fires_when_the_group_is_played_and_later_keys_as_time_passes)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("attack");
        EXPECT_EQ(firedTexts(rig.play()), (std::vector<std::string>{ "attack: start" }));
        EXPECT_EQ(rig.play().encounteredTags[0].groupname, "attack");
        rig.play().runAnimation(0.25f); // 5.25: nothing new
        EXPECT_EQ(rig.play().encounteredTags.size(), 1u);
        rig.play().runAnimation(0.25f); // 5.5: the hit key, exactly on it
        EXPECT_EQ(firedTexts(rig.play()), (std::vector<std::string>{ "attack: start", "attack: hit" }));
    }

    TEST(PlayCharacterization, one_long_step_fires_every_key_it_crosses_in_order_and_stops_on_the_stop_key)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("attack");
        rig.play().runAnimation(10.f);
        EXPECT_EQ(firedTexts(rig.play()), (std::vector<std::string>{ "attack: start", "attack: hit", "attack: stop" }));
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u);
        EXPECT_EQ(rig.play().endedAnimations[0].groupname, "attack");
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].time, 6.f) << "time stops exactly on the stop key";
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].completion, 1.f);
        EXPECT_EQ(rig.play().endedAnimations[0].startKey, "start");
        EXPECT_EQ(rig.play().endedAnimations[0].stopKey, "stop");
    }

    TEST(PlayCharacterization, a_finished_group_is_removed_and_the_bone_groups_go_idle)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle");
        rig.play().runAnimation(2.5f);
        EXPECT_FALSE(rig.play().isPlaying("idle"));
        EXPECT_FALSE(rig.play().getInfo("idle"));
        EXPECT_TRUE(rig.play().getActiveGroup(0).empty());
        EXPECT_FALSE(rig.play().isPriorityActive(1));
    }

    TEST(PlayCharacterization, a_group_that_is_not_auto_disabled_stays_at_its_stop_key)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3, 0, 1.f, "start", "stop", 0.f, false, false);
        rig.play().runAnimation(3.f);
        EXPECT_FALSE(rig.play().isPlaying("idle")) << "playing is false at the stop key";
        EXPECT_TRUE(rig.play().getInfo("idle")) << "but the state is still there";
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 2.f);
        EXPECT_TRUE(rig.play().endedAnimations.empty());
        EXPECT_EQ(rig.play().getActiveGroup(0), "idle");
        rig.play().disable("idle");
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u) << "disable reports the end";
        EXPECT_FALSE(rig.play().getInfo("idle"));
    }

    TEST(PlayCharacterization, the_start_point_moves_the_start_inside_the_group_and_skips_the_keys_before_it)
    {
        // Play::play fires the keys from lowerBound(time) on, so a key before the start point never fires.
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3, 0, 1.f, "start", "stop", 0.5f);
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 1.f);
        EXPECT_TRUE(rig.play().encounteredTags.empty());
        rig.play().runAnimation(5.f);
        EXPECT_EQ(firedTexts(rig.play()), (std::vector<std::string>{ "idle: stop", "walkforward: start" }))
            << "the keys at time 2 fire when the step reaches them, whatever group they name";
    }

    TEST(PlayCharacterization, markers_other_than_start_and_stop_choose_the_section)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("walkforward", 1, 3, 0, 1.f, "loop start", "loop stop");
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("walkforward"), 2.5f);
        rig.play().runAnimation(5.f);
        EXPECT_FALSE(rig.play().isPlaying("walkforward"));
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u);
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].time, 3.5f);
    }

    TEST(PlayCharacterization, a_missing_loop_start_marker_falls_back_to_start_and_other_missing_markers_refuse)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3, 0, 1.f, "loop start", "stop");
        EXPECT_TRUE(rig.play().isPlaying("idle")) << "idle has no loop start: the start key is used";
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 0.f);
        PlayRig other;
        other.addMain();
        other.playGroup("idle", 1, 3, 0, 1.f, "nonsense", "stop");
        EXPECT_FALSE(other.play().isPlaying("idle")) << "an unknown start marker other than loop start refuses";
        other.playGroup("idle", 1, 3, 0, 1.f, "start", "nonsense");
        EXPECT_FALSE(other.play().isPlaying("idle")) << "an unknown stop marker refuses";
    }

    TEST(PlayCharacterization, a_stop_key_matches_by_prefix_so_garbage_after_it_is_ignored)
    {
        PlayRig rig;
        rig.play().addSingleAnimSource(makeSource({ { 0.f, "scrib: start" }, { 1.f, "scrib: stop." } }), "scrib");
        rig.playGroup("scrib");
        EXPECT_TRUE(rig.play().isPlaying("scrib"));
        rig.play().runAnimation(5.f);
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u);
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].time, 1.f);
    }

    TEST(PlayCharacterization, a_group_whose_stop_key_is_before_its_start_key_refuses)
    {
        PlayRig rig;
        rig.play().addSingleAnimSource(makeSource({ { 3.f, "odd: start" }, { 1.f, "odd: stop" } }), "odd");
        rig.playGroup("odd");
        EXPECT_FALSE(rig.play().isPlaying("odd"));
    }

    TEST(PlayCharacterization, a_group_repeated_in_one_source_uses_its_last_start_and_stop_keys)
    {
        PlayRig rig; // as undeadwolf_2.nif: two walkforward sections, the last is used
        rig.play().addSingleAnimSource(makeSource({ { 1.f, "wolf: start" }, { 2.f, "wolf: stop" },
                                           { 5.f, "wolf: start" }, { 7.f, "wolf: stop" } }),
            "wolf");
        rig.playGroup("wolf");
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("wolf"), 5.f);
        rig.play().runAnimation(10.f);
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u);
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].time, 7.f);
    }

    TEST(PlayCharacterization, a_text_key_in_a_source_is_played_from_the_latest_source_that_has_the_group)
    {
        PlayRig rig;
        rig.addMain();
        rig.play().addSingleAnimSource(
            makeSource({ { 10.f, "walkforward: start" }, { 12.f, "walkforward: stop" } }), "override");
        rig.playGroup("walkforward");
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("walkforward"), 10.f);
        auto active = rig.play().getActiveState(0);
        EXPECT_EQ(active.groupname, "walkforward");
        EXPECT_FLOAT_EQ(active.time, 10.f);
        rig.playGroup("attack", 2, 3);
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("attack"), 5.f) << "falls through to the first source";
    }

    // ---- speed, loops --------------------------------------------------------------------------------------------

    TEST(PlayCharacterization, the_speed_multiplier_scales_time_and_can_be_changed_while_playing)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3, 0, 2.f);
        rig.play().runAnimation(0.25f);
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 0.5f);
        rig.play().adjustSpeedMult("idle", 0.5f);
        rig.play().runAnimation(1.f);
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 1.f);
        float speed = 0.f;
        rig.play().getInfo("idle", nullptr, &speed);
        EXPECT_FLOAT_EQ(speed, 0.5f);
    }

    TEST(PlayCharacterization, a_group_with_loop_keys_loops_between_them_and_counts_down)
    {
        // Hand-traced from Play::runAnimation: the 1.5 s step stops on the loop start key (2.5, learned there), goes on
        // to the loop stop key (3.5), loops once (count 2 to 1) and returns to 2.5, firing the loop start key again.
        PlayRig rig;
        rig.addMain();
        rig.playGroup("walkforward", 1, 3, 2);
        rig.play().runAnimation(1.5f);
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("walkforward"), 2.5f);
        EXPECT_EQ(rig.play().getCurrentLoopCount("walkforward"), 1u);
        EXPECT_TRUE(rig.play().isPlaying("walkforward"));
        std::vector<std::string> fired = firedTexts(rig.play());
        // "idle: stop" shares time 2.0 with "walkforward: start" and fires with it (keys are not filtered by group).
        EXPECT_EQ(fired, (std::vector<std::string>{ "idle: stop", "walkforward: start", "walkforward: loop start",
                             "walkforward: loop stop", "walkforward: loop start" }));
        rig.play().runAnimation(1.f); // 2.5 to 3.5 again: the second loop
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("walkforward"), 2.5f);
        EXPECT_EQ(rig.play().getCurrentLoopCount("walkforward"), 0u);
        rig.play().runAnimation(5.f); // no loops left: runs on to the stop key and ends
        EXPECT_FALSE(rig.play().isPlaying("walkforward"));
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u);
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].time, 4.f);
    }

    TEST(PlayCharacterization, with_no_loops_requested_the_loop_keys_are_passed_and_the_group_runs_to_its_stop)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("walkforward", 1, 3, 0);
        rig.play().runAnimation(10.f);
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u);
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].time, 4.f);
    }

    TEST(PlayCharacterization, looping_can_be_switched_off_while_playing)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("walkforward", 1, 3, 5);
        rig.play().setLoopingEnabled("walkforward", false);
        rig.play().runAnimation(10.f);
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u);
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].time, 4.f);
        EXPECT_EQ(rig.play().endedAnimations[0].groupname, "walkforward");
    }

    TEST(PlayCharacterization, without_loop_keys_the_start_and_stop_keys_loop_only_when_the_fallback_is_asked_for)
    {
        // Hand-traced: idle (0 to 2) with the fallback and one loop: at the stop key the loop count goes 1 to 0, time
        // returns to 0 and the rest of the step (0.5 s) is played, so after 2.5 s the time is 0.5.
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3, 1, 1.f, "start", "stop", 0.f, true);
        rig.play().runAnimation(2.5f);
        EXPECT_TRUE(rig.play().isPlaying("idle"));
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 0.5f);
        EXPECT_EQ(rig.play().getCurrentLoopCount("idle"), 0u);

        PlayRig none;
        none.addMain();
        none.playGroup("idle", 1, 3, 3, 1.f, "start", "stop", 0.f, false);
        none.play().runAnimation(2.5f);
        EXPECT_FALSE(none.play().isPlaying("idle")) << "no fallback: the loop count is ignored";
        ASSERT_EQ(none.play().endedAnimations.size(), 1u);
        EXPECT_EQ(none.play().endedAnimations[0].time, 2.f);
    }

    // ---- priority and blend masks ---------------------------------------------------------------------------------

    TEST(PlayCharacterization, the_higher_priority_group_is_active_on_each_bone_group_it_is_masked_to)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3);
        rig.playGroup("attack", 2, 2); // upper body only
        EXPECT_EQ(rig.play().getActiveGroup(0), "idle");
        EXPECT_EQ(rig.play().getActiveGroup(1), "attack");
        rig.playGroup("walkforward", 3, 1); // legs only
        EXPECT_EQ(rig.play().getActiveGroup(0), "walkforward");
        EXPECT_EQ(rig.play().getActiveGroup(1), "attack");
        EXPECT_TRUE(rig.play().isPlaying("idle")) << "a covered state keeps playing";
    }

    TEST(PlayCharacterization, a_new_group_with_the_same_priority_vector_replaces_the_old_one_and_reports_its_end)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 2, 3);
        rig.play().runAnimation(0.5f);
        rig.playGroup("attack", 2, 3);
        EXPECT_FALSE(rig.play().isPlaying("idle"));
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u);
        EXPECT_EQ(rig.play().endedAnimations[0].groupname, "idle");
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].time, 0.5f);
        EXPECT_FLOAT_EQ(rig.play().endedAnimations[0].completion, 0.25f);
        EXPECT_EQ(rig.play().getActiveGroup(0), "attack");
    }

    TEST(PlayCharacterization, a_different_priority_vector_does_not_replace_and_each_bone_group_picks_its_own_winner)
    {
        PlayRig rig;
        rig.addMain();
        MWAnim::Play::Priority legsFirst(2, 0);
        legsFirst[0] = 5;
        legsFirst[1] = 1;
        MWAnim::Play::Priority armsFirst(2, 0);
        armsFirst[0] = 1;
        armsFirst[1] = 5;
        rig.play().play("idle", legsFirst, 3, true, 1.f, "start", "stop", 0.f, 0, false);
        rig.play().play("attack", armsFirst, 3, true, 1.f, "start", "stop", 0.f, 0, false);
        EXPECT_TRUE(rig.play().isPlaying("idle"));
        EXPECT_TRUE(rig.play().isPlaying("attack"));
        EXPECT_EQ(rig.play().getActiveGroup(0), "idle");
        EXPECT_EQ(rig.play().getActiveGroup(1), "attack");
        EXPECT_TRUE(rig.play().isPriorityActive(5));
        EXPECT_TRUE(rig.play().isPriorityActive(1));
        EXPECT_FALSE(rig.play().isPriorityActive(3));
    }

    TEST(PlayCharacterization, equal_priority_on_a_bone_group_goes_to_the_first_state_by_name)
    {
        PlayRig rig;
        rig.addMain();
        MWAnim::Play::Priority a(2, 0);
        a[0] = 2;
        a[1] = 1;
        MWAnim::Play::Priority b(2, 0);
        b[0] = 2;
        b[1] = 2;
        rig.play().play("walkforward", a, 3, true, 1.f, "start", "stop", 0.f, 0, false);
        rig.play().play("idle", b, 3, true, 1.f, "start", "stop", 0.f, 0, false);
        EXPECT_EQ(rig.play().getActiveGroup(0), "idle") << "2 against 2 on the legs: the earlier name wins the tie";
        EXPECT_EQ(rig.play().getActiveGroup(1), "idle");
    }

    TEST(PlayCharacterization, disabling_the_active_group_hands_the_bone_groups_to_the_next_one)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3);
        rig.playGroup("attack", 2, 2);
        rig.play().disable("attack");
        EXPECT_EQ(rig.play().getActiveGroup(1), "idle");
        ASSERT_EQ(rig.play().endedAnimations.size(), 1u);
        EXPECT_EQ(rig.play().endedAnimations[0].groupname, "attack");
        rig.play().disable("nonsense"); // harmless
        EXPECT_EQ(rig.play().endedAnimations.size(), 1u);
    }

    TEST(PlayCharacterization, playing_a_group_that_already_plays_takes_the_new_priority_and_clears_its_rivals)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3);
        rig.playGroup("attack", 4, 2);
        rig.play().runAnimation(0.5f);
        rig.playGroup("idle", 4, 3); // same group again, with attack's priority
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 0.5f) << "not restarted";
        EXPECT_FALSE(rig.play().isPlaying("attack")) << "attack held the same priority vector";
        EXPECT_TRUE(rig.play().isPriorityActive(4));
        EXPECT_FALSE(rig.play().isPriorityActive(1));
    }

    TEST(PlayCharacterization, an_empty_group_name_only_re_arbitrates)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3);
        rig.play().play("", MWAnim::Play::Priority(1), 3, true, 1.f, "start", "stop", 0.f, 0);
        EXPECT_TRUE(rig.play().isPlaying("idle"));
        EXPECT_EQ(rig.play().getActiveGroup(0), "idle");
    }

    TEST(PlayCharacterization, a_persistent_priority_freezes_the_states_that_do_not_hold_it)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3);
        rig.playGroup("attack", 7, 2);
        rig.play().persistentPriority = 7;
        rig.play().runAnimation(0.5f);
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("idle"), 0.f);
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("attack"), 5.5f);
    }

    // ---- bones and root motion ------------------------------------------------------------------------------------

    TEST(PlayCharacterization, the_bones_are_written_from_the_active_state_on_each_bone_group)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3);
        rig.playGroup("attack", 2, 2); // arms: spine follows attack, the root follows idle
        rig.play().runAnimation(0.5f);
        EXPECT_NEAR(rig.translation("spine").x, 5.5f, 1e-4f) << "attack at 5.5";
        EXPECT_NEAR(rig.translation("bip01").z, 0.5f, 1e-4f) << "idle at 0.5 (z is not reset)";
    }

    TEST(PlayCharacterization, root_motion_is_returned_on_the_accumulated_axes_and_removed_from_the_root_bone)
    {
        // The root moves (100, 10, 1) per second. With the default accumulation (1, 1, 0) the x and y change is
        // returned as movement for the physics and only z stays on the bone.
        PlayRig rig;
        rig.addMain();
        rig.playGroup("attack", 1, 3);
        const vsg::vec3 movement = rig.play().runAnimation(0.5f);
        EXPECT_NEAR(movement.x, 50.f, 1e-3f);
        EXPECT_NEAR(movement.y, 5.f, 1e-3f);
        EXPECT_NEAR(movement.z, 0.f, 1e-3f);
        EXPECT_NEAR(rig.translation("bip01").x, 0.f, 1e-4f);
        EXPECT_NEAR(rig.translation("bip01").y, 0.f, 1e-4f);
        EXPECT_NEAR(rig.translation("bip01").z, 5.5f, 1e-4f);
    }

    TEST(PlayCharacterization, setting_the_accumulated_axes_changes_both_what_stays_on_the_bone_and_the_movement)
    {
        // CHANGED in task 0124, stage C2 (toddhow approved fixing the quirk; this edit is for his review). Before:
        // setAccumulation changed only the reset channel of the playing source, not Play::mAccumulate, which
        // updatePosition uses for the returned movement, so y was both left on the bone AND returned as movement
        // (5), and a new arbitration restored the default axes (the idle's y came back to 0). Now the axes are one
        // value: y is left on the bone and NOT returned, and a new arbitration keeps it (idle's y at 0.5 s is 5).
        PlayRig rig;
        rig.addMain();
        rig.playGroup("attack", 1, 3);
        rig.play().setAccumulation({ 1.f, 0.f, 0.f });
        const vsg::vec3 movement = rig.play().runAnimation(0.5f);
        EXPECT_NEAR(movement.x, 50.f, 1e-3f);
        EXPECT_NEAR(movement.y, 0.f, 1e-3f) << "the movement uses the axes that were set";
        EXPECT_NEAR(rig.translation("bip01").y, 55.f, 1e-3f) << "y is left on the bone now";
        EXPECT_NEAR(rig.translation("bip01").x, 0.f, 1e-4f);
        rig.playGroup("idle", 2, 3); // a new arbitration keeps the axes
        rig.play().runAnimation(0.5f);
        EXPECT_NEAR(rig.translation("bip01").y, 5.f, 1e-3f);
    }

    TEST(PlayCharacterization, only_a_state_that_is_active_on_bone_group_zero_contributes_root_motion)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("attack", 1, 2); // arms only
        const vsg::vec3 movement = rig.play().runAnimation(0.5f);
        EXPECT_EQ(movement.x, 0.f);
        EXPECT_EQ(movement.y, 0.f);
    }

    TEST(PlayCharacterization, a_covered_state_keeps_advancing_but_only_the_active_one_gives_root_motion)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("idle", 1, 3);
        rig.playGroup("attack", 2, 3); // priority vector {2,2} against idle's {1,1}: both play, attack covers idle
        const vsg::vec3 movement = rig.play().runAnimation(0.5f);
        EXPECT_NEAR(movement.x, 50.f, 1e-3f) << "attack's 50, not 100 from both";
        EXPECT_NEAR(rig.play().getCurrentTime("idle"), 0.5f, 1e-5f) << "idle still advances";
    }

    TEST(PlayCharacterization, with_bone_writes_off_time_and_movement_still_run_but_no_bone_moves)
    {
        PlayRig rig;
        rig.addMain();
        rig.play().updateBones = false;
        rig.playGroup("attack", 1, 3);
        const vsg::vec3 movement = rig.play().runAnimation(0.5f);
        EXPECT_NEAR(movement.x, 50.f, 1e-3f);
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("attack"), 5.5f);
        EXPECT_EQ(rig.translation("spine").x, 0.f);
        EXPECT_EQ(rig.translation("bip01").z, 0.f);
        EXPECT_EQ(rig.play().getActiveState(0).groupname, "attack") << "the driver reads the active state";
        EXPECT_NE(rig.play().getActiveState(0).source, nullptr);
    }

    TEST(PlayCharacterization, a_group_without_an_active_state_has_an_empty_active_state)
    {
        PlayRig rig;
        rig.addMain();
        EXPECT_EQ(rig.play().getActiveState(0).source, nullptr);
        EXPECT_TRUE(rig.play().getActiveState(0).groupname.empty());
        EXPECT_TRUE(rig.play().getActiveState(5).groupname.empty());
    }

    TEST(PlayCharacterization, the_velocity_of_a_group_is_its_root_travel_over_its_loop_or_stop_section_on_the_axes)
    {
        // Root path (100 t, 10 t): between the loop keys (2.5 to 3.5) it travels sqrt(100^2 + 10^2) = 100.4988 in one
        // second; idle (0 to 2) travels twice that in twice the time.
        PlayRig rig;
        rig.addMain();
        EXPECT_NEAR(rig.play().getVelocity("walkforward"), 100.4988f, 1e-3f);
        EXPECT_NEAR(rig.play().getVelocity("idle"), 100.4988f, 1e-3f);
        EXPECT_EQ(rig.play().getVelocity("nonsense"), 0.f);
        rig.play().setAccumulation({ 1.f, 0.f, 0.f }); // the axes do not change the cached value
        EXPECT_NEAR(rig.play().getVelocity("walkforward"), 100.4988f, 1e-3f);
    }

    TEST(PlayCharacterization, a_play_without_a_root_bone_has_no_velocity)
    {
        PlayRig rig;
        auto map = vsg::ref_ptr<Anim::ControllerMap>(new Anim::ControllerMap);
        map->tags = vsg::ref_ptr<Anim::Tags>(new Anim::Tags);
        map->tags->emplace(0.f, "idle: start");
        map->tags->emplace(1.f, "idle: stop");
        auto spine = Anim::TransformController::create();
        spine->translate = Anim::make_channel<LinearTrack>(1.f, 0.f, 0.f);
        map->map.emplace("spine", spine);
        rig.play().addSingleAnimSource(map, "no root");
        EXPECT_EQ(rig.play().getVelocity("idle"), 0.f);
        rig.playGroup("idle", 1, 3);
        EXPECT_EQ(rig.play().runAnimation(0.5f).x, 0.f);
    }

    TEST(PlayCharacterization, an_unknown_bone_in_a_source_is_skipped_and_the_rest_play)
    {
        PlayRig rig;
        auto map = makeSource({ { 0.f, "idle: start" }, { 1.f, "idle: stop" } });
        auto extra = Anim::TransformController::create();
        extra->translate = Anim::make_channel<LinearTrack>(1.f, 1.f, 1.f);
        map->map.emplace("no such bone", extra);
        // Play reports the missing bone on std::cerr ("addAnimSource(extra): can't find bone 'no such bone'"); the
        // test does not capture it (a version that used gtest's CaptureStderr ended the process with exit code 3 in a
        // whole-binary run, though it passed alone; cause not found).
        rig.play().addSingleAnimSource(map, "extra");
        rig.playGroup("idle", 1, 3);
        rig.play().runAnimation(0.5f);
        EXPECT_NEAR(rig.translation("spine").x, 0.5f, 1e-4f);
    }

    // ---- sources read from files ---------------------------------------------------------------------------------

    // A minimal Morrowind keyframe file: a text key record and one translation controller per bone, translation
    // linear in time (rate per second given). NIF 4.0.0.2 layout, as components/nif reads it.
    struct KfBone
    {
        std::string mName;
        std::array<float, 3> mRate;
    };

    std::string makeKf(const Keys& textKeys, const std::vector<KfBone>& bones, float stopTime)
    {
        std::string out = "NetImmerse File Format, Version 4.0.0.2\n";
        auto put = [](std::string& s, auto value) { s.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
        auto putString = [&](std::string& s, const std::string& text) {
            put(s, static_cast<std::uint32_t>(text.size()));
            s += text;
        };
        put(out, std::uint32_t{ 0x04000002 });
        const std::int32_t n = static_cast<std::int32_t>(bones.size());
        // Records: 0 helper, 1 text keys, 2..n+1 string extras, n+2..2n+1 controllers, 2n+2..3n+1 key data.
        put(out, static_cast<std::uint32_t>(2 + 3 * n));
        std::string rec;
        auto begin = [&](const char* type) {
            rec.clear();
            putString(rec, type);
        };
        auto flush = [&] { out += rec; };

        begin("NiSequenceStreamHelper");
        putString(rec, "");
        put(rec, std::int32_t{ 1 }); // extra data
        put(rec, n > 0 ? std::int32_t{ n + 2 } : std::int32_t{ -1 }); // first controller
        flush();

        begin("NiTextKeyExtraData");
        put(rec, n > 0 ? std::int32_t{ 2 } : std::int32_t{ -1 }); // next extra
        put(rec, std::uint32_t{ 0 });
        put(rec, static_cast<std::uint32_t>(textKeys.size()));
        for (const auto& [time, text] : textKeys)
        {
            put(rec, time);
            putString(rec, text);
        }
        flush();

        for (std::int32_t i = 0; i < n; ++i)
        {
            begin("NiStringExtraData");
            put(rec, i + 1 < n ? std::int32_t{ 3 + i } : std::int32_t{ -1 });
            put(rec, static_cast<std::uint32_t>(4 + bones[i].mName.size()));
            putString(rec, bones[i].mName);
            flush();
        }
        for (std::int32_t i = 0; i < n; ++i)
        {
            begin("NiKeyframeController");
            put(rec, i + 1 < n ? std::int32_t{ n + 3 + i } : std::int32_t{ -1 }); // next controller
            put(rec, std::uint16_t{ 0 }); // flags
            put(rec, 1.f); // frequency
            put(rec, 0.f); // phase
            put(rec, 0.f); // start
            put(rec, stopTime);
            put(rec, std::int32_t{ -1 }); // target
            put(rec, std::int32_t{ 2 * n + 2 + i }); // data
            flush();
        }
        for (std::int32_t i = 0; i < n; ++i)
        {
            begin("NiKeyframeData");
            put(rec, std::uint32_t{ 0 }); // no rotations
            put(rec, std::uint32_t{ 2 }); // two translation keys
            put(rec, std::uint32_t{ 1 }); // linear
            for (float t : { 0.f, stopTime })
            {
                put(rec, t);
                for (float rate : bones[i].mRate)
                    put(rec, rate * t);
            }
            put(rec, std::uint32_t{ 0 }); // no scales
            flush();
        }
        put(out, std::uint32_t{ 1 }); // roots
        put(out, std::int32_t{ 0 });
        return out;
    }

    vsg::ref_ptr<Anim::ControllerMap> readKf(const std::string& bytes)
    {
        auto options = vsg::Options::create();
        options->extensionHint = ".kf";
        options->setValue("filename", std::string("anim.kf"));
        vsg::ref_ptr<vsgAdapters::kf> reader{ new vsgAdapters::kf };
        std::istringstream stream(bytes);
        auto object = reader->read(stream, options);
        return vsg::ref_ptr<Anim::ControllerMap>(dynamic_cast<Anim::ControllerMap*>(object.get()));
    }

    TEST(PlayCharacterization, a_generated_kf_gives_a_group_table_with_lower_cased_split_text_keys_and_plays)
    {
        // The reader lower-cases text keys and splits one record on CR LF; bone names are matched ignoring case.
        const Keys keys = { { 0.f, "Idle: Start\r\nIdle: Loop Start" }, { 1.f, "Idle: Stop" }, { 1.f, "Sound: Foot" } };
        auto map = readKf(makeKf(keys, { { "Bip01", { 100.f, 10.f, 1.f } }, { "Spine", { 1.f, 0.f, 0.f } } }, 1.f));
        ASSERT_NE(map, nullptr);
        ASSERT_NE(map->tags, nullptr);
        std::vector<std::string> texts;
        for (const auto& [time, text] : *map->tags)
            texts.push_back(std::to_string(static_cast<int>(time)) + " " + text);
        EXPECT_EQ(texts, (std::vector<std::string>{ "0 idle: start", "0 idle: loop start", "1 idle: stop", "1 sound: foot" }));
        EXPECT_TRUE(map->tags->hasGroup("idle"));
        EXPECT_TRUE(map->tags->hasGroup("sound"));
        EXPECT_EQ(map->map.size(), 2u);

        PlayRig rig;
        rig.play().addSingleAnimSource(map, "generated.kf");
        EXPECT_TRUE(rig.play().hasAnimation("idle"));
        rig.playGroup("idle");
        const vsg::vec3 movement = rig.play().runAnimation(0.5f);
        EXPECT_NEAR(movement.x, 50.f, 1e-3f);
        EXPECT_NEAR(rig.translation("bip01").z, 0.5f, 1e-4f);
        EXPECT_NEAR(rig.translation("spine").x, 0.5f, 1e-4f);
        // The key sound: foot is at the stop time, so a step to the end fires it as well.
        rig.play().runAnimation(5.f);
        std::vector<std::string> fired = firedTexts(rig.play());
        EXPECT_NE(std::find(fired.begin(), fired.end(), "sound: foot"), fired.end());
    }

    struct PlayDaeFixture : testing::Test
    {
        std::vector<std::unique_ptr<TestingOpenMW::VFSTestFile>> mFiles;
        std::unique_ptr<VFS::Manager> mVfs;

        vsg::ref_ptr<Anim::ControllerMap> read(const std::string& document, const std::string& textKeys)
        {
            VFS::FileMap fileMap;
            mFiles.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(document));
            fileMap.emplace(VFS::Path::Normalized("meshes/anim.dae"), mFiles.back().get());
            mFiles.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(textKeys));
            fileMap.emplace(VFS::Path::Normalized("meshes/anim.txt"), mFiles.back().get());
            mVfs = TestingOpenMW::createTestVFS(std::move(fileMap));
            auto options = vsg::Options::create();
            options->extensionHint = ".dae";
            options->setValue("filename", std::string("meshes/anim.dae"));
            vsg::ref_ptr<vsgAdapters::daeClips> reader{ new vsgAdapters::daeClips(*mVfs) };
            std::istringstream stream(document);
            auto object = reader->read(stream, options);
            // The reader returns the AnimSet (task 0124, C3); Play is given the controllers inside it.
            const auto* set = dynamic_cast<const Anim::AnimSet*>(object.get());
            return set != nullptr ? vsg::ref_ptr<Anim::ControllerMap>(const_cast<Anim::ControllerMap*>(set->controllers.get()))
                                  : vsg::ref_ptr<Anim::ControllerMap>();
        }
    };

    // A COLLADA model whose "bip01" node moves (0,0,0) to (100,10,1) in one second and "spine" (0,0,0) to (1,0,0),
    // both shifted by their clip's start time.
    std::string makeDaeDocument()
    {
        using namespace ColladaTesting;
        DocumentParts parts;
        parts.mGeometries = makeUnitBoxGeometry("box");
        parts.mAnimations = makeMatrixAnimation("root-anim", "bip01-id", { 0, 1 },
                                {makeTranslationMatrix(0, 0, 0), makeTranslationMatrix(100, 10, 1)})
            + makeMatrixAnimation(
                "spine-anim", "spine-id", { 0, 1 }, { makeTranslationMatrix(0, 0, 0), makeTranslationMatrix(1, 0, 0) });
        parts.mAnimationClips = makeAnimationClip("walk", 10, 11, { "root-anim", "spine-anim" });
        parts.mNodes = makeNode("Root", "Root",
            makeMatrix(identityMatrix) + makeInstanceGeometry("box")
                + makeNode("bip01-id", "bip01", makeMatrix(makeTranslationMatrix(0, 0, 0)))
                + makeNode("spine-id", "spine", makeMatrix(makeTranslationMatrix(0, 0, 0))));
        return makeColladaDocument(parts);
    }

    // --- Task 0124, stage C2: setAccumulation sets the axes the movement uses, and the active state names its AnimSet.

    TEST(PlayCharacterization, the_accumulated_axes_default_to_x_and_y_and_are_the_ones_last_set)
    {
        PlayRig rig;
        EXPECT_EQ(rig.play().accumulation(), vsg::vec3(1.f, 1.f, 0.f));
        rig.play().setAccumulation({ 0.f, 0.f, 0.f });
        EXPECT_EQ(rig.play().accumulation(), vsg::vec3(0.f, 0.f, 0.f));
        rig.play().setAccumulation({ 1.f, 0.f, 1.f });
        EXPECT_EQ(rig.play().accumulation(), vsg::vec3(1.f, 0.f, 1.f));
    }

    TEST(PlayCharacterization, axes_set_before_any_animation_plays_are_used_by_the_movement_and_the_reset)
    {
        // CharacterController sets them once, in its constructor, before anything plays; non-actors get (0, 0, 0).
        PlayRig rig;
        rig.addMain();
        rig.play().setAccumulation({ 0.f, 0.f, 0.f });
        rig.playGroup("attack", 1, 3);
        const vsg::vec3 movement = rig.play().runAnimation(0.5f);
        EXPECT_EQ(movement, vsg::vec3(0.f, 0.f, 0.f)) << "no axis accumulates: nothing is handed to the physics";
        EXPECT_NEAR(rig.translation("bip01").x, 550.f, 1e-2f) << "the root keeps its whole motion on the bone";
        EXPECT_NEAR(rig.translation("bip01").y, 55.f, 1e-3f);
    }

    TEST(PlayCharacterization, the_active_state_names_the_shared_anim_set_of_its_source)
    {
        PlayRig rig;
        rig.addMain();
        rig.playGroup("attack", 1, 3);
        const MWAnim::Play::ActiveState state = rig.play().getActiveState(0);
        ASSERT_NE(state.source, nullptr);
        ASSERT_NE(state.set, nullptr);
        EXPECT_EQ(state.set->controllers.get(), state.source);
        EXPECT_TRUE(state.set->hasGroup("attack"));
        EXPECT_EQ(rig.play().getActiveState(5).set, nullptr);
    }

    TEST(PlayCharacterization, two_plays_of_one_source_share_its_anim_set)
    {
        auto map = makeSource(mainKeys());
        PlayRig a, b;
        a.play().addSingleAnimSource(map, "shared");
        b.play().addSingleAnimSource(map, "shared");
        a.playGroup("attack", 1, 3);
        b.playGroup("attack", 1, 3);
        EXPECT_EQ(a.play().getActiveState(0).set, b.play().getActiveState(0).set);
    }

    TEST_F(PlayDaeFixture, a_generated_dae_with_a_text_key_file_gives_the_same_kind_of_group_table_and_plays)
    {
        // The .txt file has one "group: key time" per line. Line 4 is the suite's own typo (BasicPlayer.txt: "hit1
        // stop 101.06" without the colon): it is stored as a key of the group-less text "hit1 stop", so "hit1" has a
        // start key and no stop key and can never be played.
        const std::string textKeys = "walk: start 10\nwalk: hit 10.5\nwalk: stop 11\nhit1: start 12\nhit1 stop 13\n";
        auto map = read(makeDaeDocument(), textKeys);
        ASSERT_NE(map, nullptr);
        ASSERT_NE(map->tags, nullptr);
        EXPECT_TRUE(map->tags->hasGroup("walk"));
        EXPECT_TRUE(map->tags->hasGroup("hit1"));
        EXPECT_EQ(map->map.count("bip01"), 1u);

        PlayRig rig;
        rig.play().addSingleAnimSource(map, "generated.dae");
        EXPECT_TRUE(rig.play().hasAnimation("walk"));
        EXPECT_FLOAT_EQ(rig.play().getStartTime("walk"), 10.f);
        EXPECT_FLOAT_EQ(rig.play().getTextKeyTime("walk: hit"), 10.5f);
        rig.playGroup("hit1");
        EXPECT_FALSE(rig.play().isPlaying("hit1")) << "the suite's typo leaves hit1 without a stop key";
        rig.playGroup("walk");
        EXPECT_TRUE(rig.play().isPlaying("walk"));
        EXPECT_FLOAT_EQ(rig.play().getCurrentTime("walk"), 10.f);
        const vsg::vec3 movement = rig.play().runAnimation(0.5f);
        EXPECT_NEAR(movement.x, 50.f, 1e-2f) << "the clip's animation is shifted to the clip start time";
        EXPECT_NEAR(movement.y, 5.f, 1e-2f);
        EXPECT_EQ(firedTexts(rig.play()), (std::vector<std::string>{ "walk: start", "walk: hit" }));
        EXPECT_NEAR(rig.translation("spine").x, 0.5f, 1e-3f);
    }
}
