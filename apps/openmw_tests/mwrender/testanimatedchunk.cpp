// Task 0096 stage d: MWRender::AnimatedChunk, the moving objects of one object paging chunk. Controllers run at the
// rate of the object's distance within a shared budget, keep their phase, and cannot outlive the nodes they move.
// Written from the task spec before the implementation.

#include "apps/openmw/mwrender/animatedchunk.hpp"

#include "apps/openmw_tests/support/allocationcounter.hpp"

#include <components/animation/controller.hpp>

#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{
    using MWRender::AnimatedChunk;

    int sControllersAlive = 0;
    int sTargetsAlive = 0;

    // Records every time it was run with.
    struct RecordingController : public Anim::Controller
    {
        mutable std::vector<float> times;
        mutable int runs = 0;
        RecordingController(int phaseGroup = 0)
        {
            hints.autoPlay = true;
            hints.phaseGroup = phaseGroup;
            ++sControllersAlive;
        }
        ~RecordingController() override { --sControllersAlive; }
        void run(vsg::Object&, float time) const override
        {
            ++runs;
            if (times.size() < 4096)
                times.push_back(time);
        }
    };

    struct CountedTarget : public vsg::Inherit<vsg::Object, CountedTarget>
    {
        CountedTarget() { ++sTargetsAlive; }
        ~CountedTarget() override { --sTargetsAlive; }
    };

    struct AnimatedChunkTest : public ::testing::Test
    {
        std::vector<vsg::ref_ptr<RecordingController>> mControllers;

        void SetUp() override
        {
            sControllersAlive = 0;
            sTargetsAlive = 0;
        }

        AnimatedChunk::Controllers one(int phaseGroup = 0)
        {
            auto controller = vsg::ref_ptr<RecordingController>(new RecordingController(phaseGroup));
            mControllers.push_back(controller);
            return { { controller, vsg::ref_ptr<vsg::Object>(new CountedTarget) } };
        }

        AnimatedChunk::Controllers several(int count)
        {
            AnimatedChunk::Controllers all;
            for (int i = 0; i < count; ++i)
            {
                auto more = one();
                all.insert(all.end(), more.begin(), more.end());
            }
            return all;
        }

        static unsigned frame(AnimatedChunk& chunk, const vsg::dvec3& camera, double dt, unsigned number,
            unsigned budget = 100000, float stop = 1000.f)
        {
            Lod::AnimBudget b{ budget, budget };
            return chunk.update(camera, dt, stop, number, b);
        }
    };
}

TEST_F(AnimatedChunkTest, an_object_with_no_controllers_adds_nothing)
{
    auto chunk = AnimatedChunk::create();
    chunk->addObject({ 0, 0, 0 }, 1, {});
    chunk->finish();
    EXPECT_EQ(chunk->objectCount(), 0u);
    EXPECT_EQ(chunk->controllerCount(), 0u);
    EXPECT_EQ(frame(*chunk, { 0, 0, 0 }, 0.016, 0), 0u);
}

TEST_F(AnimatedChunkTest, counts_objects_and_controllers)
{
    auto chunk = AnimatedChunk::create();
    chunk->addObject({ 0, 0, 0 }, 1, several(3));
    chunk->addObject({ 10, 0, 0 }, 2, one());
    chunk->finish();
    EXPECT_EQ(chunk->objectCount(), 2u);
    EXPECT_EQ(chunk->controllerCount(), 4u);
}

TEST_F(AnimatedChunkTest, a_near_object_runs_all_its_controllers_every_frame_with_the_time_so_far)
{
    auto chunk = AnimatedChunk::create();
    auto controllers = several(2);
    chunk->addObject({ 100, 0, 0 }, 7, controllers);
    chunk->finish();
    for (unsigned f = 0; f < 10; ++f)
        EXPECT_EQ(frame(*chunk, { 0, 0, 0 }, 0.5, f), 2u);
    for (const auto& [controller, target] : controllers)
    {
        const auto& times = static_cast<const RecordingController&>(*controller).times;
        ASSERT_EQ(times.size(), 10u);
        for (unsigned f = 0; f < 10; ++f)
            EXPECT_FLOAT_EQ(times[f], 0.5f * static_cast<float>(f + 1));
    }
}

TEST_F(AnimatedChunkTest, a_far_object_runs_less_often_but_its_time_is_the_time_that_passed)
{
    auto chunk = AnimatedChunk::create();
    auto controllers = one();
    chunk->addObject({ 500, 0, 0 }, 3, controllers); // between 25 and 60 percent of 1000: a quarter rate
    chunk->finish();
    unsigned ran = 0;
    for (unsigned f = 0; f < 40; ++f)
        ran += frame(*chunk, { 0, 0, 0 }, 0.01, f);
    EXPECT_EQ(ran, 10u);
    EXPECT_EQ(chunk->countInTier(Lod::AnimTier::Quarter), 1u);
    const auto& times = static_cast<const RecordingController&>(*controllers[0].first).times;
    ASSERT_EQ(times.size(), 10u);
    // The last run came in the last four frames: the time given is the whole time up to it.
    EXPECT_GE(times.back(), 0.37f - 1e-4f);
    EXPECT_LE(times.back(), 0.40f + 1e-4f);
    for (std::size_t i = 1; i < times.size(); ++i)
        EXPECT_NEAR(times[i] - times[i - 1], 0.04f, 1e-5f);
}

TEST_F(AnimatedChunkTest, an_object_beyond_the_stop_distance_does_not_run_and_catches_up_when_it_comes_back)
{
    auto chunk = AnimatedChunk::create();
    auto controllers = one();
    chunk->addObject({ 5000, 0, 0 }, 3, controllers);
    chunk->finish();
    for (unsigned f = 0; f < 50; ++f)
        EXPECT_EQ(frame(*chunk, { 0, 0, 0 }, 0.02, f), 0u);
    EXPECT_EQ(chunk->countInTier(Lod::AnimTier::Stopped), 1u);
    EXPECT_EQ(frame(*chunk, { 5000, 0, 0 }, 0.02, 50), 1u);
    const auto& times = static_cast<const RecordingController&>(*controllers[0].first).times;
    ASSERT_EQ(times.size(), 1u);
    EXPECT_NEAR(times[0], 51 * 0.02f, 1e-4f);
}

TEST_F(AnimatedChunkTest, the_budget_limits_the_controllers_per_frame_over_all_chunks)
{
    auto a = AnimatedChunk::create();
    auto b = AnimatedChunk::create();
    for (int i = 0; i < 4; ++i)
    {
        a->addObject({ 10, 0, 0 }, 10 + i, several(5));
        b->addObject({ 10, 0, 0 }, 20 + i, several(5));
    }
    a->finish();
    b->finish();
    for (unsigned f = 0; f < 20; ++f)
    {
        Lod::AnimBudget budget{ 20, 20 };
        const unsigned ran
            = a->update({ 0, 0, 0 }, 0.01, 1000.f, f, budget) + b->update({ 0, 0, 0 }, 0.01, 1000.f, f, budget);
        EXPECT_LE(ran, 20u);
        EXPECT_GT(ran, 0u);
    }
}

TEST_F(AnimatedChunkTest, a_phase_group_starts_at_a_stable_time_from_the_object_id)
{
    auto make = [this](std::uint64_t id) {
        auto chunk = AnimatedChunk::create();
        auto controllers = one(2);
        chunk->addObject({ 10, 0, 0 }, id, controllers);
        chunk->finish();
        frame(*chunk, { 0, 0, 0 }, 0.5, 0);
        return static_cast<const RecordingController&>(*controllers[0].first).times.at(0);
    };
    const float a1 = make(41);
    const float a2 = make(41);
    const float b = make(42);
    EXPECT_FLOAT_EQ(a1, a2);
    EXPECT_NE(a1, b);
    EXPECT_FLOAT_EQ(a1, Lod::animStartTime(41, 2) + 0.5f);
    EXPECT_GE(a1, 0.5f);
}

TEST_F(AnimatedChunkTest, controllers_of_different_phase_groups_keep_their_own_clocks)
{
    auto chunk = AnimatedChunk::create();
    auto zero = one(0);
    auto two = one(2);
    AnimatedChunk::Controllers both = zero;
    both.insert(both.end(), two.begin(), two.end());
    chunk->addObject({ 10, 0, 0 }, 5, both);
    chunk->finish();
    frame(*chunk, { 0, 0, 0 }, 1.0, 0);
    frame(*chunk, { 0, 0, 0 }, 1.0, 1);
    const auto& t0 = static_cast<const RecordingController&>(*zero[0].first).times;
    const auto& t2 = static_cast<const RecordingController&>(*two[0].first).times;
    EXPECT_FLOAT_EQ(t0[1], 2.f);
    EXPECT_FLOAT_EQ(t2[1], Lod::animStartTime(5, 2) + 2.f);
}

TEST_F(AnimatedChunkTest, the_chunk_keeps_its_controllers_and_targets_alive_and_frees_them_with_it)
{
    auto chunk = AnimatedChunk::create();
    chunk->addObject({ 10, 0, 0 }, 1, one());
    chunk->addObject({ 20, 0, 0 }, 2, one());
    chunk->finish();
    mControllers.clear(); // the test's own references
    EXPECT_EQ(sControllersAlive, 2);
    EXPECT_EQ(sTargetsAlive, 2);
    EXPECT_EQ(frame(*chunk, { 0, 0, 0 }, 0.01, 0), 2u);
    chunk = nullptr;
    EXPECT_EQ(sControllersAlive, 0);
    EXPECT_EQ(sTargetsAlive, 0);
}

TEST_F(AnimatedChunkTest, a_chunk_hung_on_a_node_dies_with_the_node)
{
    auto node = vsg::Group::create();
    {
        auto chunk = AnimatedChunk::create();
        chunk->addObject({ 10, 0, 0 }, 1, one());
        chunk->finish();
        node->setObject(AnimatedChunk::sKey, chunk);
    }
    mControllers.clear();
    ASSERT_NE(node->getObject<AnimatedChunk>(AnimatedChunk::sKey), nullptr);
    EXPECT_EQ(sControllersAlive, 1);
    node = nullptr;
    EXPECT_EQ(sControllersAlive, 0);
    EXPECT_EQ(sTargetsAlive, 0);
}

TEST_F(AnimatedChunkTest, two_chunks_built_the_same_way_run_the_same_way)
{
    auto run = [this] {
        auto chunk = AnimatedChunk::create();
        std::vector<AnimatedChunk::Controllers> all;
        for (int i = 0; i < 12; ++i)
        {
            all.push_back(one());
            chunk->addObject({ 80.0 * i, 0, 0 }, 100 + i, all.back());
        }
        chunk->finish();
        std::vector<float> log;
        for (unsigned f = 0; f < 60; ++f)
        {
            frame(*chunk, { 0, 0, 0 }, 0.016, f, 5);
            for (const auto& c : all)
                log.push_back(static_cast<float>(static_cast<const RecordingController&>(*c[0].first).runs));
        }
        return log;
    };
    EXPECT_EQ(run(), run());
}

TEST_F(AnimatedChunkTest, update_does_not_allocate)
{
    auto chunk = AnimatedChunk::create();
    for (int i = 0; i < 32; ++i)
        chunk->addObject({ 60.0 * i, 0, 0 }, 100 + i, one());
    chunk->finish();
    // Warm up: the recording controllers push into their vectors until they hold 4096 entries; stop that first.
    for (auto& controller : mControllers)
        controller->times.assign(4096, 0.f);
    frame(*chunk, { 0, 0, 0 }, 0.016, 0);
    TestSupport::AllocationCounter counter;
    for (unsigned f = 1; f < 200; ++f)
        frame(*chunk, { 0, 0, 0 }, 0.016, f, 10);
    EXPECT_EQ(counter.count(), 0u);
}

// Task 0096 stage e: the distance is measured on the ground plane (toddhow, 2026-11-11).
TEST_F(AnimatedChunkTest, a_camera_far_above_an_object_does_not_stop_its_animation)
{
    auto chunk = AnimatedChunk::create();
    chunk->addObject({ 100, 0, 0 }, 1, one());
    chunk->finish();
    // 30,000 units up but 100 across: near in the plane, so it runs every frame at the full rate.
    for (unsigned f = 0; f < 8; ++f)
        EXPECT_EQ(frame(*chunk, { 0, 0, 30000 }, 0.01, f, 100000, 1000.f), 1u);
    EXPECT_EQ(chunk->countInTier(Lod::AnimTier::Full), 1u);
}

TEST_F(AnimatedChunkTest, an_object_far_across_the_plane_still_stops_whatever_the_height)
{
    auto chunk = AnimatedChunk::create();
    chunk->addObject({ 5000, 0, 0 }, 1, one());
    chunk->finish();
    for (unsigned f = 0; f < 8; ++f)
        EXPECT_EQ(frame(*chunk, { 0, 0, 10 }, 0.01, f, 100000, 1000.f), 0u);
    EXPECT_EQ(chunk->countInTier(Lod::AnimTier::Stopped), 1u);
}

// Task 0096 stage e: skinned objects (banners and flags). The skin controllers run after the object's own controllers,
// every time the object runs, are part of its cost, and the model the skin's bones belong to lives as long as the
// chunk.
namespace
{
    std::vector<int>* sOrder = nullptr;

    struct OrderController : public Anim::Controller
    {
        int mark;
        OrderController(int in_mark, bool autoPlay)
            : mark(in_mark)
        {
            hints.autoPlay = autoPlay;
        }
        void run(vsg::Object&, float) const override
        {
            if (sOrder != nullptr)
                sOrder->push_back(mark);
        }
    };

    struct LiveNode : public vsg::Inherit<vsg::Group, LiveNode>
    {
        LiveNode() { ++sNodesAlive; }
        ~LiveNode() override { --sNodesAlive; }
        static inline int sNodesAlive = 0;
    };

    AnimatedChunk::Controllers marked(int mark, bool autoPlay)
    {
        return { { vsg::ref_ptr<const Anim::Controller>(new OrderController(mark, autoPlay)),
            vsg::ref_ptr<vsg::Object>(new CountedTarget) } };
    }
}

TEST_F(AnimatedChunkTest, skins_run_after_the_object_controllers_each_time_the_object_runs)
{
    std::vector<int> order;
    sOrder = &order;
    auto chunk = AnimatedChunk::create();
    auto controllers = marked(1, true);
    const auto more = marked(2, true);
    controllers.insert(controllers.end(), more.begin(), more.end());
    chunk->addObject({ 10, 0, 0 }, 5, controllers, marked(9, false));
    chunk->finish();
    EXPECT_EQ(chunk->skinCount(), 1u);
    EXPECT_EQ(frame(*chunk, { 0, 0, 0 }, 0.016, 0), 3u);
    EXPECT_EQ(frame(*chunk, { 0, 0, 0 }, 0.016, 1), 3u);
    sOrder = nullptr;
    ASSERT_EQ(order.size(), 6u);
    EXPECT_EQ(order[2], 9);
    EXPECT_EQ(order[5], 9);
}

TEST_F(AnimatedChunkTest, an_object_with_only_skins_adds_nothing)
{
    auto chunk = AnimatedChunk::create();
    chunk->addObject({ 10, 0, 0 }, 5, {}, marked(9, false));
    chunk->finish();
    EXPECT_EQ(chunk->objectCount(), 0u);
    EXPECT_EQ(chunk->skinCount(), 0u);
}

TEST_F(AnimatedChunkTest, the_skins_are_part_of_the_cost_against_the_budget)
{
    auto chunk = AnimatedChunk::create();
    auto first = one();
    auto skins = marked(9, false);
    const auto moreSkins = marked(8, false);
    skins.insert(skins.end(), moreSkins.begin(), moreSkins.end());
    chunk->addObject({ 10, 0, 0 }, 1, first, skins); // costs 1 + 2
    chunk->addObject({ 20, 0, 0 }, 2, one(), marked(7, false)); // costs 1 + 1
    chunk->finish();
    Lod::AnimBudget budget{ 4, 4 };
    // The first object runs and leaves 1; the second needs 2 and is not the first of the frame: only 3 ran.
    EXPECT_EQ(chunk->update({ 0, 0, 0 }, 0.016, 1000.f, 0, budget), 3u);
}

TEST_F(AnimatedChunkTest, the_model_given_to_keep_alive_lives_as_long_as_the_chunk)
{
    LiveNode::sNodesAlive = 0;
    auto chunk = AnimatedChunk::create();
    chunk->addObject({ 10, 0, 0 }, 1, one(), marked(9, false), vsg::ref_ptr<vsg::Node>(new LiveNode));
    chunk->finish();
    EXPECT_EQ(LiveNode::sNodesAlive, 1);
    chunk = nullptr;
    EXPECT_EQ(LiveNode::sNodesAlive, 0);
}

TEST_F(AnimatedChunkTest, update_with_skins_does_not_allocate)
{
    auto chunk = AnimatedChunk::create();
    for (std::uint64_t i = 0; i < 16; ++i)
        chunk->addObject({ double(i) * 10, 0, 0 }, i + 1, one(), marked(9, false));
    chunk->finish();
    for (auto& controller : mControllers)
        controller->times.assign(4096, 0.f);
    frame(*chunk, { 0, 0, 0 }, 0.016, 0);
    TestSupport::AllocationCounter counter;
    for (unsigned f = 1; f < 200; ++f)
        frame(*chunk, { 0, 0, 0 }, 0.016, f, 10);
    EXPECT_EQ(counter.count(), 0u);
}
