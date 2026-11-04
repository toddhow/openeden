// Tests for task 0074b, the review of task 0074 (MWRender::PartControllers): a part's auto-play clock stays with the
// part through an equipment change; the entries keep their controllers and targets alive; the relink runs on every
// exit of an update. Written from the review's findings before the implementation and run red against stubs.

#include "apps/openmw/mwrender/partcontrollers.hpp"

#include <components/animation/autoplay.hpp>
#include <components/animation/controller.hpp>

#include <vsg/core/Object.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/core/ref_ptr.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace
{
    using MWRender::PartControllerList;

    constexpr std::size_t numParts = 8;
    constexpr std::size_t chest = 3;
    constexpr std::size_t hands = 5;
    using Log = MWRender::PartControllers<numParts>;

    constexpr float frameTime = 1.f / 60.f;

    // An auto-play controller that records every time it is run with.
    struct Recorder : public Anim::Controller
    {
        explicit Recorder(int phaseGroup)
        {
            hints.autoPlay = true;
            hints.phaseGroup = phaseGroup;
        }
        void run(vsg::Object& /*target*/, float time) const override { mTimes.push_back(time); }
        mutable std::vector<float> mTimes;
    };

    struct PartClocksTest : public ::testing::Test
    {
        std::vector<vsg::ref_ptr<Recorder>> mRecorders;
        std::vector<vsg::ref_ptr<vsg::Object>> mTargets;

        Recorder* recorder(int phaseGroup)
        {
            mRecorders.emplace_back(new Recorder(phaseGroup));
            return mRecorders.back().get();
        }

        vsg::Object* target()
        {
            mTargets.push_back(vsg::Object::create());
            return mTargets.back().get();
        }
    };

    // The relink of Npc::relinkSurvivingParts, reduced to what it links.
    std::vector<std::size_t> relinkParts(Log& log)
    {
        std::vector<std::size_t> parts;
        log.relinkSurvivors(
            [&](std::size_t part, const std::vector<Log::Entry>& /*entries*/) { parts.push_back(part); });
        return parts;
    }

    float runFrames(Log& log, int frames)
    {
        float elapsed = 0;
        for (int i = 0; i < frames; ++i)
        {
            log.update(frameTime);
            elapsed += frameTime;
        }
        return elapsed;
    }

    TEST_F(PartClocksTest, a_kept_part_keeps_its_clock_through_an_equipment_change)
    {
        Log log;
        Recorder* steady = recorder(0); // AnimFlag_NotRandom: phase group 0
        Recorder* random = recorder(2); // a random phase group, timer from std::rand
        log.beginPart(chest);
        log.addAutoPlay(chest, steady, target());
        log.addAutoPlay(chest, random, target());
        runFrames(log, 10);
        ASSERT_EQ(steady->mTimes.size(), 10u);
        ASSERT_EQ(random->mTimes.size(), 10u);
        const float steadyBefore = steady->mTimes.back();
        const float randomBefore = random->mTimes.back();

        // The hands are replaced, the chest stays.
        log.runUpdate(
            [&] {
                log.beginPart(hands);
                log.addAutoPlay(hands, recorder(0), target());
            },
            [&] { relinkParts(log); });
        runFrames(log, 1);

        ASSERT_EQ(steady->mTimes.size(), 11u);
        ASSERT_EQ(random->mTimes.size(), 11u);
        EXPECT_EQ(steady->mTimes.back(), steadyBefore + frameTime);
        EXPECT_EQ(random->mTimes.back(), randomBefore + frameTime);
    }

    TEST_F(PartClocksTest, a_part_inserted_during_the_update_starts_a_fresh_clock_as_before)
    {
        Log log;
        log.beginPart(chest);
        log.addAutoPlay(chest, recorder(0), target());
        runFrames(log, 30);

        Recorder* torch = recorder(0);
        log.runUpdate(
            [&] {
                log.beginPart(hands);
                log.addAutoPlay(hands, torch, target());
            },
            [&] { relinkParts(log); });
        runFrames(log, 1);

        // Its first run is one frame after its insertion, not the half second the chest's clock has run: a controller
        // that measures the time between its runs (a light's flicker) sees one frame.
        ASSERT_EQ(torch->mTimes.size(), 1u);
        EXPECT_EQ(torch->mTimes[0], frameTime);
    }

    TEST_F(PartClocksTest, a_part_inserted_again_drops_its_old_clock)
    {
        Log log;
        Recorder* old = recorder(0);
        log.beginPart(chest);
        log.addAutoPlay(chest, old, target());
        log.addAutoPlay(chest, recorder(4), target());
        runFrames(log, 5);

        Recorder* replacement = recorder(0);
        log.beginPart(chest);
        log.addAutoPlay(chest, replacement, target());
        runFrames(log, 2);

        EXPECT_EQ(old->mTimes.size(), 5u);
        ASSERT_EQ(replacement->mTimes.size(), 2u);
        EXPECT_EQ(replacement->mTimes[0], frameTime);
        EXPECT_EQ(log.autoPlay(chest).groups.size(), 1u);
    }

    TEST_F(PartClocksTest, a_removed_part_never_runs_again)
    {
        Log log;
        Recorder* removed = recorder(1);
        log.beginPart(chest);
        log.addAutoPlay(chest, removed, target());
        runFrames(log, 3);

        log.forget(chest);
        runFrames(log, 3);

        EXPECT_EQ(removed->mTimes.size(), 3u);
        EXPECT_TRUE(log.autoPlay(chest).groups.empty());
        EXPECT_TRUE(log.entries(chest).empty());
    }

    TEST_F(PartClocksTest, auto_play_controllers_are_recorded_with_their_kind)
    {
        Log log;
        const Recorder* flicker = recorder(0);
        vsg::Object* light = target();
        log.beginPart(hands);
        log.addAutoPlay(hands, flicker, light);

        ASSERT_EQ(log.entries(hands).size(), 1u);
        EXPECT_EQ(log.entries(hands)[0].mController, flicker);
        EXPECT_EQ(log.entries(hands)[0].mTarget, light);
        EXPECT_EQ(log.entries(hands)[0].mList, PartControllerList::AutoPlay);
        ASSERT_EQ(log.autoPlay(hands).groups.size(), 1u);
        EXPECT_EQ(log.autoPlay(hands).groups[0].phaseGroup, 0);
        ASSERT_EQ(log.autoPlay(hands).groups[0].controllers.size(), 1u);
        EXPECT_EQ(log.autoPlay(hands).groups[0].controllers[0].first, flicker);
        EXPECT_EQ(log.autoPlay(hands).groups[0].controllers[0].second, light);
    }

    TEST_F(PartClocksTest, two_hundred_equipment_changes_keep_the_clock_and_its_groups)
    {
        Log log;
        Recorder* steady = recorder(0);
        log.beginPart(chest);
        log.addAutoPlay(chest, steady, target());
        log.addAutoPlay(chest, recorder(3), target());

        float expected = 0;
        for (int change = 0; change < 200; ++change)
        {
            log.runUpdate(
                [&] {
                    log.beginPart(hands);
                    log.addAutoPlay(hands, recorder(1), target());
                    log.addAutoPlay(hands, recorder(2), target());
                },
                [&] { relinkParts(log); });
            log.update(frameTime);
            expected += frameTime;
        }

        ASSERT_EQ(steady->mTimes.size(), 200u);
        EXPECT_EQ(steady->mTimes.back(), expected);
        EXPECT_EQ(log.autoPlay(chest).groups.size(), 2u);
        EXPECT_EQ(log.autoPlay(hands).groups.size(), 2u);
        EXPECT_EQ(log.entries(hands).size(), 2u);
    }

    TEST_F(PartClocksTest, the_relink_runs_when_the_update_throws)
    {
        Log log;
        log.beginPart(chest);
        log.record(chest, recorder(0), target(), PartControllerList::Static);

        int relinks = 0;
        std::vector<std::size_t> linked;
        EXPECT_THROW(log.runUpdate(
                         [&] {
                             log.beginPart(hands);
                             log.record(hands, recorder(0), target(), PartControllerList::Static);
                             // vsgUtil::readNode on a model it cannot read (tested on data, so the compiler does not
                             // see a body that always throws: MSVC's C4702 on the relink after it)
                             if (!log.entries(hands).empty())
                                 throw std::runtime_error("!readNode(\"meshes/missing.nif\")");
                         },
                         [&] {
                             ++relinks;
                             linked = relinkParts(log);
                         }),
            std::runtime_error);

        EXPECT_EQ(relinks, 1);
        ASSERT_EQ(linked.size(), 1u);
        EXPECT_EQ(linked[0], chest);
    }

    TEST_F(PartClocksTest, the_relink_runs_once_when_the_update_returns_early)
    {
        Log log;
        log.beginPart(chest);
        log.record(chest, recorder(0), target(), PartControllerList::Static);

        int relinks = 0;
        std::vector<std::size_t> linked;
        log.runUpdate(
            [&] {
                // Npc::updateParts' head-only view returns here
                return;
            },
            [&] {
                ++relinks;
                linked = relinkParts(log);
            });

        EXPECT_EQ(relinks, 1);
        ASSERT_EQ(linked.size(), 1u);
        EXPECT_EQ(linked[0], chest);
    }

    TEST_F(PartClocksTest, an_entry_keeps_its_controller_and_target_alive)
    {
        Log log;
        vsg::ref_ptr<Recorder> controller(new Recorder(0));
        vsg::ref_ptr<vsg::Object> geometry = vsg::Object::create();
        const vsg::observer_ptr<Recorder> controllerAlive(controller);
        const vsg::observer_ptr<vsg::Object> geometryAlive(geometry);
        log.beginPart(chest);
        log.record(chest, controller.get(), geometry.get(), PartControllerList::Static);

        // The part's node goes another way than forget (a removePath outside Npc::removeIndividualPart).
        controller = {};
        geometry = {};

        EXPECT_TRUE(controllerAlive.valid());
        EXPECT_TRUE(geometryAlive.valid());
        log.forget(chest);
        EXPECT_FALSE(controllerAlive.valid());
        EXPECT_FALSE(geometryAlive.valid());
    }
}
